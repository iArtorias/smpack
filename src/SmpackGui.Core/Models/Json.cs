using System.Globalization;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

namespace SmpackGui.Core.Models;

public static class Json
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        PropertyNameCaseInsensitive = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        WriteIndented = true,
        NumberHandling = JsonNumberHandling.AllowReadingFromString,
    };

    public static T Read<T>(string path) =>
        JsonSerializer.Deserialize<T>(File.ReadAllText(path), Options) ?? throw new InvalidDataException($"'{path}' is empty");

    public static T Parse<T>(string text) =>
        JsonSerializer.Deserialize<T>(text, Options) ?? throw new InvalidDataException("empty JSON");

    public static void Write<T>(string path, T value)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);

        var tmp = path + ".tmp";
        File.WriteAllText(tmp, JsonSerializer.Serialize(value, Options));
        File.Move(tmp, path, overwrite: true);
    }

    /// <summary>Parse '0x' or bare hex into a ulong.</summary>
    public static ulong ParseHex(string? s)
    {
        if (string.IsNullOrWhiteSpace(s))
            return 0;

        s = s.Trim();

        if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            s = s[2..];

        return ulong.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var v) ? v : 0;
    }

    public static string Hex(ulong v) => $"0x{v:x16}";
}

/// <summary>One row of 'texindex.json' (smpack index).</summary>
public sealed class TexIndexEntry
{
    public string Name
    {
        get;
        set;
    } = "";
    public string ContentHash
    {
        get;
        set;
    } = "";
    public string TexIdentifier
    {
        get;
        set;
    } = "";
    public string Wad
    {
        get;
        set;
    } = "";
    public int Width
    {
        get;
        set;
    }
    public int Height
    {
        get;
        set;
    }
    public string Format
    {
        get;
        set;
    } = "";
    public string? Texpack
    {
        get;
        set;
    }
    public int? Mips
    {
        get;
        set;
    }
    public bool? Streamed
    {
        get;
        set;
    }
}

public sealed class TexIndex
{
    public string Smpack
    {
        get;
        set;
    } = "";
    public List<TexIndexEntry> Textures
    {
        get;
        set;
    } = [];
}

/// <summary>Texpack flavour.</summary>
public sealed class TexListEntry
{
    public int Index
    {
        get;
        set;
    }
    public string TexIdentifier
    {
        get;
        set;
    } = "";
    public string ContentHash
    {
        get;
        set;
    } = "";
    public int Width
    { 
        get;
        set;
    }
    public int Height
    {
        get;
        set;
    }
    public int Mips
    {
        get;
        set;
    }
    public string Format
    {
        get;
        set;
    } = "";
    public long Bytes
    {
        get;
        set;
    }
    public string Name
    {
        get;
        set;
    } = "";
    // WAD flavour extras.
    public int ParmIndex
    {
        get;
        set;
    }
    public int WadWidth
    {
        get;
        set;
    }
    public int WadHeight
    {
        get;
        set;
    }
    public int WadMips
    {
        get;
        set;
    }
    public bool Streamed
    {
        get;
        set;
    }
    public bool HasData
    {
        get;
        set;
    } = true;
}

public sealed class TexList
{
    public string Source
    {
        get;
        set;
    } = "";
    public string? Pack 
    {
        get;
        set;
    }
    public string? File
    {
        get;
        set;
    }
    public bool HasPayload
    {
        get;
        set;
    } = true;
    public List<TexListEntry> Textures
    {
        get;
        set;
    } = [];
}

/// <summary>
/// The .json sidecar smpack writes next to each exported DDS. Kept as a
/// <see cref="JsonObject"/> so that unknown fields survive a round trip.
/// </summary>
public sealed class Sidecar
{
    public JsonObject Root { get; }
    public string FilePath
    {
        get;
        private set;
    }

    private Sidecar(JsonObject root, string path)
    { 
        Root = root;
        FilePath = path;
    }

    public static Sidecar Load(string path) =>
        new(JsonNode.Parse(File.ReadAllText(path)) as JsonObject ?? throw new InvalidDataException($"'{path}' is not a sidecar"), path);

    public void Save(string path)
    {
        File.WriteAllText(path, Root.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        FilePath = path;
    }

    private string Str(string k) => Root[k]?.GetValue<string>() ?? "";
    private JsonObject? m_Tex => Root["texture"] as JsonObject;

    public string Source => Str("source");
    public bool IsTexpack => Source == "texpack";
    public bool IsWad => Source == "wad";
    public string Name => Str("name");
    public string Pack => Str("pack");
    public string Wad
    {
        get => Str("wad");
        set => Root["wad"] = value;
    }
    public string ContentHash => Str("content_hash");
    public string TexIdentifier => Str("tex_identifier");
    public bool Streamed => Root["streamed"]?.GetValue<bool>() ?? IsTexpack;
    public string AgcFormat => m_Tex?["format"]?.GetValue<string>() ?? "";
    public int Dxgi => m_Tex?["dxgi"]?.GetValue<int>() ?? 0;
    public int Width => m_Tex?["width"]?.GetValue<int>() ?? 0;
    public int Height => m_Tex?["height"]?.GetValue<int>() ?? 0;
    public int Mips => m_Tex?["mips"]?.GetValue<int>() ?? 1;
    public int Slices => m_Tex?["slices"]?.GetValue<int>() ?? 1;
    /// <summary>Depth of a volume (3D) texture, 1 otherwise.</summary>
    public int Depth => m_Tex?["depth"]?.GetValue<int>() ?? 1;
    /// <summary>Anything but a plain 2D image (cube, array or volume). Must be replaced by a matching DDS.</summary>
    public bool IsLayered => Slices > 1 || Depth > 1;
    public string TexType => m_Tex?["type"]?.GetValue<string>() ?? "2d";
    /// <summary>Full highres size recorded in the WAD parm, for WAD sidecars.</summary>
    public int FullWidth => Root["full_width"]?.GetValue<int>() ?? Width;
    public int FullHeight => Root["full_height"]?.GetValue<int>() ?? Height;
}

/// <summary>'boot-options.json' patch lists.</summary>
public sealed class PatchLists
{
    [JsonPropertyName("patch-texpacks")] public List<string> PatchTexpacks 
    { 
        get;
        set;
    } = [];
    [JsonPropertyName("patch-lodpacks")] public List<string> PatchLodpacks 
    { 
        get;
        set;
    } = [];
}