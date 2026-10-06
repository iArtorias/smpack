using System.Collections.Concurrent;
using SmpackGui.Core.Cli;
using SmpackGui.Core.Game;
using SmpackGui.Core.Models;

namespace SmpackGui.Core.Textures;

public sealed record ExportedTexture(string DdsPath, string SidecarPath)
{
    public Sidecar LoadSidecar() => Sidecar.Load(SidecarPath);
    public string Stem => Path.GetFileNameWithoutExtension(DdsPath);
}

/// <summary>
/// Pulls original textures out of the game via smpack into a local cache.
/// Always reads pristine files. If a WAD was modded, its '*.smpack-orig' backup is used.
/// </summary>
public sealed class TextureExporter(SmpackCli cli, Func<GameInstall?> game, string cache_root)
{
    private readonly ConcurrentDictionary<string, SemaphoreSlim> m_Locks = new();

    public string CacheRoot 
    { 
        get;
        set;
    } = cache_root;

    public string SourcePathFor(TextureItem item, string? wad = null)
    {
        var g = game() ?? throw new InvalidOperationException("No game folder selected.");

        if (item.Texpack != null && wad == null)
            return Path.Combine(g.WadDir, item.Texpack + ".texpack");

        wad ??= item.Wads.FirstOrDefault() ?? throw new InvalidOperationException($"{item.Name}: no WAD references this texture.");
        return GameInstall.Pristine(Path.Combine(g.WadDir, wad));
    }

    private string CacheDirFor(TextureItem item, string? wad)
    {
        string key = item.Texpack != null && wad == null
            ? $"tp_{item.ContentHash:x16}"
            : $"wad_{GameInstall.PackBase(wad ?? item.Wads.FirstOrDefault() ?? "x")}_{item.ContentHash:x16}_{SafeName(item.Name)}";

        return Path.Combine(CacheRoot, "textures", key);
    }

    /// <summary>
    /// Export the original texture. Full resolution for streamed textures, the WAD copy when <paramref name="wad"/> is given
    /// or the texture only lives in a WAD.
    /// </summary>
    public async Task<ExportedTexture> ExportAsync(TextureItem item, string? wad = null, CancellationToken ct = default)
    {
        var dir = CacheDirFor(item, wad);
        var gate = m_Locks.GetOrAdd(dir, _ => new SemaphoreSlim(1, 1));
        await gate.WaitAsync(ct).ConfigureAwait(false);

        try
        {
            var src = SourcePathFor(item, wad);

            if (!File.Exists(src))
                throw new FileNotFoundException($"Game file not found: {src}");

            var cached = FindPair(dir);

            if (cached != null && File.GetLastWriteTimeUtc(cached.DdsPath) >= File.GetLastWriteTimeUtc(src))
                return cached;

            var tmp = dir + ".part";
            if (Directory.Exists(tmp))
                Directory.Delete(tmp, true);

            Directory.CreateDirectory(tmp);

            List<string> args;
            bool from_wad = !(item.Texpack != null && wad == null);

            if (!from_wad)
                args = ["tex", "export", src, "--id", Json.Hex(item.ContentHash), "-o", tmp];
            else
                args = ["tex", "export", src, "--name", item.Name, "-o", tmp];

            var r = await cli.RunAsync(args, echo_stdout: false, ct: ct).ConfigureAwait(false);
            r.ThrowIfFailed();
            var pair = FindPair(tmp) ?? throw new SmpackException(ExportFailure(item.Name, r), r);

            if (from_wad)
            {
                // Exported from a '*.smpack-orig' backup? Record the real WAD name so import finds it.
                var sc = pair.LoadSidecar();
                var real = wad ?? item.Wads[0];

                if (!string.Equals(sc.Wad, real, StringComparison.OrdinalIgnoreCase))
                {
                    sc.Wad = real;
                    sc.Save(pair.SidecarPath);
                }
            }

            if (Directory.Exists(dir))
                Directory.Delete(dir, true);

            Directory.Move(tmp, dir);
            return FindPair(dir)!;
        }
        finally
        {
            gate.Release();
        }
    }

    private static string ExportFailure(string name, CliResult r)
    {
        // smpack reports per texture problems as '  ! <name>: <reason>'.
        var line = r.StdErr.Split('\n').Select(l => l.Trim()).FirstOrDefault(l => l.StartsWith('!'));

        if (line == null)
            return $"smpack exported nothing for {name}.";

        var reason = line.TrimStart('!', ' ');

        if (reason.StartsWith(name + ":", StringComparison.OrdinalIgnoreCase))
            reason = reason[(name.Length + 1)..].Trim();

        return $"Can't read {name}: {reason}";
    }

    public static string SafeName(string s)
    {
        var bad = Path.GetInvalidFileNameChars();
        var chars = s.Select(c => bad.Contains(c) || c == ' ' ? '_' : c).ToArray();
        var r = new string(chars);
        return r.Length > 80 ? r[..80] : r;
    }

    private static ExportedTexture? FindPair(string dir)
    {
        if (!Directory.Exists(dir))
            return null;

        foreach (var dds in Directory.EnumerateFiles(dir, "*.dds"))
        {
            var js = Path.ChangeExtension(dds, ".json");

            if (File.Exists(js))
                return new ExportedTexture(dds, js);
        }

        return null;
    }

    /// <summary>Preview cache folders. Decoded textures, model previews and audio previews.</summary>
    private IEnumerable<string> CacheFolders() =>
        new[] { "textures", "models", "audio" }.Select(d => Path.Combine(CacheRoot, d)).Where(Directory.Exists);

    public long CacheSize()
    {
        long total = 0;
        foreach (var d in CacheFolders())
            foreach (var f in new DirectoryInfo(d).EnumerateFiles("*", SearchOption.AllDirectories))
            {
                try
                {
                    total += f.Length;
                }
                catch (IOException)
                {
                    // Removed while counting.
                }
            }

        return total;
    }

    /// <summary>Delete the preview caches. The texture index is kept.</summary>
    public void ClearCache()
    {
        foreach (var d in CacheFolders().ToList())
            Directory.Delete(d, true);

        m_Locks.Clear();
    }
}