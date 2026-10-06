using SmpackGui.Core.Cli;
using SmpackGui.Core.Imaging;
using SmpackGui.Core.Models;
using SmpackGui.Core.Textures;

namespace SmpackGui.Core.Mods;

public sealed class ReplaceRequest
{
    public required TextureItem Texture
    {
        get;
        init;
    }
    public required string SourceFile
    {
        get;
        init;
    }
    /// <summary>Target size.</summary>
    public int Width
    {
        get;
        init;
    }
    public int Height
    {
        get;
        init;
    }
    public BcQuality Quality
    {
        get;
        init;
    } = BcQuality.BALANCED;
    /// <summary>For WAD resident textures referenced by several WADs. Patch every copy.</summary>
    public bool AllWadCopies
    {
        get;
        init;
    } = true;
}

public sealed record ReplaceResult(List<ModTextureEntry> Entries, List<string> Warnings);

/// <summary>Turns a user image into project DDS files that 'smpack tex import' accepts.</summary>
public sealed class TextureReplacer(TextureExporter exporter, Texconv texconv, IActivityLog? log)
{
    public static readonly string[] ImageExtensions = [
        ".png",
        ".tga",
        ".jpg",
        ".jpeg",
        ".bmp",
        ".tif",
        ".tiff",
        ".hdr",
        ".exr",
        ".dds",
        ".wdp",
        ".jxr"];

    public async Task<ReplaceResult> ReplaceAsync(ModProject project, ReplaceRequest req, CancellationToken ct = default)
    {
        var item = req.Texture;
        var warnings = new List<string>();
        var targets = new List<(ExportedTexture Orig, string? Wad)>();

        if (item.Texpack != null)
        {
            targets.Add((await exporter.ExportAsync(item, null, ct).ConfigureAwait(false), null));
        }
        else
        {
            var wads = req.AllWadCopies ? item.Wads : [.. item.Wads.Take(1)];

            foreach (var w in wads)
                targets.Add((await exporter.ExportAsync(item, w, ct).ConfigureAwait(false), w));
        }

        var entries = new List<ModTextureEntry>();
        string? first_dds = null;

        foreach (var (orig, wad) in targets)
        {
            var sc = orig.LoadSidecar();
            int dxgi = sc.Dxgi != 0 ? sc.Dxgi : DxgiFormats.FromAgcName(sc.AgcFormat);

            // Streamed texture whose texpack we don't know. Only the WAD's low mip copy can change, at the same size.
            bool fixed_size = sc.IsWad && sc.Streamed;
            int w = fixed_size || req.Width <= 0 ? sc.Width : req.Width;
            int h = fixed_size || req.Height <= 0 ? sc.Height : req.Height;

            if (fixed_size && (req.Width > 0 && (req.Width != sc.Width || req.Height != sc.Height)))
                warnings.Add($"{item.Name}: only the {sc.Width}x{sc.Height} WAD copy is available, so the size is kept.");

            if (sc.IsTexpack && (long)w * sc.Height != (long)h * sc.Width)
                warnings.Add($"{item.Name}: aspect ratio differs from the original {sc.Width}x{sc.Height}. UVs may stretch.");

            var base_stem = TextureExporter.SafeName(item.Name);
            var stem = wad == null ? base_stem : $"{base_stem}@{GameInstallName(wad)}";
            var dst_dds = Path.Combine(project.TexturesDir, stem + ".dds");
            var dst_json = Path.Combine(project.TexturesDir, stem + ".json");

            Directory.CreateDirectory(project.TexturesDir);

            if (first_dds != null && new FileInfo(first_dds).Length > 0 && SameTarget(first_dds, dxgi, w, h))
            {
                File.Copy(first_dds, dst_dds, overwrite: true);
            }
            else
            {
                await ProduceDdsAsync(req.SourceFile, dst_dds, sc, dxgi, w, h, req.Quality, warnings, ct).ConfigureAwait(false);
                first_dds = dst_dds;
            }

            File.Copy(orig.SidecarPath, dst_json, overwrite: true);

            var final = DdsFile.LoadHeader(dst_dds);
            var entry = new ModTextureEntry
            {
                Stem = stem,
                Name = item.Name,
                ContentHash = Json.Hex(item.ContentHash),
                Source = sc.Source,
                Texpack = item.Texpack,
                Wad = wad,
                Wads = wad == null ? [.. item.Wads] : [],
                SourceImage = req.SourceFile,
                Width = final.Width,
                Height = final.Height,
                OriginalWidth = sc.Width,
                OriginalHeight = sc.Height,
                Format = sc.AgcFormat,
            };

            project.Upsert(entry);
            entries.Add(entry);

            log?.Write(LogKind.SUCCESS, $"queued {stem} ({final.Width}x{final.Height}, {DxgiFormats.Friendly(final.Dxgi)}, {final.MipCount} mips)");
        }

        return new ReplaceResult(entries, warnings);
    }

    private static string GameInstallName(string wad) => Game.GameInstall.PackBase(wad);

    private static bool SameTarget(string dds, int dxgi, int w, int h)
    {
        try
        {
            var d = DdsFile.LoadHeader(dds);
            return DxgiFormats.Family(d.Dxgi) == DxgiFormats.Family(dxgi) && d.Width == w && d.Height == h;
        }
        catch
        {
            return false;
        }
    }

    private async Task ProduceDdsAsync(string src, string dst, Sidecar sc, int dxgi, int w, int h, BcQuality q,
        List<string> warnings, CancellationToken ct)
    {
        bool is_dds = src.EndsWith(".dds", StringComparison.OrdinalIgnoreCase);
        bool want_full_chain = sc.IsTexpack;

        if (is_dds)
        {
            var d = DdsFile.LoadHeader(src);
            string? problem = null;

            if (!DxgiFormats.Compatible(dxgi, d.Dxgi))
                problem = $"format is {DxgiFormats.Name(d.Dxgi)}, the game needs {DxgiFormats.Name(dxgi)}";
            else if (d.Slices != sc.Slices)
                problem = $"{d.Slices} slice(s), the game texture has {sc.Slices}";
            else if (d.Depth != sc.Depth)
                problem = $"depth {d.Depth}, the game's volume texture has depth {sc.Depth}";
            else if (d.Width != w || d.Height != h)
                problem = $"size is {d.Width}x{d.Height}, target is {w}x{h}";
            else if (want_full_chain && !d.HasFullChain)
                problem = $"{d.MipCount} mips, a full chain needs {DdsFile.FullChain(d.Width, d.Height)}";
            else if (sc.IsWad && sc.Streamed && d.MipCount != sc.Mips)
                problem = $"{d.MipCount} mips, the WAD copy has {sc.Mips}";

            if (problem == null)
            {
                File.Copy(src, dst, overwrite: true);
                return;
            }

            if (d.Slices > 1 || d.Depth > 1 || sc.IsLayered)
                throw new InvalidOperationException($"DDS cannot be used: {problem}. Cube maps, arrays and volume textures must already match exactly.");

            if (!texconv.Available)
                throw new InvalidOperationException($"DDS cannot be used as is: {problem}. Install texconv to convert automatically.");

            warnings.Add($"Reencoded DDS ({problem}).");
        }
        else
        {
            if (sc.IsLayered)
                throw new InvalidOperationException(sc.Depth > 1
                    ? $"This is a 3D (volume) texture, {sc.Width}x{sc.Height}x{sc.Depth}. Replace it with a 3D DDS of the same size and format."
                    : $"This is a {sc.TexType} texture with {sc.Slices} faces or slices, so replace it with a DDS that has the same layout.");

            if (!texconv.Available)
                throw new InvalidOperationException("Converting images needs texconv.exe (Microsoft DirectXTex). Download it in 'Settings' -> 'Tools', or supply a ready DDS.");
        }

        await texconv.ConvertAsync(src, dst, dxgi, w, h, q, ct).ConfigureAwait(false);

        // WAD low mip copies must keep the WAD's mip count.
        var made = DdsFile.LoadHeader(dst);

        if (sc.IsWad && sc.Streamed && made.MipCount != sc.Mips)
            throw new InvalidOperationException($"Converted DDS has {made.MipCount} mips but the WAD copy needs {sc.Mips}.");
    }
}