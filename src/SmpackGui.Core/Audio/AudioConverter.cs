using SmpackGui.Core.Cli;

namespace SmpackGui.Core.Audio;

public enum AudioExportFormat { MP3, WAV, WEM }

/// <summary>Bridge between the audio tools. WEM to WAV or MP3 for listening, and audio files to WEM for mods.</summary>
public sealed class AudioConverter(Vgmstream vgm, WwiseConverter wwise, string cache_root)
{
    public Vgmstream Vgmstream { get; } = vgm;
    public WwiseConverter Wwise { get; } = wwise;
    public string CacheRoot
    {
        get;
        set;
    } = cache_root;
    public int Mp3Kbps
    {
        get;
        set;
    } = 320;

    public static readonly string[] ImportExtensions = [
        ".mp3", 
        ".wav", 
        ".ogg", 
        ".flac", 
        ".m4a", 
        ".aac", 
        ".wma",
        ".wem"];

    /// <summary>Decoded, stereo preview WAV for a WEM, cached by key.</summary>
    public async Task<string> PreviewWavAsync(string key, Func<byte[]> wem, CancellationToken ct = default)
    {
        var dir = Path.Combine(CacheRoot, "audio");
        Directory.CreateDirectory(dir);

        var wav = Path.Combine(dir, key + ".wav");

        if (File.Exists(wav))
            return wav;

        var src = Path.Combine(dir, key + ".wem");
        await File.WriteAllBytesAsync(src, wem(), ct).ConfigureAwait(false);

        var raw = Path.Combine(dir, key + ".raw.wav");
        try
        {
            await Vgmstream.DecodeAsync(src, raw, ct).ConfigureAwait(false);

            await Task.Run(() =>
            {
                var w = WavFile.Load(raw);
                w.ToStereo().Save(wav + ".tmp");
                File.Move(wav + ".tmp", wav, overwrite: true);
            }, ct).ConfigureAwait(false);
        }
        finally
        {
            TryDelete(src);
            TryDelete(raw);
        }

        return wav;
    }

    /// <summary>Write a WEM as MP3, WAV with original channel layout or the raw WEM.</summary>
    public async Task ExportAsync(byte[] wem, string output, AudioExportFormat fmt, CancellationToken ct = default)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output))!);

        if (fmt == AudioExportFormat.WEM)
        {
            await File.WriteAllBytesAsync(output, wem, ct).ConfigureAwait(false);
            return;
        }

        var tmp = Path.Combine(Path.GetTempPath(), "SmpackGui", "audio", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tmp);

        try
        {
            var src = Path.Combine(tmp, "in.wem");
            await File.WriteAllBytesAsync(src, wem, ct).ConfigureAwait(false);

            var wav = fmt == AudioExportFormat.WAV ? output : Path.Combine(tmp, "out.wav");
            await Vgmstream.DecodeAsync(src, wav, ct).ConfigureAwait(false);

            if (fmt == AudioExportFormat.MP3)
                await Task.Run(() => MediaFoundation.EncodeMp3(WavFile.Load(wav), output, Mp3Kbps), ct).ConfigureAwait(false);
        }
        finally
        {
            try
            {
                Directory.Delete(tmp, true);
            } catch
            {}
        }
    }

    /// <summary>Any supported audio file to WEM (Vorbis) for the game. A WEM is copied as is.</summary>
    public async Task ToWemAsync(string input, string wem_out, int? target_rate, CancellationToken ct = default)
    {
        if (input.EndsWith(".wem", StringComparison.OrdinalIgnoreCase))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(wem_out))!);
            File.Copy(input, wem_out, overwrite: true);
            return;
        }

        if (!Wwise.Available)
            throw new InvalidOperationException("Converting to .wem needs Wwise (WwiseConsole.exe). Install Wwise or set its path in 'Settings', or pick a ready .wem file.");

        var tmp = Path.Combine(Path.GetTempPath(), "SmpackGui", "audio", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(tmp);

        try
        {
            var wav = Path.Combine(tmp, "source.wav");

            await Task.Run(() =>
            {
                WavFile w;

                try
                {
                    w = input.EndsWith(".wav", StringComparison.OrdinalIgnoreCase) ? WavFile.Load(input) : MediaFoundation.Decode(input);
                }
                catch (NotSupportedException)
                {
                    w = MediaFoundation.Decode(input);
                }

                if (target_rate is > 0 && w.SampleRate != target_rate)
                    w = MediaFoundation.Resample(w, target_rate.Value);

                w.Save(wav);
            }, ct).ConfigureAwait(false);

            await Wwise.ConvertAsync(wav, wem_out, Path.Combine(CacheRoot, "wwise"), ct).ConfigureAwait(false);
        }
        finally
        {
            try
            {
                Directory.Delete(tmp, true);
            } catch
            {}
        }
    }

    private static void TryDelete(string p)
    {
        try
        {
            if (File.Exists(p))
                File.Delete(p);
        } catch
        {}
    }
}