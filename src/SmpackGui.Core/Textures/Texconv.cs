using SmpackGui.Core.Cli;
using SmpackGui.Core.Imaging;

namespace SmpackGui.Core.Textures;

public enum BcQuality
{
    FAST,
    BALANCED,
    BEST
}

/// <summary>
/// Wrapper around Microsoft's texconv (DirectXTex, MIT) for turning PNG, TGA, JPG, EXR, DDS
/// into the exact BCn format plus full mip chain the game texture uses.
/// </summary>
public sealed class Texconv(IActivityLog? log)
{
    public const string DownloadUrl = "https://github.com/microsoft/DirectXTex/releases/latest/download/texconv.exe";
    public const string ProjectUrl = "https://github.com/microsoft/DirectXTex/wiki/Texconv";

    private readonly ToolRunner m_Runner = new(log);

    public string? ExePath
    {
        get;
        set;
    }
    public bool Available => ExePath != null && File.Exists(ExePath);

    /// <summary>Look next to the app, in the tools folder, then on 'PATH'.</summary>
    public static string? Locate(string? configured, string tools_dir)
    {
        if (!string.IsNullOrWhiteSpace(configured) && File.Exists(configured))
            return configured;

        foreach (var dir in new[] { AppContext.BaseDirectory, tools_dir })
        {
            var p = Path.Combine(dir, "texconv.exe");

            if (File.Exists(p))
                return p;
        }

        foreach (var dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
        {
            try
            {
                var p = Path.Combine(dir.Trim(), "texconv.exe");

                if (File.Exists(p))
                    return p;
            }
            catch (ArgumentException)
            {
                // Bad 'PATH' entry.
            }
        }

        return null;
    }

    public static async Task<string> DownloadAsync(string tools_dir, IProgress<double>? progress = null, CancellationToken ct = default)
    {
        Directory.CreateDirectory(tools_dir);

        var dst = Path.Combine(tools_dir, "texconv.exe");
        using var http = new HttpClient();
        http.DefaultRequestHeaders.UserAgent.ParseAdd("SmpackGui/1.0");
        using var resp = await http.GetAsync(DownloadUrl, HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
        resp.EnsureSuccessStatusCode();
        long total = resp.Content.Headers.ContentLength ?? -1, done = 0;
        var tmp = dst + ".download";

        await using (var src = await resp.Content.ReadAsStreamAsync(ct).ConfigureAwait(false))
        await using (var fs = File.Create(tmp))
        {
            var buf = new byte[81920];
            int n;
            while ((n = await src.ReadAsync(buf, ct).ConfigureAwait(false)) > 0)
            {
                await fs.WriteAsync(buf.AsMemory(0, n), ct).ConfigureAwait(false);
                done += n;
                if (total > 0) progress?.Report(done / (double)total);
            }
        }

        // Sanity check. A PE image.
        var head = new byte[2];
        await using (var fs = File.OpenRead(tmp)) fs.ReadExactly(head);
        if (head[0] != 'M' || head[1] != 'Z')
        {
            File.Delete(tmp);
            throw new InvalidDataException("Downloaded file is not an executable.");
        }

        File.Move(tmp, dst, overwrite: true);
        return dst;
    }

    /// <summary>
    /// Convert <paramref name="input"/> to <paramref name="output_dds"/> in format <paramref name="dxgi"/>
    /// with a full mip chain, resized to width x height.
    /// </summary>
    public async Task ConvertAsync(string input, string output_dds, int dxgi, int width, int height, BcQuality quality,
        CancellationToken ct = default)
    {
        if (!Available)
            throw new InvalidOperationException("texconv.exe is not available. Download it in 'Settings' -> 'Tools'.");

        var out_dir = Path.Combine(Path.GetTempPath(), "SmpackGui", "texconv", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(out_dir);

        try
        {
            var fmt = DxgiFormats.Name(dxgi);

            // Normalise the output name. texconv writes '<input stem>.dds', copy the input to a neutral name first.
            var ext = Path.GetExtension(input);
            var staged = Path.Combine(out_dir, "in" + ext);
            File.Copy(input, staged);

            var args = new List<string>
            {
                "-nologo", "-y", "-f", fmt, "-m", "0", "-w", width.ToString(), "-h", height.ToString(),
                "-dx10", "-if", "FANT", "-o", Path.Combine(out_dir, "out"),
            };

            // Keep color values as authored. sRGB textures stay sRGB, data textures stay linear.
            if (DxgiFormats.IsSrgb(dxgi))
                args.Add("-srgb");

            if (DxgiFormats.IsBlockCompressed(dxgi))
            {
                switch (quality)
                {
                    case BcQuality.FAST:
                        args.AddRange(["-bc", "q"]);
                        break;

                    case BcQuality.BEST:
                        args.AddRange(["-bc", "x"]);
                        break;
                }
            }

            args.Add(staged);
            Directory.CreateDirectory(Path.Combine(out_dir, "out"));

            var r = await m_Runner.RunAsync(ExePath!, args, out_dir, ct: ct).ConfigureAwait(false);
            var produced = Path.Combine(out_dir, "out", "in.dds");

            if (!r.Success || !File.Exists(produced))
                throw new SmpackException($"texconv failed: {r.StdOut.Split('\n').LastOrDefault(l => l.Contains("FAILED") || l.Contains("ERROR"))?.Trim() ?? r.ErrorMessage}", r);

            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(output_dds))!);
            File.Copy(produced, output_dds, overwrite: true);
        }
        finally
        {
            try
            {
                Directory.Delete(out_dir, true);
            }
            catch
            {}
        }
    }
}