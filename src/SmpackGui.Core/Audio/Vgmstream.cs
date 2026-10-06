using System.Globalization;
using System.IO.Compression;
using System.Text.RegularExpressions;
using SmpackGui.Core.Cli;

namespace SmpackGui.Core.Audio;

public sealed record AudioInfo(int SampleRate, int Channels, long Samples, string Encoding)
{
    public TimeSpan Duration => SampleRate > 0 ? TimeSpan.FromSeconds(Samples / (double)SampleRate) : TimeSpan.Zero;
}

/// <summary>vgmstream-cli [ISC licence]. Decodes Wwise WEM (Vorbis, Opus, ADPCM, PCM) to WAV.</summary>
public sealed class Vgmstream(IActivityLog? log)
{
    public const string DownloadUrl = "https://github.com/vgmstream/vgmstream/releases/latest/download/vgmstream-win64.zip";
    public const string ProjectUrl = "https://vgmstream.org";

    private readonly ToolRunner m_Runner = new(log);

    public string? ExePath
    {
        get;
        set;
    }
    public bool Available => ExePath != null && File.Exists(ExePath);

    public static string? Locate(string? configured, string tools_dir)
    {
        if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured))
            return configured;

        foreach (var dir in new[] { Path.Combine(tools_dir, "vgmstream"), AppContext.BaseDirectory, Path.Combine(AppContext.BaseDirectory, "tools", "vgmstream") })
        {
            var p = Path.Combine(dir, "vgmstream-cli.exe");

            if (File.Exists(p))
                return p;
        }

        return null;
    }

    /// <summary>Download the official Windows build.</summary>
    public static async Task<string> DownloadAsync(string tools_dir, IProgress<double>? progress = null, CancellationToken ct = default)
    {
        var dir = Path.Combine(tools_dir, "vgmstream");
        Directory.CreateDirectory(dir);

        var zip = Path.Combine(dir, "vgmstream-win64.zip");
        using (var http = new HttpClient())
        {
            http.DefaultRequestHeaders.UserAgent.ParseAdd("SmpackGui/1.0");

            using var resp = await http.GetAsync(DownloadUrl, HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
            resp.EnsureSuccessStatusCode();

            long total = resp.Content.Headers.ContentLength ?? -1, done = 0;
            await using var src = await resp.Content.ReadAsStreamAsync(ct).ConfigureAwait(false);
            await using var fs = File.Create(zip);
            var buf = new byte[81920];
            int n;

            while ((n = await src.ReadAsync(buf, ct).ConfigureAwait(false)) > 0)
            {
                await fs.WriteAsync(buf.AsMemory(0, n), ct).ConfigureAwait(false);
                done += n;

                if (total > 0)
                    progress?.Report(done / (double)total);
            }
        }

        ZipFile.ExtractToDirectory(zip, dir, overwriteFiles: true);
        File.Delete(zip);

        var exe = Path.Combine(dir, "vgmstream-cli.exe");

        if (!File.Exists(exe))
            throw new InvalidDataException("The download did not contain vgmstream-cli.exe.");

        return exe;
    }

    private void Require()
    {
        if (!Available)
            throw new InvalidOperationException("vgmstream is not available. Download it in 'Settings' -> 'Audio tools'.");
    }

    public async Task<AudioInfo> InfoAsync(string file, CancellationToken ct = default)
    {
        Require();

        var r = await m_Runner.RunAsync(ExePath!, ["-m", file], echo_stdout: false, ct: ct).ConfigureAwait(false);

        if (!r.Success)
            throw new SmpackException($"vgmstream can't read {Path.GetFileName(file)}: {r.ErrorMessage}", r);

        int Int(string pat) => Regex.Match(r.StdOut, pat) is { Success: true } m ? int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture) : 0;
        long samples = Regex.Match(r.StdOut, @"stream total samples:\s*(\d+)") is { Success: true } s ? long.Parse(s.Groups[1].Value, CultureInfo.InvariantCulture) : 0;
        var enc = Regex.Match(r.StdOut, @"encoding:\s*(.+)") is { Success: true } e ? e.Groups[1].Value.Trim() : "";
        return new AudioInfo(Int(@"sample rate:\s*(\d+)"), Int(@"channels:\s*(\d+)"), samples, enc);
    }

    /// <summary>Decode to 16 bit PCM WAV. Loops are played once.</summary>
    public async Task DecodeAsync(string input, string wav, CancellationToken ct = default)
    {
        Require();

        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(wav))!);
        var r = await m_Runner.RunAsync(ExePath!, ["-o", wav, input], echo_stdout: false, ct: ct).ConfigureAwait(false);

        if (!r.Success || !File.Exists(wav))
            throw new SmpackException($"vgmstream could not decode {Path.GetFileName(input)}: {r.ErrorMessage}", r);
    }
}