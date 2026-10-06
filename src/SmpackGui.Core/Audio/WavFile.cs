namespace SmpackGui.Core.Audio;

/// <summary>16 bit PCM WAV in memory.</summary>
public sealed class WavFile
{
    public int SampleRate 
    {
        get;
        init;
    }
    public int Channels
    {
        get;
        init;
    }
    public short[] Samples
    {
        get;
        init;
    } = [];

    public long Frames => Channels == 0 ? 0 : Samples.Length / Channels;
    public TimeSpan Duration => SampleRate == 0 ? TimeSpan.Zero : TimeSpan.FromSeconds(Frames / (double)SampleRate);

    public static WavFile Load(string path)
    {
        using var br = new BinaryReader(File.OpenRead(path));

        if (new string(br.ReadChars(4)) != "RIFF")
            throw new InvalidDataException("Not a WAV file");

        br.ReadInt32();

        if (new string(br.ReadChars(4)) != "WAVE")
            throw new InvalidDataException("Not a WAV file");

        int rate = 0, ch = 0, bits = 0, fmt = 0;
        while (br.BaseStream.Position + 8 <= br.BaseStream.Length)
        {
            var id = new string(br.ReadChars(4));
            int size = br.ReadInt32();
            long next = br.BaseStream.Position + size + (size & 1);

            if (id == "fmt ")
            {
                fmt = br.ReadUInt16();
                ch = br.ReadUInt16();
                rate = br.ReadInt32();
                br.ReadInt32();
                br.ReadUInt16();
                bits = br.ReadUInt16();

                if (fmt == 0xFFFE && size >= 40)
                {
                    br.ReadBytes(8);
                    fmt = br.ReadUInt16(); // Sub format GUID starts with the format tag.
                }
            }
            else if (id == "data")
            {
                if (ch == 0)
                    throw new InvalidDataException("WAV has no fmt chunk");

                var raw = br.ReadBytes((int)Math.Min(size, br.BaseStream.Length - br.BaseStream.Position));
                short[] s;

                if (fmt == 1 && bits == 16)
                {
                    s = new short[raw.Length / 2];
                    Buffer.BlockCopy(raw, 0, s, 0, s.Length * 2);
                }
                else if (fmt == 3 && bits == 32)
                {
                    s = new short[raw.Length / 4];
                    for (int i = 0; i < s.Length; i++)
                        s[i] = (short)Math.Clamp(BitConverter.ToSingle(raw, i * 4) * 32767f, -32768f, 32767f);
                }
                else if (fmt == 1 && bits == 24)
                {
                    s = new short[raw.Length / 3];
                    for (int i = 0; i < s.Length; i++)
                        s[i] = (short)(raw[i * 3 + 1] | raw[i * 3 + 2] << 8);
                }
                else
                    throw new NotSupportedException($"WAV format {fmt}/{bits} bit is not supported (use 16 bit PCM)");

                return new WavFile { SampleRate = rate, Channels = ch, Samples = s };
            }

            br.BaseStream.Position = next;
        }

        throw new InvalidDataException("WAV has no data chunk");
    }

    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);

        using var bw = new BinaryWriter(File.Create(path));
        int bytes = Samples.Length * 2;

        bw.Write("RIFF"u8);
        bw.Write(36 + bytes);
        bw.Write("WAVEfmt "u8);
        bw.Write(16);
        bw.Write((ushort)1);
        bw.Write((ushort)Channels);
        bw.Write(SampleRate);
        bw.Write(SampleRate * Channels * 2);
        bw.Write((ushort)(Channels * 2));
        bw.Write((ushort)16);
        bw.Write("data"u8);
        bw.Write(bytes);
        var raw = new byte[bytes];
        Buffer.BlockCopy(Samples, 0, raw, 0, bytes);
        bw.Write(raw);
    }

    /// <summary>Fold surround layouts to stereo, mono and stereo stay as they are.</summary>
    public WavFile ToStereo()
    {
        if (Channels <= 2)
            return this;

        long frames = Frames;
        var o = new short[frames * 2];
        const float c = 0.7071f;

        float norm = Channels switch
        {
            3 => 1 / (1 + c),
            4 => 1 / (1 + c),
            _ => 1 / (1 + 2 * c)
        };

        for (long f = 0; f < frames; f++)
        {
            long b = f * Channels;
            float l = Samples[b], r = Samples[b + 1];

            switch (Channels)
            {
                case 3:
                    l += c * Samples[b + 2];
                    r += c * Samples[b + 2];
                    break;

                case 4:
                    l += c * Samples[b + 2];
                    r += c * Samples[b + 3];
                    break;

                default:
                    // 5.1. L R C LFE Ls Rs.
                    // 7.1. L R C LFE Lb Rb Ls Rs. Extra pairs folded into the rears.
                    l += c * Samples[b + 2];
                    r += c * Samples[b + 2];

                    for (int k = 4; k + 1 < Channels; k += 2)
                    {
                        l += c * Samples[b + k];
                        r += c * Samples[b + k + 1];
                    }

                    break;
            }

            o[f * 2] = (short)Math.Clamp(l * norm, -32768f, 32767f);
            o[f * 2 + 1] = (short)Math.Clamp(r * norm, -32768f, 32767f);
        }

        return new WavFile
        { 
            SampleRate = SampleRate,
            Channels = 2, 
            Samples = o
        };
    }
}