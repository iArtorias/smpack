using SmpackGui.Core.Imaging;
using SmpackGui.Core.Models;

namespace SmpackGui.Core.Textures;

/// <summary>
/// One game texture as the user thinks of it. The same texture is usually
/// referenced by several WADs (each keeps a small low mip copy), streamed
/// textures keep their full resolution mips in one texpack.
/// </summary>
public sealed class TextureItem
{
    public required string Name
    {
        get;
        init;
    }
    public required ulong ContentHash
    {
        get;
        init;
    }
    public ulong TexIdentifier
    {
        get;
        init;
    }
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
    public int Mips
    {
        get;
        init;
    }
    public string Format
    {
        get;
        init;
    } = "";
    /// <summary>Texpack base name holding the streamed mips, or null for WAD resident textures.</summary>
    public string? Texpack
    {
        get;
        init;
    }
    public bool Streamed 
    {
        get;
        init;
    }
    /// <summary>WAD file names that reference this texture.</summary>
    public List<string> Wads { get; } = [];

    public string Key => Texpack != null ? $"tp:{ContentHash:x16}" : $"wad:{ContentHash:x16}:{Name}";
    public int Dxgi => DxgiFormats.FromAgcName(Format);
    public string FormatLabel => DxgiFormats.FriendlyAgc(Format);
    public string SizeLabel => $"{Width}x{Height}";
    public string Location => Texpack ?? (Wads.Count > 0 ? Wads[0] : "?");
    public string WadsLabel => Wads.Count switch
    {
        0 => "-",
        1 => Wads[0],
        _ => $"{Wads[0]} +{Wads.Count - 1}"
    };
    public string HashLabel => $"{ContentHash:x16}";
    /// <summary>Streamed texture whose texpack is unknown (index built without its pack). Only the WAD low mips exist.</summary>
    public bool LowMipsOnly => Streamed && Texpack == null;
    public long Pixels => (long)Width * Height;

    public string StorageLabel => Texpack != null ? "Streamed (texpack)" : LowMipsOnly ? "Streamed (pack not indexed)" : "WAD resident";

    public bool Matches(string query)
    {
        if (string.IsNullOrEmpty(query))
            return true;

        if (Name.Contains(query, StringComparison.OrdinalIgnoreCase))
            return true;

        var q = query.StartsWith("0x", StringComparison.OrdinalIgnoreCase) ? query[2..] : query;

        if (q.Length >= 4 && (HashLabel.Contains(q, StringComparison.OrdinalIgnoreCase) || $"{TexIdentifier:x16}".Contains(q, StringComparison.OrdinalIgnoreCase)))
            return true;

        return Wads.Any(w => w.Contains(query, StringComparison.OrdinalIgnoreCase)) ||
               (Texpack?.Contains(query, StringComparison.OrdinalIgnoreCase) ?? false);
    }
}

public static class TextureCatalog
{
    /// <summary>Collapse per WAD index rows into one item per texture.</summary>
    public static List<TextureItem> FromIndex(TexIndex index)
    {
        var map = new Dictionary<string, TextureItem>();

        foreach (var e in index.Textures)
        {
            var hash = Json.ParseHex(e.ContentHash);
            bool streamed = e.Streamed ?? e.Texpack != null;
            var key = e.Texpack != null ? $"tp:{hash:x16}" : $"wad:{hash:x16}:{e.Name}";

            if (!map.TryGetValue(key, out var item))
            {
                item = new TextureItem
                {
                    Name = e.Name,
                    ContentHash = hash,
                    TexIdentifier = Json.ParseHex(e.TexIdentifier),
                    Width = e.Width,
                    Height = e.Height,
                    Mips = e.Mips ?? DdsFile.FullChain(e.Width, e.Height),
                    Format = e.Format,
                    Texpack = e.Texpack,
                    Streamed = streamed,
                };

                map[key] = item;
            }

            if (!item.Wads.Contains(e.Wad, StringComparer.OrdinalIgnoreCase))
                item.Wads.Add(e.Wad);
        }

        return [.. map.Values.OrderBy(t => t.Name, StringComparer.OrdinalIgnoreCase)];
    }

    /// <summary>Items for textures of a texpack that no WAD in the index names (shown by hex ID).</summary>
    public static List<TextureItem> Unnamed(TexList list, IEnumerable<TextureItem> known)
    {
        var have = known.Select(k => k.ContentHash).ToHashSet();
        var res = new List<TextureItem>();

        foreach (var t in list.Textures)
        {
            var hash = Json.ParseHex(t.ContentHash);

            if (have.Contains(hash))
                continue;

            res.Add(new TextureItem
            {
                Name = string.IsNullOrEmpty(t.Name) ? $"{Json.ParseHex(t.TexIdentifier):x16}" : t.Name,
                ContentHash = hash,
                TexIdentifier = Json.ParseHex(t.TexIdentifier),
                Width = t.Width,
                Height = t.Height,
                Mips = t.Mips,
                Format = t.Format,
                Texpack = list.Pack,
                Streamed = true,
            });
        }

        return res;
    }
}