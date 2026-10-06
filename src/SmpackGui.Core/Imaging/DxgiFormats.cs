namespace SmpackGui.Core.Imaging;

/// <summary>DXGI_FORMAT helpers. Names, element sizes and the AGC (smpack) name mapping.</summary>
public static class DxgiFormats
{
    public const int R32G32B32A32_FLOAT = 2, R16G16B16A16_FLOAT = 10, R16G16B16A16_UNORM = 11, R16G16B16A16_SNORM = 13,
        R32G32_FLOAT = 16, R10G10B10A2_UNORM = 24, R11G11B10_FLOAT = 26, R8G8B8A8_TYPELESS = 27, R8G8B8A8_UNORM = 28,
        R8G8B8A8_UNORM_SRGB = 29, R8G8B8A8_SNORM = 31, R16G16_FLOAT = 34, R16G16_UNORM = 35, R16G16_SNORM = 37,
        R32_FLOAT = 41, R8G8_UNORM = 49, R8G8_SNORM = 51, R16_FLOAT = 54, R16_UNORM = 56, R16_SNORM = 58,
        R8_UNORM = 61, R8_SNORM = 63, A8_UNORM = 65, R9G9B9E5_SHAREDEXP = 67,
        BC1_TYPELESS = 70, BC1_UNORM = 71, BC1_UNORM_SRGB = 72, BC2_TYPELESS = 73, BC2_UNORM = 74, BC2_UNORM_SRGB = 75,
        BC3_TYPELESS = 76, BC3_UNORM = 77, BC3_UNORM_SRGB = 78, BC4_TYPELESS = 79, BC4_UNORM = 80, BC4_SNORM = 81,
        BC5_TYPELESS = 82, BC5_UNORM = 83, BC5_SNORM = 84, B5G6R5_UNORM = 85, B5G5R5A1_UNORM = 86,
        B8G8R8A8_UNORM = 87, B8G8R8X8_UNORM = 88, B8G8R8A8_TYPELESS = 90, B8G8R8A8_UNORM_SRGB = 91, B8G8R8X8_UNORM_SRGB = 93,
        BC6H_TYPELESS = 94, BC6H_UF16 = 95, BC6H_SF16 = 96, BC7_TYPELESS = 97, BC7_UNORM = 98, BC7_UNORM_SRGB = 99,
        B4G4R4A4_UNORM = 115;

    private static readonly Dictionary<int, string> NAMES = new()
    {
        [R32G32B32A32_FLOAT] = "R32G32B32A32_FLOAT",
        [R16G16B16A16_FLOAT] = "R16G16B16A16_FLOAT",
        [R16G16B16A16_UNORM] = "R16G16B16A16_UNORM",
        [R16G16B16A16_SNORM] = "R16G16B16A16_SNORM",
        [R32G32_FLOAT] = "R32G32_FLOAT",
        [R10G10B10A2_UNORM] = "R10G10B10A2_UNORM",
        [R11G11B10_FLOAT] = "R11G11B10_FLOAT",
        [R8G8B8A8_TYPELESS] = "R8G8B8A8_TYPELESS",
        [R8G8B8A8_UNORM] = "R8G8B8A8_UNORM",
        [R8G8B8A8_UNORM_SRGB] = "R8G8B8A8_UNORM_SRGB",
        [R8G8B8A8_SNORM] = "R8G8B8A8_SNORM",
        [R16G16_FLOAT] = "R16G16_FLOAT",
        [R16G16_UNORM] = "R16G16_UNORM",
        [R16G16_SNORM] = "R16G16_SNORM",
        [R32_FLOAT] = "R32_FLOAT",
        [R8G8_UNORM] = "R8G8_UNORM",
        [R8G8_SNORM] = "R8G8_SNORM",
        [R16_FLOAT] = "R16_FLOAT",
        [R16_UNORM] = "R16_UNORM",
        [R16_SNORM] = "R16_SNORM",
        [R8_UNORM] = "R8_UNORM",
        [R8_SNORM] = "R8_SNORM",
        [A8_UNORM] = "A8_UNORM",
        [R9G9B9E5_SHAREDEXP] = "R9G9B9E5_SHAREDEXP",
        [BC1_TYPELESS] = "BC1_TYPELESS",
        [BC1_UNORM] = "BC1_UNORM",
        [BC1_UNORM_SRGB] = "BC1_UNORM_SRGB",
        [BC2_TYPELESS] = "BC2_TYPELESS",
        [BC2_UNORM] = "BC2_UNORM",
        [BC2_UNORM_SRGB] = "BC2_UNORM_SRGB",
        [BC3_TYPELESS] = "BC3_TYPELESS",
        [BC3_UNORM] = "BC3_UNORM",
        [BC3_UNORM_SRGB] = "BC3_UNORM_SRGB",
        [BC4_TYPELESS] = "BC4_TYPELESS",
        [BC4_UNORM] = "BC4_UNORM",
        [BC4_SNORM] = "BC4_SNORM",
        [BC5_TYPELESS] = "BC5_TYPELESS",
        [BC5_UNORM] = "BC5_UNORM",
        [BC5_SNORM] = "BC5_SNORM",
        [B5G6R5_UNORM] = "B5G6R5_UNORM",
        [B5G5R5A1_UNORM] = "B5G5R5A1_UNORM",
        [B8G8R8A8_UNORM] = "B8G8R8A8_UNORM",
        [B8G8R8X8_UNORM] = "B8G8R8X8_UNORM",
        [B8G8R8A8_TYPELESS] = "B8G8R8A8_TYPELESS",
        [B8G8R8A8_UNORM_SRGB] = "B8G8R8A8_UNORM_SRGB",
        [B8G8R8X8_UNORM_SRGB] = "B8G8R8X8_UNORM_SRGB",
        [BC6H_TYPELESS] = "BC6H_TYPELESS",
        [BC6H_UF16] = "BC6H_UF16",
        [BC6H_SF16] = "BC6H_SF16",
        [BC7_TYPELESS] = "BC7_TYPELESS",
        [BC7_UNORM] = "BC7_UNORM",
        [BC7_UNORM_SRGB] = "BC7_UNORM_SRGB",
        [B4G4R4A4_UNORM] = "B4G4R4A4_UNORM",
    };

    public static string Name(int dxgi) => NAMES.TryGetValue(dxgi, out var n) ? n : $"DXGI {dxgi}";

    public static bool IsBlockCompressed(int dxgi) => dxgi is >= 70 and <= 84 or >= 94 and <= 99;

    /// <summary>Bytes per 4x4 block (BC) or per pixel as uncompressed. '0' is unknown.</summary>
    public static int ElementBytes(int dxgi) => dxgi switch
    {
        >= 70 and <= 72 or >= 79 and <= 81 => 8,
        >= 73 and <= 78 or >= 82 and <= 84 or >= 94 and <= 99 => 16,
        >= 1 and <= 4 => 16,
        >= 5 and <= 8 => 12,
        >= 9 and <= 22 => 8,
        >= 23 and <= 47 or 67 or >= 87 and <= 93 => 4,
        >= 48 and <= 59 or 85 or 86 or 115 => 2,
        >= 60 and <= 65 => 1,
        _ => 0,
    };

    public static bool IsSrgb(int dxgi) => dxgi is R8G8B8A8_UNORM_SRGB or BC1_UNORM_SRGB or BC2_UNORM_SRGB or BC3_UNORM_SRGB
        or B8G8R8A8_UNORM_SRGB or B8G8R8X8_UNORM_SRGB or BC7_UNORM_SRGB;

    public static bool IsHdr(int dxgi) => dxgi is BC6H_UF16 or BC6H_SF16 or BC6H_TYPELESS or R16G16B16A16_FLOAT or R32G32B32A32_FLOAT
        or R11G11B10_FLOAT or R16_FLOAT or R32_FLOAT or R16G16_FLOAT or R32G32_FLOAT or R9G9B9E5_SHAREDEXP;

    /// <summary>Encoding family ignoring sRGB and typeless differences.</summary>
    public static int Family(int dxgi) => dxgi switch
    {
        BC1_TYPELESS or BC1_UNORM_SRGB => BC1_UNORM,
        BC2_TYPELESS or BC2_UNORM_SRGB => BC2_UNORM,
        BC3_TYPELESS or BC3_UNORM_SRGB => BC3_UNORM,
        BC4_TYPELESS => BC4_UNORM,
        BC5_TYPELESS => BC5_UNORM,
        BC6H_TYPELESS => BC6H_UF16,
        BC7_TYPELESS or BC7_UNORM_SRGB => BC7_UNORM,
        R8G8B8A8_TYPELESS or R8G8B8A8_UNORM_SRGB => R8G8B8A8_UNORM,
        B8G8R8A8_TYPELESS or B8G8R8A8_UNORM_SRGB => B8G8R8A8_UNORM,
        _ => dxgi,
    };

    public static bool Compatible(int wanted, int have)
    {
        if (Family(wanted) == Family(have))
            return true;

        // smpack swaps B8G8R8A8 to R8G8B8A8 on import.
        return Family(wanted) == R8G8B8A8_UNORM && Family(have) == B8G8R8A8_UNORM;
    }

    /// <summary>Short human label, e.g. 'BC7 sRGB'.</summary>
    public static string Friendly(int dxgi) => dxgi switch
    {
        BC1_UNORM => "BC1",
        BC1_UNORM_SRGB => "BC1 sRGB",
        BC2_UNORM => "BC2",
        BC2_UNORM_SRGB => "BC2 sRGB",
        BC3_UNORM => "BC3",
        BC3_UNORM_SRGB => "BC3 sRGB",
        BC4_UNORM => "BC4",
        BC4_SNORM => "BC4 signed",
        BC5_UNORM => "BC5",
        BC5_SNORM => "BC5 signed",
        BC6H_UF16 => "BC6H",
        BC6H_SF16 => "BC6H signed",
        BC7_UNORM => "BC7",
        BC7_UNORM_SRGB => "BC7 sRGB",
        R8G8B8A8_UNORM => "RGBA8",
        R8G8B8A8_UNORM_SRGB => "RGBA8 sRGB",
        B8G8R8A8_UNORM => "BGRA8",
        _ => Name(dxgi),
    };

    /// <summary>Map an smpack/AGC format name to DXGI, '0' if unknown.</summary>
    public static int FromAgcName(string? agc) => agc switch
    {
        "Bc1UNorm" => BC1_UNORM,
        "Bc1Srgb" => BC1_UNORM_SRGB,
        "Bc2UNorm" => BC2_UNORM,
        "Bc2Srgb" => BC2_UNORM_SRGB,
        "Bc3UNorm" => BC3_UNORM,
        "Bc3Srgb" => BC3_UNORM_SRGB,
        "Bc4UNorm" => BC4_UNORM,
        "Bc4SNorm" => BC4_SNORM,
        "Bc5UNorm" => BC5_UNORM,
        "Bc5SNorm" => BC5_SNORM,
        "Bc6UFloat" => BC6H_UF16,
        "Bc6SFloat" => BC6H_SF16,
        "Bc7UNorm" => BC7_UNORM,
        "Bc7Srgb" => BC7_UNORM_SRGB,
        "8_8_8_8UNorm" => R8G8B8A8_UNORM,
        "8_8_8_8Srgb" => R8G8B8A8_UNORM_SRGB,
        "8_8_8_8SNorm" => R8G8B8A8_SNORM,
        "8UNorm" => R8_UNORM,
        "8SNorm" => R8_SNORM,
        "8_8UNorm" => R8G8_UNORM,
        "8_8SNorm" => R8G8_SNORM,
        "16Float" => R16_FLOAT,
        "16UNorm" => R16_UNORM,
        "32Float" => R32_FLOAT,
        "16_16UNorm" => R16G16_UNORM,
        "16_16Float" => R16G16_FLOAT,
        "16_16_16_16Float" => R16G16B16A16_FLOAT,
        "16_16_16_16UNorm" => R16G16B16A16_UNORM,
        "32_32_32_32Float" => R32G32B32A32_FLOAT,
        "2_10_10_10UNorm" => R10G10B10A2_UNORM,
        "10_11_11Float" => R11G11B10_FLOAT,
        "9_9_9_5Float" => R9G9B9E5_SHAREDEXP,
        "5_6_5UNorm" => B5G6R5_UNORM,
        "5_5_5_1UNorm" => B5G5R5A1_UNORM,
        "4_4_4_4UNorm" => B4G4R4A4_UNORM,
        _ => 0,
    };

    /// <summary>Friendly label straight from an AGC name.</summary>
    public static string FriendlyAgc(string? agc)
    {
        var d = FromAgcName(agc);
        return d != 0 ? Friendly(d) : agc ?? "?";
    }

    /// <summary>What kind of content a format usually carries, for UI hints.</summary>
    public static string UsageHint(int dxgi) => Family(dxgi) switch
    {
        BC1_UNORM => IsSrgb(dxgi) ? "Color (no, 1 bit alpha)" : "Data / mask (RGB)",
        BC3_UNORM => "Color and alpha",
        BC4_UNORM => "Single channel (gloss, AO, mask...)",
        BC5_UNORM => "Two channels (usually a normal map XY)",
        BC6H_UF16 => "HDR color (skies, probes)",
        BC7_UNORM => IsSrgb(dxgi) ? "High quality color" : "High quality data / normal map",
        R8G8B8A8_UNORM => "Uncompressed RGBA",
        _ => "",
    };
}