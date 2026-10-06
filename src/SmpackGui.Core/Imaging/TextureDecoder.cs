using System.Buffers.Binary;

namespace SmpackGui.Core.Imaging;

/// <summary>Decodes DDS surfaces (BC1-BC7 including BC6H, common uncompressed formats) to RGBA8/linear floats.</summary>
public static class TextureDecoder
{
    public static bool IsSupported(int dxgi) =>
        DxgiFormats.IsBlockCompressed(dxgi) || UncompressedSupported(dxgi);

    public static DecodedImage Decode(DdsFile dds, int slice = 0, int mip = 0, float exposure = 1f, int depth_index = 0)
    {
        var data = dds.GetSurface(slice, mip, depth_index).Span;
        return Decode(data, dds.Dxgi, dds.MipWidth(mip), dds.MipHeight(mip), exposure);
    }

    public static DecodedImage Decode(ReadOnlySpan<byte> data, int dxgi, int width, int height, float exposure = 1f)
    {
        int fam = DxgiFormats.Family(dxgi);

        if (fam == DxgiFormats.BC6H_UF16 || dxgi == DxgiFormats.BC6H_SF16)
        {
            var hdr = new float[width * height * 4];
            bool signed = dxgi == DxgiFormats.BC6H_SF16;
            DecodeBlocks(data, width, height, 16, (blk, px) => Bc6h.DecodeBlock(blk, px, signed), hdr);

            return new DecodedImage(width, height, hdr, exposure);
        }

        if (DxgiFormats.IsBlockCompressed(dxgi))
        {
            var rgba = new byte[width * height * 4];
            switch (dxgi)
            {
                case DxgiFormats.BC1_UNORM or DxgiFormats.BC1_UNORM_SRGB or DxgiFormats.BC1_TYPELESS:
                    DecodeBlocks(data, width, height, 8, static (b, p) => Bc1(b, p, true), rgba);
                    break;

                case DxgiFormats.BC2_UNORM or DxgiFormats.BC2_UNORM_SRGB or DxgiFormats.BC2_TYPELESS:
                    DecodeBlocks(data, width, height, 16, Bc2, rgba);
                    break;

                case DxgiFormats.BC3_UNORM or DxgiFormats.BC3_UNORM_SRGB or DxgiFormats.BC3_TYPELESS:
                    DecodeBlocks(data, width, height, 16, Bc3, rgba);
                    break;

                case DxgiFormats.BC4_UNORM or DxgiFormats.BC4_TYPELESS:
                    DecodeBlocks(data, width, height, 8, static (b, p) => Bc4(b, p, false), rgba);
                    break;

                case DxgiFormats.BC4_SNORM:
                    DecodeBlocks(data, width, height, 8, static (b, p) => Bc4(b, p, true), rgba);
                    break;

                case DxgiFormats.BC5_UNORM or DxgiFormats.BC5_TYPELESS:
                    DecodeBlocks(data, width, height, 16, static (b, p) => Bc5(b, p, false), rgba);
                    break;

                case DxgiFormats.BC5_SNORM:
                    DecodeBlocks(data, width, height, 16, static (b, p) => Bc5(b, p, true), rgba);
                    break;

                case DxgiFormats.BC7_UNORM or DxgiFormats.BC7_UNORM_SRGB or DxgiFormats.BC7_TYPELESS:
                    DecodeBlocks(data, width, height, 16, Bc7.DecodeBlock, rgba);
                    break;

                default:
                    throw new NotSupportedException($"Cannot decode {DxgiFormats.Name(dxgi)}");
            }

            return new DecodedImage(width, height, rgba);
        }

        return DecodeUncompressed(data, dxgi, width, height, exposure);
    }

    // Block plumbing.

    private delegate void BlockDecoder<T>(ReadOnlySpan<byte> block, Span<T> pixels16);

    private static void DecodeBlocks<T>(ReadOnlySpan<byte> data, int width, int height, int block_bytes,
        BlockDecoder<T> decode, T[] dst) where T : struct
    {
        int bw = (width + 3) / 4, bh = (height + 3) / 4;

        if (data.Length < (long)bw * bh * block_bytes)
            throw new InvalidDataException("Surface data is truncated");

        // Copy into an array so the parallel loop can capture it.
        var src = data.Slice(0, bw * bh * block_bytes).ToArray();
        Parallel.For(0, bh, by =>
        {
            var tmp = new T[16 * 4];

            for (int bx = 0; bx < bw; bx++)
            {
                decode(src.AsSpan((by * bw + bx) * block_bytes, block_bytes), tmp);

                for (int y = 0; y < 4; y++)
                {
                    int py = by * 4 + y;

                    if (py >= height)
                        break;

                    for (int x = 0; x < 4; x++)
                    {
                        int px_x = bx * 4 + x;

                        if (px_x >= width)
                            break;

                        int d = (py * width + px_x) * 4, s = (y * 4 + x) * 4;

                        dst[d] = tmp[s];
                        dst[d + 1] = tmp[s + 1];
                        dst[d + 2] = tmp[s + 2];
                        dst[d + 3] = tmp[s + 3];
                    }
                }
            }
        });
    }

    // BC1-BC5

    private static void Bc1(ReadOnlySpan<byte> b, Span<byte> p, bool allow_alpha)
    {
        ushort c0 = BinaryPrimitives.ReadUInt16LittleEndian(b), c1 = BinaryPrimitives.ReadUInt16LittleEndian(b[2..]);
        Span<byte> pal = stackalloc byte[16];

        Rgb565(c0, pal[..4]);
        Rgb565(c1, pal.Slice(4, 4));

        if (c0 > c1 || !allow_alpha)
        {
            for (int c = 0; c < 3; c++)
            {
                pal[8 + c] = (byte)((2 * pal[c] + pal[4 + c] + 1) / 3);
                pal[12 + c] = (byte)((pal[c] + 2 * pal[4 + c] + 1) / 3);
            }

            pal[11] = pal[15] = 255;
        }
        else
        {
            for (int c = 0; c < 3; c++)
                pal[8 + c] = (byte)((pal[c] + pal[4 + c]) / 2);

            pal[11] = 255;
            pal[12] = pal[13] = pal[14] = pal[15] = 0;
        }

        uint idx = BinaryPrimitives.ReadUInt32LittleEndian(b[4..]);

        for (int i = 0; i < 16; i++)
        {
            int k = (int)(idx >> (2 * i) & 3) * 4;

            p[i * 4] = pal[k];
            p[i * 4 + 1] = pal[k + 1];
            p[i * 4 + 2] = pal[k + 2];
            p[i * 4 + 3] = pal[k + 3];
        }
    }

    private static void Rgb565(ushort c, Span<byte> o)
    {
        int r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
        o[0] = (byte)(r << 3 | r >> 2);
        o[1] = (byte)(g << 2 | g >> 4);
        o[2] = (byte)(b << 3 | b >> 2);
        o[3] = 255;
    }

    private static void Bc2(ReadOnlySpan<byte> b, Span<byte> p)
    {
        Bc1(b[8..], p, false);

        ulong a = BinaryPrimitives.ReadUInt64LittleEndian(b);

        for (int i = 0; i < 16; i++)
        {
            int v = (int)(a >> (4 * i) & 15);
            p[i * 4 + 3] = (byte)(v << 4 | v);
        }
    }

    private static void Bc3(ReadOnlySpan<byte> b, Span<byte> p)
    {
        Bc1(b[8..], p, false);

        Span<byte> a = stackalloc byte[16];
        AlphaBlock(b, a, false);

        for (int i = 0; i < 16; i++)
            p[i * 4 + 3] = a[i];
    }

    /// <summary>BC3 and BC4 style interpolated 8 bit channel. Signed values are remapped to '0 - 255'.</summary>
    private static void AlphaBlock(ReadOnlySpan<byte> b, Span<byte> o, bool signed)
    {
        Span<int> pal = stackalloc int[8];

        int a0 = signed ? Math.Max(-127, (int)(sbyte)b[0]) : b[0];
        int a1 = signed ? Math.Max(-127, (int)(sbyte)b[1]) : b[1];

        pal[0] = a0; pal[1] = a1;

        if (a0 > a1)
            for (int i = 1; i < 7; i++)
                pal[i + 1] = ((7 - i) * a0 + i * a1) / 7;
        else
        {
            for (int i = 1; i < 5; i++)
                pal[i + 1] = ((5 - i) * a0 + i * a1) / 5;

            pal[6] = signed ? -127 : 0;
            pal[7] = signed ? 127 : 255;
        }

        ulong bits = 0;
        for (int i = 0; i < 6; i++)
            bits |= (ulong)b[2 + i] << (8 * i);

        for (int i = 0; i < 16; i++)
        {
            int v = pal[(int)(bits >> (3 * i) & 7)];
            o[i] = signed ? (byte)Math.Clamp((int)Math.Round((v / 127.0 * 0.5 + 0.5) * 255), 0, 255) : (byte)v;
        }
    }

    private static void Bc4(ReadOnlySpan<byte> b, Span<byte> p, bool signed)
    {
        Span<byte> r = stackalloc byte[16];

        AlphaBlock(b, r, signed);

        for (int i = 0; i < 16; i++)
        {
            p[i * 4] = p[i * 4 + 1] = p[i * 4 + 2] = r[i];
            p[i * 4 + 3] = 255;
        }
    }

    private static void Bc5(ReadOnlySpan<byte> b, Span<byte> p, bool signed)
    {
        Span<byte> r = stackalloc byte[16], g = stackalloc byte[16];

        AlphaBlock(b, r, signed);
        AlphaBlock(b[8..], g, signed);

        for (int i = 0; i < 16; i++)
        {
            p[i * 4] = r[i];
            p[i * 4 + 1] = g[i];
            p[i * 4 + 2] = 0;
            p[i * 4 + 3] = 255;
        }
    }

    // Uncompressed.

    private static bool UncompressedSupported(int dxgi) => dxgi is DxgiFormats.R8G8B8A8_UNORM or DxgiFormats.R8G8B8A8_UNORM_SRGB
        or DxgiFormats.R8G8B8A8_TYPELESS or DxgiFormats.R8G8B8A8_SNORM or DxgiFormats.B8G8R8A8_UNORM or DxgiFormats.B8G8R8A8_UNORM_SRGB
        or DxgiFormats.B8G8R8X8_UNORM or DxgiFormats.B8G8R8X8_UNORM_SRGB or DxgiFormats.B8G8R8A8_TYPELESS or DxgiFormats.R8_UNORM
        or DxgiFormats.A8_UNORM or DxgiFormats.R8G8_UNORM or DxgiFormats.R16_UNORM or DxgiFormats.R16G16_UNORM
        or DxgiFormats.R16G16B16A16_UNORM or DxgiFormats.R10G10B10A2_UNORM or DxgiFormats.B5G6R5_UNORM or DxgiFormats.B5G5R5A1_UNORM
        or DxgiFormats.B4G4R4A4_UNORM or DxgiFormats.R16_FLOAT or DxgiFormats.R16G16_FLOAT or DxgiFormats.R16G16B16A16_FLOAT
        or DxgiFormats.R32_FLOAT or DxgiFormats.R32G32_FLOAT or DxgiFormats.R32G32B32A32_FLOAT or DxgiFormats.R11G11B10_FLOAT
        or DxgiFormats.R9G9B9E5_SHAREDEXP;

    private static DecodedImage DecodeUncompressed(ReadOnlySpan<byte> s, int dxgi, int w, int h, float exposure)
    {
        int n = w * h, eb = DxgiFormats.ElementBytes(dxgi);

        if (eb == 0 || !UncompressedSupported(dxgi))
            throw new NotSupportedException($"Cannot decode {DxgiFormats.Name(dxgi)}");

        if (s.Length < (long)n * eb)
            throw new InvalidDataException("Surface data is truncated");

        if (DxgiFormats.IsHdr(dxgi))
        {
            var f = new float[n * 4];

            for (int i = 0; i < n; i++)
            {
                var e = s.Slice(i * eb, eb);
                float r = 0, g = 0, b = 0, a = 1;

                switch (dxgi)
                {
                    case DxgiFormats.R16_FLOAT:
                        r = g = b = H(e, 0);
                        break;

                    case DxgiFormats.R16G16_FLOAT:
                        r = H(e, 0);
                        g = H(e, 2);
                        break;

                    case DxgiFormats.R16G16B16A16_FLOAT:
                        r = H(e, 0);
                        g = H(e, 2);
                        b = H(e, 4);
                        a = H(e, 6);
                        break;

                    case DxgiFormats.R32_FLOAT:
                        r = g = b = F(e, 0);
                        break;

                    case DxgiFormats.R32G32_FLOAT:
                        r = F(e, 0);
                        g = F(e, 4);
                        break;

                    case DxgiFormats.R32G32B32A32_FLOAT:
                        r = F(e, 0);
                        g = F(e, 4);
                        b = F(e, 8);
                        a = F(e, 12);
                        break;

                    case DxgiFormats.R11G11B10_FLOAT:
                        {
                            uint v = BinaryPrimitives.ReadUInt32LittleEndian(e);

                            r = SmallFloat(v & 0x7FF, 6);
                            g = SmallFloat(v >> 11 & 0x7FF, 6);
                            b = SmallFloat(v >> 22 & 0x3FF, 5);
                            break;
                        }

                    case DxgiFormats.R9G9B9E5_SHAREDEXP:
                        {
                            uint v = BinaryPrimitives.ReadUInt32LittleEndian(e);

                            float scale = MathF.Pow(2, (int)(v >> 27) - 15 - 9);
                            r = (v & 0x1FF) * scale;
                            g = (v >> 9 & 0x1FF) * scale;
                            b = (v >> 18 & 0x1FF) * scale;
                            break;
                        }
                }

                f[i * 4] = r;
                f[i * 4 + 1] = g;
                f[i * 4 + 2] = b;
                f[i * 4 + 3] = a;
            }

            return new DecodedImage(w, h, f, exposure);
        }

        var o = new byte[n * 4];
        for (int i = 0; i < n; i++)
        {
            var e = s.Slice(i * eb, eb);
            int d = i * 4;

            switch (dxgi)
            {
                case DxgiFormats.R8G8B8A8_UNORM or DxgiFormats.R8G8B8A8_UNORM_SRGB or DxgiFormats.R8G8B8A8_TYPELESS:
                    o[d] = e[0];
                    o[d + 1] = e[1];
                    o[d + 2] = e[2];
                    o[d + 3] = e[3];
                    break;

                case DxgiFormats.R8G8B8A8_SNORM:
                    for (int c = 0; c < 4; c++)
                        o[d + c] = (byte)((sbyte)e[c] + 128);
                    break;

                case DxgiFormats.B8G8R8A8_UNORM or DxgiFormats.B8G8R8A8_UNORM_SRGB or DxgiFormats.B8G8R8A8_TYPELESS:
                    o[d] = e[2];
                    o[d + 1] = e[1];
                    o[d + 2] = e[0];
                    o[d + 3] = e[3];
                    break;
                case DxgiFormats.B8G8R8X8_UNORM or DxgiFormats.B8G8R8X8_UNORM_SRGB:
                    o[d] = e[2];
                    o[d + 1] = e[1];
                    o[d + 2] = e[0];
                    o[d + 3] = 255;
                    break;

                case DxgiFormats.R8_UNORM:
                    o[d] = o[d + 1] = o[d + 2] = e[0];
                    o[d + 3] = 255;
                    break;

                case DxgiFormats.A8_UNORM:
                    o[d] = o[d + 1] = o[d + 2] = 0;
                    o[d + 3] = e[0];
                    break;

                case DxgiFormats.R8G8_UNORM:
                    o[d] = e[0];
                    o[d + 1] = e[1];
                    o[d + 2] = 0;
                    o[d + 3] = 255;
                    break;

                case DxgiFormats.R16_UNORM:
                    o[d] = o[d + 1] = o[d + 2] = e[1];
                    o[d + 3] = 255;
                    break;

                case DxgiFormats.R16G16_UNORM:
                    o[d] = e[1];
                    o[d + 1] = e[3];
                    o[d + 2] = 0;
                    o[d + 3] = 255;
                    break;

                case DxgiFormats.R16G16B16A16_UNORM:
                    o[d] = e[1];
                    o[d + 1] = e[3];
                    o[d + 2] = e[5];
                    o[d + 3] = e[7];
                    break;

                case DxgiFormats.R10G10B10A2_UNORM:
                {
                    uint v = BinaryPrimitives.ReadUInt32LittleEndian(e);

                    o[d] = (byte)((v & 0x3FF) >> 2);
                    o[d + 1] = (byte)((v >> 10 & 0x3FF) >> 2);
                    o[d + 2] = (byte)((v >> 20 & 0x3FF) >> 2);
                    o[d + 3] = (byte)((v >> 30) * 85);
                    break;
                }

                case DxgiFormats.B5G6R5_UNORM:
                    Rgb565(BinaryPrimitives.ReadUInt16LittleEndian(e), o.AsSpan(d, 4));
                    break;

                case DxgiFormats.B5G5R5A1_UNORM:
                {
                    int v = BinaryPrimitives.ReadUInt16LittleEndian(e);
                    int r = v >> 10 & 31, g = v >> 5 & 31, b = v & 31;

                    o[d] = (byte)(r << 3 | r >> 2);
                    o[d + 1] = (byte)(g << 3 | g >> 2);
                    o[d + 2] = (byte)(b << 3 | b >> 2);
                    o[d + 3] = (byte)((v >> 15) * 255);
                    break;
                }

                case DxgiFormats.B4G4R4A4_UNORM:
                {
                    int v = BinaryPrimitives.ReadUInt16LittleEndian(e);

                    o[d] = (byte)((v >> 8 & 15) * 17);
                    o[d + 1] = (byte)((v >> 4 & 15) * 17);
                    o[d + 2] = (byte)((v & 15) * 17);
                    o[d + 3] = (byte)((v >> 12 & 15) * 17);
                    break;
                }
            }
        }

        return new DecodedImage(w, h, o);
    }

    private static float H(ReadOnlySpan<byte> e, int o) => (float)BitConverter.UInt16BitsToHalf(BinaryPrimitives.ReadUInt16LittleEndian(e[o..]));
    private static float F(ReadOnlySpan<byte> e, int o) => BinaryPrimitives.ReadSingleLittleEndian(e[o..]);

    private static float SmallFloat(uint v, int mant_bits)
    {
        uint exp = v >> mant_bits, mant = v & ((1u << mant_bits) - 1);

        if (exp == 0)
            return mant / (float)(1 << mant_bits) * MathF.Pow(2, -14);

        if (exp == 31)
            return mant == 0 ? float.PositiveInfinity : float.NaN;

        return (1 + mant / (float)(1 << mant_bits)) * MathF.Pow(2, (int)exp - 15);
    }
}