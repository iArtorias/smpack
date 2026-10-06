using System.Buffers.Binary;

namespace SmpackGui.Core.Imaging;

/// <summary>
/// Minimal DDS reader.
/// Pixel data is addressed as slice major, then mip, tightly packed.
/// </summary>
public sealed class DdsFile
{
    public int Width
    {
        get;
        private init;
    }
    public int Height
    {
        get;
        private init;
    }
    public int Depth
    {
        get;
        private init;
    } = 1;
    public int MipCount
    {
        get;
        private init;
    } = 1;
    /// <summary>Array slices, cube maps count 6 per cube.</summary>
    public int Slices
    {
        get;
        private init;
    } = 1;
    public bool IsCube
    {
        get;
        private init;
    }
    public int Dxgi
    {
        get;
        private init;
    }
    public byte[] Data
    {
        get;
        private init;
    } = [];
    public int DataOffset
    {
        get;
        private init;
    }

    public string FormatName => DxgiFormats.Name(Dxgi);

    public static DdsFile Load(string path) => Parse(File.ReadAllBytes(path));

    /// <summary>Read only the header, data stays empty.</summary>
    public static DdsFile LoadHeader(string path)
    {
        using var fs = File.OpenRead(path);
        var buf = new byte[Math.Min(fs.Length, 148)];
        fs.ReadExactly(buf);

        return Parse(buf, header_only: true);
    }

    public static DdsFile Parse(byte[] d, bool header_only = false)
    {
        if (d.Length < 128 || BinaryPrimitives.ReadUInt32LittleEndian(d) != 0x20534444)
            throw new InvalidDataException("Not a DDS file");

        uint U(int o) => BinaryPrimitives.ReadUInt32LittleEndian(d.AsSpan(o));

        int height = (int)U(12), width = (int)U(16), depth = (int)U(24), mips = (int)U(28);
        uint flags = U(8);
        uint pf_flags = U(80), fourcc = U(84), rgb_bits = U(88), r_mask = U(92), g_mask = U(96), b_mask = U(100), a_mask = U(104);
        uint caps2 = U(112);

        if ((flags & 0x20000) == 0 || mips == 0)
            mips = 1;

        if ((flags & 0x800000) == 0 || depth == 0)
            depth = 1;

        int offset = 128, slices = 1, dxgi;
        bool cube = (caps2 & 0x200) != 0;

        if ((pf_flags & 0x4) != 0 && fourcc == FourCC("DX10"))
        {
            if (d.Length < 148)
                throw new InvalidDataException("Truncated DX10 header");

            dxgi = (int)U(128);
            uint misc = U(136);
            slices = Math.Max(1, (int)U(140));

            if ((misc & 0x4) != 0)
            {
                cube = true;
            }

            offset = 148;

            if (cube)
                slices *= 6;
        }
        else
        {
            dxgi = LegacyFormat(pf_flags, fourcc, rgb_bits, r_mask, g_mask, b_mask, a_mask);

            if (cube)
                slices = 6;
        }

        if (dxgi == 0)
            throw new NotSupportedException("Unsupported DDS pixel format");

        return new DdsFile
        {
            Width = width,
            Height = height,
            Depth = depth,
            MipCount = mips,
            Slices = slices,
            IsCube = cube,
            Dxgi = dxgi,
            Data = header_only ? [] : d,
            DataOffset = offset,
        };
    }

    private static uint FourCC(string s) => (uint)(s[0] | s[1] << 8 | s[2] << 16 | s[3] << 24);

    private static int LegacyFormat(uint pf_flags, uint fourcc, uint bits, uint r, uint g, uint b, uint a)
    {
        if ((pf_flags & 0x4) != 0)
        {
            if (fourcc == FourCC("DXT1"))
                return DxgiFormats.BC1_UNORM;

            if (fourcc == FourCC("DXT2") || fourcc == FourCC("DXT3"))
                return DxgiFormats.BC2_UNORM;

            if (fourcc == FourCC("DXT4") || fourcc == FourCC("DXT5"))
                return DxgiFormats.BC3_UNORM;

            if (fourcc == FourCC("ATI1") || fourcc == FourCC("BC4U"))
                return DxgiFormats.BC4_UNORM;

            if (fourcc == FourCC("BC4S"))
                return DxgiFormats.BC4_SNORM;

            if (fourcc == FourCC("ATI2") || fourcc == FourCC("BC5U"))
                return DxgiFormats.BC5_UNORM;

            if (fourcc == FourCC("BC5S"))
                return DxgiFormats.BC5_SNORM;

            return fourcc switch
            {
                36 => DxgiFormats.R16G16B16A16_UNORM,
                110 => DxgiFormats.R16G16B16A16_SNORM,
                111 => DxgiFormats.R16_FLOAT,
                112 => DxgiFormats.R16G16_FLOAT,
                113 => DxgiFormats.R16G16B16A16_FLOAT,
                114 => DxgiFormats.R32_FLOAT,
                115 => DxgiFormats.R32G32_FLOAT,
                116 => DxgiFormats.R32G32B32A32_FLOAT,
                _ => 0,
            };
        }

        if (bits == 32)
        {
            if (r == 0x000000FF && g == 0x0000FF00 && b == 0x00FF0000) return DxgiFormats.R8G8B8A8_UNORM;
            if (r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF)
                return a != 0 ? DxgiFormats.B8G8R8A8_UNORM : DxgiFormats.B8G8R8X8_UNORM;
            if (r == 0x3FF && g == 0xFFC00 && b == 0x3FF00000) return DxgiFormats.R10G10B10A2_UNORM;
            if (r == 0xFFFF && g == 0xFFFF0000) return DxgiFormats.R16G16_UNORM;
        }

        if (bits == 16)
        {
            if (r == 0xF800 && g == 0x07E0 && b == 0x001F)
                return DxgiFormats.B5G6R5_UNORM;

            if (r == 0x7C00 && g == 0x03E0 && b == 0x001F)
                return DxgiFormats.B5G5R5A1_UNORM;

            if (r == 0x0F00 && g == 0x00F0 && b == 0x000F)
                return DxgiFormats.B4G4R4A4_UNORM;

            if (r == 0x00FF && g == 0xFF00)
                return DxgiFormats.R8G8_UNORM;

            if (r == 0xFFFF)
                return DxgiFormats.R16_UNORM;
        }

        if (bits == 8)
        {
            if (r == 0xFF) return DxgiFormats.R8_UNORM;
            if (a == 0xFF) return DxgiFormats.A8_UNORM;
        }

        return 0;
    }

    public int MipWidth(int mip) => Math.Max(1, Width >> mip);
    public int MipDepth(int mip) => Math.Max(1, Depth >> mip);
    public bool IsVolume => Depth > 1;
    public int MipHeight(int mip) => Math.Max(1, Height >> mip);

    /// <summary>Size in bytes of one mip level of one slice (depth slices included).</summary>
    public long MipSize(int mip)
    {
        int w = MipWidth(mip), h = MipHeight(mip), dp = Math.Max(1, Depth >> mip);
        int eb = DxgiFormats.ElementBytes(Dxgi);

        if (DxgiFormats.IsBlockCompressed(Dxgi))
            return (long)((w + 3) / 4) * ((h + 3) / 4) * eb * dp;

        return (long)w * h * eb * dp;
    }

    public long SliceSize()
    {
        long s = 0;
        for (int m = 0; m < MipCount; m++)
            s += MipSize(m);

        return s;
    }

    /// <summary>Byte range of a given slice/mip inside <see cref="Data"/>.</summary>
    public ReadOnlyMemory<byte> GetSurface(int slice, int mip, int depth_index = 0)
    {
        if (slice < 0 || slice >= Slices)
            throw new ArgumentOutOfRangeException(nameof(slice));

        if (mip < 0 || mip >= MipCount)
            throw new ArgumentOutOfRangeException(nameof(mip));

        long off = DataOffset + slice * SliceSize();

        for (int m = 0; m < mip; m++)
            off += MipSize(m);

        long size = MipSize(mip);
        int dp = MipDepth(mip);

        if (dp > 1)
        {
            // One 2D layer of a volume texture.
            long layer = size / dp;
            off += Math.Clamp(depth_index, 0, dp - 1) * layer;
            size = layer;
        }

        if (off + size > Data.Length)
            throw new InvalidDataException("DDS data is truncated");

        return new ReadOnlyMemory<byte>(Data, (int)off, (int)size);
    }

    /// <summary>Number of mips a full chain down to 1x1 needs.</summary>
    public static int FullChain(int w, int h)
    {
        int m = Math.Max(w, h),
            n = 0;

        while (m > 0)
        {
            n++;
            m >>= 1;
        }

        return n;
    }

    public bool HasFullChain => MipCount == FullChain(Width, Height);
}