using System.Runtime.InteropServices;

namespace SmpackGui.Core.Audio;

/// <summary>
/// Windows Media Foundation, called through raw COM vtables.
/// decode any audio file Windows understands (MP3, WAV, AAC, FLAC, WMA) to PCM, and encode PCM to MP3.
/// </summary>
public static unsafe class MediaFoundation
{
    private const int MF_VERSION = 0x00020070;
    private const int SOURCE_READER_ALL_STREAMS = unchecked((int)0xFFFFFFFE);
    private const int SOURCE_READER_FIRST_AUDIO = unchecked((int)0xFFFFFFFD);
    private const int READERF_ENDOFSTREAM = 0x2;

    private static readonly Guid MT_MAJOR_TYPE = new("48eba18e-f8c9-4687-bf11-0a74c9f96a8f");
    private static readonly Guid MT_SUBTYPE = new("f7e34c9a-42e8-4714-b74b-cb29d72c35e5");
    private static readonly Guid MT_AUDIO_NUM_CHANNELS = new("37e48bf5-645e-4c5b-89de-ada9e29b696a");
    private static readonly Guid MT_AUDIO_SAMPLES_PER_SECOND = new("5faeeae7-0290-4c31-9e8a-c534f68d9dba");
    private static readonly Guid MT_AUDIO_AVG_BYTES_PER_SECOND = new("1aab75c8-cfef-451c-ab95-ac034b8e1731");
    private static readonly Guid MT_AUDIO_BLOCK_ALIGNMENT = new("322de230-9eeb-43bd-ab7a-ff412251541d");
    private static readonly Guid MT_AUDIO_BITS_PER_SAMPLE = new("f2deb57f-40fa-4764-aa33-ed4f2d1ff669");
    private static readonly Guid MT_ALL_SAMPLES_INDEPENDENT = new("c9173739-5e56-461c-b713-46fb995cb95f");
    private static readonly Guid MEDIA_TYPE_AUDIO = new("73647561-0000-0010-8000-00aa00389b71");
    private static readonly Guid AUDIO_FORMAT_PCM = new("00000001-0000-0010-8000-00aa00389b71");
    private static readonly Guid AUDIO_FORMAT_MP3 = new("00000055-0000-0010-8000-00aa00389b71");

    [DllImport("mfplat.dll")] private static extern int MFStartup(int version, int flags);
    [DllImport("mfplat.dll")] private static extern int MFShutdown();
    [DllImport("mfplat.dll")] private static extern int MFCreateMediaType(out IntPtr type);
    [DllImport("mfplat.dll")] private static extern int MFCreateSample(out IntPtr sample);
    [DllImport("mfplat.dll")] private static extern int MFCreateMemoryBuffer(int max_length, out IntPtr buffer);
    [DllImport("mfreadwrite.dll", CharSet = CharSet.Unicode)]
    private static extern int MFCreateSourceReaderFromURL(string url, IntPtr attributes, out IntPtr reader);
    [DllImport("mfreadwrite.dll", CharSet = CharSet.Unicode)]
    private static extern int MFCreateSinkWriterFromURL(string url, IntPtr byte_stream, IntPtr attributes, out IntPtr writer);

    // vtable stuff.

    private static void* Slot(IntPtr obj, int index) => (*(void***)obj)[index];

    private static void Check(int hr, string what)
    {
        if (hr < 0)
            throw new COMException($"Media Foundation: {what} failed (0x{hr:X8})", hr);
    }

    private static void Release(IntPtr p)
    {
        if (p != IntPtr.Zero)
            Marshal.Release(p);
    }

    // IMFAttributes
    private static int GetUINT32(IntPtr a, Guid key, out int v)
    {
        int r = 0;
        int hr = ((delegate* unmanaged[Stdcall]<IntPtr, Guid*, int*, int>)Slot(a, 7))(a, &key, &r);
        v = r;
        return hr;
    }
    private static void SetUINT32(IntPtr a, Guid key, int v) =>
        Check(((delegate* unmanaged[Stdcall]<IntPtr, Guid*, int, int>)Slot(a, 21))(a, &key, v), "SetUINT32");
    private static void SetGUID(IntPtr a, Guid key, Guid v) =>
        Check(((delegate* unmanaged[Stdcall]<IntPtr, Guid*, Guid*, int>)Slot(a, 24))(a, &key, &v), "SetGUID");

    private static IntPtr NewType(Guid subtype)
    {
        Check(MFCreateMediaType(out var t), "MFCreateMediaType");

        SetGUID(t, MT_MAJOR_TYPE, MEDIA_TYPE_AUDIO);
        SetGUID(t, MT_SUBTYPE, subtype);
        return t;
    }

    private static void WithMf(Action body)
    {
        if (!OperatingSystem.IsWindows())
            throw new PlatformNotSupportedException("Media Foundation needs Windows.");

        int hr;
        try
        {
            hr = MFStartup(MF_VERSION, 0);
        }
        catch (DllNotFoundException)
        {
            throw new InvalidOperationException("Windows Media Foundation is missing.");
        }

        Check(hr, "MFStartup");

        try
        {
            body();
        }
        finally
        {
            MFShutdown();
        }
    }

    // Decode.

    /// <summary>Decode any audio file Media Foundation can open to 16 bit PCM.</summary>
    public static WavFile Decode(string path)
    {
        WavFile? result = null;
        WithMf(() =>
        {
            IntPtr reader = IntPtr.Zero, partial = IntPtr.Zero, current = IntPtr.Zero;

            try
            {
                Check(MFCreateSourceReaderFromURL(Path.GetFullPath(path), IntPtr.Zero, out reader), $"opening {Path.GetFileName(path)}");

                var set_sel = (delegate* unmanaged[Stdcall]<IntPtr, int, int, int>)Slot(reader, 4);
                set_sel(reader, SOURCE_READER_ALL_STREAMS, 0);

                Check(set_sel(reader, SOURCE_READER_FIRST_AUDIO, 1), "selecting the audio stream");
                partial = NewType(AUDIO_FORMAT_PCM);

                SetUINT32(partial, MT_AUDIO_BITS_PER_SAMPLE, 16);
                Check(((delegate* unmanaged[Stdcall]<IntPtr, int, IntPtr, IntPtr, int>)Slot(reader, 7))(reader, SOURCE_READER_FIRST_AUDIO, IntPtr.Zero, partial),
                    "requesting PCM output");

                IntPtr cur;
                Check(((delegate* unmanaged[Stdcall]<IntPtr, int, IntPtr*, int>)Slot(reader, 6))(reader, SOURCE_READER_FIRST_AUDIO, &cur), "GetCurrentMediaType");
                current = cur;
                GetUINT32(current, MT_AUDIO_NUM_CHANNELS, out int ch);
                GetUINT32(current, MT_AUDIO_SAMPLES_PER_SECOND, out int rate);

                var read_sample = (delegate* unmanaged[Stdcall]<IntPtr, int, int, int*, int*, long*, IntPtr*, int>)Slot(reader, 9);
                using var ms = new MemoryStream();

                while (true)
                {
                    int idx, flags;
                    long ts;
                    IntPtr sample;
                    Check(read_sample(reader, SOURCE_READER_FIRST_AUDIO, 0, &idx, &flags, &ts, &sample), "ReadSample");

                    if (sample != IntPtr.Zero)
                    {
                        IntPtr buf;
                        Check(((delegate* unmanaged[Stdcall]<IntPtr, IntPtr*, int>)Slot(sample, 41))(sample, &buf), "ConvertToContiguousBuffer");

                        byte* data;
                        int max, len;
                        Check(((delegate* unmanaged[Stdcall]<IntPtr, byte**, int*, int*, int>)Slot(buf, 3))(buf, &data, &max, &len), "Lock");

                        ms.Write(new ReadOnlySpan<byte>(data, len));
                        ((delegate* unmanaged[Stdcall]<IntPtr, int>)Slot(buf, 4))(buf);

                        Release(buf);
                        Release(sample);
                    }
                    if ((flags & READERF_ENDOFSTREAM) != 0)
                        break;
                }

                var raw = ms.ToArray();
                var s = new short[raw.Length / 2];
                Buffer.BlockCopy(raw, 0, s, 0, s.Length * 2);
                result = new WavFile { SampleRate = rate, Channels = Math.Max(1, ch), Samples = s };
            }
            finally
            {
                Release(current);
                Release(partial);
                Release(reader);
            }
        });

        return result!;
    }

    // Encode.

    /// <summary>Encode to MP3 with the Windows MP3 encoder (stereo, 32, 44.1, 48 kHz, other inputs are converted first).</summary>
    public static void EncodeMp3(WavFile wav, string path, int kbps = 192)
    {
        var w = wav.ToStereo();

        if (w.Channels == 1)
            w = new WavFile
            {
                SampleRate = w.SampleRate, Channels = 2, Samples = Duplicate(w.Samples)
            };

        if (w.SampleRate is not (32000 or 44100 or 48000))
            w = Resample(w, w.SampleRate < 40000 ? 44100 : 48000);

        kbps = kbps switch
        { 
            <= 128 => 128,
            <= 160 => 160,
            <= 192 => 192,
            <= 224 => 224,
            <= 256 => 256,
            _ => 320 
        };

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);

        if (File.Exists(path))
            File.Delete(path);

        WithMf(() =>
        {
            IntPtr writer = IntPtr.Zero, out_type = IntPtr.Zero, in_type = IntPtr.Zero;

            try
            {
                Check(MFCreateSinkWriterFromURL(Path.GetFullPath(path), IntPtr.Zero, IntPtr.Zero, out writer), "creating the MP3 file");
                out_type = NewType(AUDIO_FORMAT_MP3);

                SetUINT32(out_type, MT_AUDIO_NUM_CHANNELS, 2);
                SetUINT32(out_type, MT_AUDIO_SAMPLES_PER_SECOND, w.SampleRate);
                SetUINT32(out_type, MT_AUDIO_AVG_BYTES_PER_SECOND, kbps * 1000 / 8);

                int stream;
                Check(((delegate* unmanaged[Stdcall]<IntPtr, IntPtr, int*, int>)Slot(writer, 3))(writer, out_type, &stream),
                    "adding the MP3 stream (is the Windows MP3 encoder available?)");

                in_type = NewType(AUDIO_FORMAT_PCM);
                SetUINT32(in_type, MT_AUDIO_NUM_CHANNELS, 2);
                SetUINT32(in_type, MT_AUDIO_SAMPLES_PER_SECOND, w.SampleRate);
                SetUINT32(in_type, MT_AUDIO_BITS_PER_SAMPLE, 16);
                SetUINT32(in_type, MT_AUDIO_BLOCK_ALIGNMENT, 4);
                SetUINT32(in_type, MT_AUDIO_AVG_BYTES_PER_SECOND, w.SampleRate * 4);
                SetUINT32(in_type, MT_ALL_SAMPLES_INDEPENDENT, 1);

                Check(((delegate* unmanaged[Stdcall]<IntPtr, int, IntPtr, IntPtr, int>)Slot(writer, 4))(writer, stream, in_type, IntPtr.Zero), "SetInputMediaType");
                Check(((delegate* unmanaged[Stdcall]<IntPtr, int>)Slot(writer, 5))(writer), "BeginWriting");

                var write_sample = (delegate* unmanaged[Stdcall]<IntPtr, int, IntPtr, int>)Slot(writer, 6);
                int frames_per_chunk = w.SampleRate / 4;
                long frames = w.Frames;

                for (long f = 0; f < frames; f += frames_per_chunk)
                {
                    int n = (int)Math.Min(frames_per_chunk, frames - f);
                    int bytes = n * 4;

                    Check(MFCreateMemoryBuffer(bytes, out var buf), "MFCreateMemoryBuffer");
                    Check(MFCreateSample(out var sample), "MFCreateSample");

                    try
                    {
                        byte* data;
                        int max, len;
                        Check(((delegate* unmanaged[Stdcall]<IntPtr, byte**, int*, int*, int>)Slot(buf, 3))(buf, &data, &max, &len), "Lock");

                        fixed (short* src = &w.Samples[f * 2]) Buffer.MemoryCopy(src, data, max, bytes);
                        ((delegate* unmanaged[Stdcall]<IntPtr, int>)Slot(buf, 4))(buf);
                        Check(((delegate* unmanaged[Stdcall]<IntPtr, int, int>)Slot(buf, 6))(buf, bytes), "SetCurrentLength");
                        Check(((delegate* unmanaged[Stdcall]<IntPtr, IntPtr, int>)Slot(sample, 42))(sample, buf), "AddBuffer");

                        long t = f * 10_000_000L / w.SampleRate, d = n * 10_000_000L / w.SampleRate;
                        Check(((delegate* unmanaged[Stdcall]<IntPtr, long, int>)Slot(sample, 36))(sample, t), "SetSampleTime");
                        Check(((delegate* unmanaged[Stdcall]<IntPtr, long, int>)Slot(sample, 38))(sample, d), "SetSampleDuration");
                        Check(write_sample(writer, stream, sample), "WriteSample");
                    }
                    finally
                    {
                        Release(sample);
                        Release(buf);
                    }
                }

                Check(((delegate* unmanaged[Stdcall]<IntPtr, int>)Slot(writer, 11))(writer), "Finalize");
            }
            finally
            {
                Release(in_type);
                Release(out_type);
                Release(writer);
            }
        });
    }

    private static short[] Duplicate(short[] mono)
    {
        var o = new short[mono.Length * 2];

        for (int i = 0; i < mono.Length; i++) 
            o[i * 2] = o[i * 2 + 1] = mono[i];

        return o;
    }

    /// <summary>Linear resampling, good enough for previews and voice lines.</summary>
    public static WavFile Resample(WavFile w, int rate)
    {
        if (w.SampleRate == rate)
            return w;

        long in_frames = w.Frames, out_frames = in_frames * rate / w.SampleRate;
        var o = new short[out_frames * w.Channels];
        double step = w.SampleRate / (double)rate;

        for (long f = 0; f < out_frames; f++)
        {
            double pos = f * step;
            long i0 = (long)pos;
            long i1 = Math.Min(i0 + 1, in_frames - 1);
            double t = pos - i0;

            for (int c = 0; c < w.Channels; c++)
                o[f * w.Channels + c] = (short)Math.Round(w.Samples[i0 * w.Channels + c] * (1 - t) + w.Samples[i1 * w.Channels + c] * t);
        }

        return new WavFile { SampleRate = rate, Channels = w.Channels, Samples = o };
    }
}