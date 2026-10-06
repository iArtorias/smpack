// AGC texture descriptors and GPU surface addressing.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "smpack/reader.hpp"

namespace smpack::agc
{
    // Enums.

    enum class TileMode : std::uint32_t
    {
        LINEAR = 0,
        STANDARD_256B = 1,
        STANDARD_4KB = 5,
        STANDARD_64KB = 9,
        PRT = 17,
        DEPTH = 24,
        RENDER_TARGET = 27,
    };

    enum class TexType : std::uint32_t
    {
        TEX_1D = 8,
        TEX_2D = 9,
        TEX_3D = 10,
        CUBEMAP = 11,
        TEX_1D_ARRAY = 12,
        TEX_2D_ARRAY = 13,
        TEX_2D_MSAA = 14,
        TEX_2D_ARRAY_MSAA = 15,
    };

    [[nodiscard]] constexpr std::string_view TileModeName( 
        std::uint32_t t
    ) noexcept
    {
        switch (t)
        {
            case 0:
                return "Linear";

            case 1:
                return "Standard256B";

            case 5:
                return "Standard4KB";

            case 9:
                return "Standard64KB";

            case 17:
                return "Prt";

            case 24:
                return "Depth";

            case 27:
                return "RenderTarget";

            default:
                return "?";
        }
    }


    [[nodiscard]] constexpr std::string_view TexTypeName( 
        std::uint32_t t 
    ) noexcept
    {
        switch (t)
        {
            case 8:
                return "1d";

            case 9:
                return "2d";

            case 10:
                return "3d";

            case 11:
                return "cube";

            case 12:
                return "1d-array";

            case 13:
                return "2d-array";

            case 14:
                return "2d-msaa";

            case 15:
                return "2d-array-msaa";

            default:
                return "?";
        }
    }

    // Format table.

    struct FormatInfo
    {
        std::uint32_t m_Agc = 0; // AGC typed format.
        std::string_view m_Name;
        std::uint32_t m_Bits = 0; // Bits per element [0 = unsupported].
        std::uint32_t m_Block = 1; // Texels per element edge [4 for BC].
        std::uint32_t m_Dxgi = 0; // 'DXGI_FORMAT', 0 = none.
        bool m_Srgb = false;
    };

    // 'DXGI_FORMAT' values used below.
    namespace dxgi
    {
        inline constexpr std::uint32_t R32G32B32A32_FLOAT = 2, R32G32B32A32_UINT = 3, R32G32B32A32_SINT = 4,
            R32G32B32_FLOAT = 6, R32G32B32_UINT = 7, R32G32B32_SINT = 8, R16G16B16A16_FLOAT = 10,
            R16G16B16A16_UNORM = 11, R16G16B16A16_UINT = 12, R16G16B16A16_SNORM = 13, R16G16B16A16_SINT = 14,
            R32G32_FLOAT = 16, R32G32_UINT = 17, R32G32_SINT = 18, R10G10B10A2_UNORM = 24, R10G10B10A2_UINT = 25,
            R11G11B10_FLOAT = 26, R8G8B8A8_UNORM = 28, R8G8B8A8_UNORM_SRGB = 29, R8G8B8A8_UINT = 30,
            R8G8B8A8_SNORM = 31, R8G8B8A8_SINT = 32, R16G16_FLOAT = 34, R16G16_UNORM = 35, R16G16_UINT = 36,
            R16G16_SNORM = 37, R16G16_SINT = 38, R32_FLOAT = 41, R32_UINT = 42, R32_SINT = 43, R8G8_UNORM = 49,
            R8G8_UINT = 50, R8G8_SNORM = 51, R8G8_SINT = 52, R16_FLOAT = 54, R16_UNORM = 56, R16_UINT = 57,
            R16_SNORM = 58, R16_SINT = 59, R8_UNORM = 61, R8_UINT = 62, R8_SNORM = 63, R8_SINT = 64,
            R9G9B9E5_SHAREDEXP = 67, BC1_UNORM = 71, BC1_UNORM_SRGB = 72, BC2_UNORM = 74, BC2_UNORM_SRGB = 75,
            BC3_UNORM = 77, BC3_UNORM_SRGB = 78, BC4_UNORM = 80, BC4_SNORM = 81, BC5_UNORM = 83, BC5_SNORM = 84,
            B5G6R5_UNORM = 85, B5G5R5A1_UNORM = 86, BC6H_UF16 = 95, BC6H_SF16 = 96, BC7_UNORM = 98,
            BC7_UNORM_SRGB = 99, B4G4R4A4_UNORM = 115;
    }


    [[nodiscard]] inline const FormatInfo * FindFormatInfo(
        std::uint32_t f
    ) noexcept
    {
        using namespace dxgi;
        static const FormatInfo table[] =
        {
            { 1, "8UNorm", 8, 1, R8_UNORM }, { 2, "8SNorm", 8, 1, R8_SNORM },
            { 3, "8UScaled", 8, 1, 0 }, { 4, "8SScaled", 8, 1, 0 },
            { 5, "8UInt", 8, 1, R8_UINT }, { 6, "8SInt", 8, 1, R8_SINT },
            { 7, "16UNorm", 16, 1, R16_UNORM }, { 8, "16SNorm", 16, 1, R16_SNORM },
            { 9, "16UScaled", 16, 1, 0 }, { 10, "16SScaled", 16, 1, 0 },
            { 11, "16UInt", 16, 1, R16_UINT }, { 12, "16SInt", 16, 1, R16_SINT },
            { 13, "16Float", 16, 1, R16_FLOAT }, { 14, "8_8UNorm", 16, 1, R8G8_UNORM },
            { 15, "8_8SNorm", 16, 1, R8G8_SNORM }, { 16, "8_8UScaled", 16, 1, 0 },
            { 17, "8_8SScaled", 16, 1, 0 }, { 18, "8_8UInt", 16, 1, R8G8_UINT },
            { 19, "8_8SInt", 16, 1, R8G8_SINT }, { 20, "32UInt", 32, 1, R32_UINT },
            { 21, "32SInt", 32, 1, R32_SINT }, { 22, "32Float", 32, 1, R32_FLOAT },
            { 23, "16_16UNorm", 32, 1, R16G16_UNORM }, { 24, "16_16SNorm", 32, 1, R16G16_SNORM },
            { 25, "16_16UScaled", 32, 1, 0 }, { 26, "16_16SScaled", 32, 1, 0 },
            { 27, "16_16UInt", 32, 1, R16G16_UINT }, { 28, "16_16SInt", 32, 1, R16G16_SINT },
            { 29, "16_16Float", 32, 1, R16G16_FLOAT },
            { 30, "11_11_10UNorm", 32, 1, 0 }, { 31, "11_11_10SNorm", 32, 1, 0 },
            { 32, "11_11_10UScaled", 32, 1, 0 }, { 33, "11_11_10SScaled", 32, 1, 0 },
            { 34, "11_11_10UInt", 32, 1, 0 }, { 35, "11_11_10SInt", 32, 1, 0 },
            { 36, "11_11_10Float", 32, 1, 0 }, { 37, "10_11_11UNorm", 32, 1, 0 },
            { 38, "10_11_11SNorm", 32, 1, 0 }, { 39, "10_11_11UScaled", 32, 1, 0 },
            { 40, "10_11_11SScaled", 32, 1, 0 }, { 41, "10_11_11UInt", 32, 1, 0 },
            { 42, "10_11_11SInt", 32, 1, 0 }, { 43, "10_11_11Float", 32, 1, R11G11B10_FLOAT },
            { 44, "2_10_10_10UNorm", 32, 1, R10G10B10A2_UNORM }, { 45, "2_10_10_10SNorm", 32, 1, 0 },
            { 46, "2_10_10_10UScaled", 32, 1, 0 }, { 47, "2_10_10_10SScaled", 32, 1, 0 },
            { 48, "2_10_10_10UInt", 32, 1, R10G10B10A2_UINT }, { 49, "2_10_10_10SInt", 32, 1, 0 },
            { 50, "10_10_10_2UNorm", 32, 1, 0 }, { 51, "10_10_10_2SNorm", 32, 1, 0 },
            { 52, "10_10_10_2UScaled", 32, 1, 0 }, { 53, "10_10_10_2SScaled", 32, 1, 0 },
            { 54, "10_10_10_2UInt", 32, 1, 0 }, { 55, "10_10_10_2SInt", 32, 1, 0 },
            { 56, "8_8_8_8UNorm", 32, 1, R8G8B8A8_UNORM }, { 57, "8_8_8_8SNorm", 32, 1, R8G8B8A8_SNORM },
            { 58, "8_8_8_8UScaled", 32, 1, 0 }, { 59, "8_8_8_8SScaled", 32, 1, 0 },
            { 60, "8_8_8_8UInt", 32, 1, R8G8B8A8_UINT }, { 61, "8_8_8_8SInt", 32, 1, R8G8B8A8_SINT },
            { 62, "32_32UInt", 64, 1, R32G32_UINT }, { 63, "32_32SInt", 64, 1, R32G32_SINT },
            { 64, "32_32Float", 64, 1, R32G32_FLOAT },
            { 65, "16_16_16_16UNorm", 64, 1, R16G16B16A16_UNORM }, { 66, "16_16_16_16SNorm", 64, 1, R16G16B16A16_SNORM },
            { 67, "16_16_16_16UScaled", 64, 1, 0 }, { 68, "16_16_16_16SScaled", 64, 1, 0 },
            { 69, "16_16_16_16UInt", 64, 1, R16G16B16A16_UINT }, { 70, "16_16_16_16SInt", 64, 1, R16G16B16A16_SINT },
            { 71, "16_16_16_16Float", 64, 1, R16G16B16A16_FLOAT },
            { 72, "32_32_32UInt", 96, 1, R32G32B32_UINT }, { 73, "32_32_32SInt", 96, 1, R32G32B32_SINT },
            { 74, "32_32_32Float", 96, 1, R32G32B32_FLOAT },
            { 75, "32_32_32_32UInt", 128, 1, R32G32B32A32_UINT }, { 76, "32_32_32_32SInt", 128, 1, R32G32B32A32_SINT },
            { 77, "32_32_32_32Float", 128, 1, R32G32B32A32_FLOAT },
            { 128, "8Srgb", 8, 1, 0, true }, { 129, "8_8Srgb", 16, 1, 0, true },
            { 130, "8_8_8_8Srgb", 32, 1, R8G8B8A8_UNORM_SRGB, true },
            { 131, "10_10_10_2Float", 32, 1, 0 }, { 132, "9_9_9_5Float", 32, 1, R9G9B9E5_SHAREDEXP },
            { 133, "5_6_5UNorm", 16, 1, B5G6R5_UNORM }, { 134, "5_5_5_1UNorm", 16, 1, B5G5R5A1_UNORM },
            { 135, "1_5_5_5UNorm", 16, 1, 0 }, { 136, "4_4_4_4UNorm", 16, 1, B4G4R4A4_UNORM },
            { 137, "4_4UNorm", 8, 1, 0 }, { 140, "32FloatClamp", 32, 1, R32_FLOAT },
            { 169, "Bc1UNorm", 64, 4, BC1_UNORM }, { 170, "Bc1Srgb", 64, 4, BC1_UNORM_SRGB, true },
            { 171, "Bc2UNorm", 128, 4, BC2_UNORM }, { 172, "Bc2Srgb", 128, 4, BC2_UNORM_SRGB, true },
            { 173, "Bc3UNorm", 128, 4, BC3_UNORM }, { 174, "Bc3Srgb", 128, 4, BC3_UNORM_SRGB, true },
            { 175, "Bc4UNorm", 64, 4, BC4_UNORM }, { 176, "Bc4SNorm", 64, 4, BC4_SNORM },
            { 177, "Bc5UNorm", 128, 4, BC5_UNORM }, { 178, "Bc5SNorm", 128, 4, BC5_SNORM },
            { 179, "Bc6UFloat", 128, 4, BC6H_UF16 }, { 180, "Bc6SFloat", 128, 4, BC6H_SF16 },
            { 181, "Bc7UNorm", 128, 4, BC7_UNORM }, { 182, "Bc7Srgb", 128, 4, BC7_UNORM_SRGB, true },
        };

        for (const auto & e : table)
            if (e.m_Agc == f)
                return &e;

        return nullptr;
    }


    // Reverse lookup used when importing DDS files. sRGB and UNORM variants of
    // the same block encoding are bit identical, so they are considered
    // compatible.
    [[nodiscard]] inline bool DxgiCompatible(
        std::uint32_t agc_fmt,
        std::uint32_t dxgi_fmt
    ) noexcept
    {
        const FormatInfo * f = FindFormatInfo( agc_fmt );

        if (!f || !f->m_Dxgi)
            return false;

        if (f->m_Dxgi == dxgi_fmt)
            return true;

        auto family = [] ( std::uint32_t d ) -> std::uint32_t
        {
            switch (d)
            {
                case dxgi::BC1_UNORM_SRGB:
                    return dxgi::BC1_UNORM;

                case dxgi::BC2_UNORM_SRGB:
                    return dxgi::BC2_UNORM;

                case dxgi::BC3_UNORM_SRGB:
                    return dxgi::BC3_UNORM;

                case dxgi::BC7_UNORM_SRGB:
                    return dxgi::BC7_UNORM;

                case dxgi::R8G8B8A8_UNORM_SRGB:
                    return dxgi::R8G8B8A8_UNORM;

                case 70:
                case 71:
                    return dxgi::BC1_UNORM; // BC1_TYPELESS

                case 73:
                case 74:
                    return dxgi::BC2_UNORM;

                case 76:
                case 77:
                    return dxgi::BC3_UNORM;

                case 79:
                case 80:
                    return dxgi::BC4_UNORM;

                case 82:
                case 83:
                    return dxgi::BC5_UNORM;

                case 94:
                case 95:
                    return dxgi::BC6H_UF16;

                case 97:
                case 98:
                    return dxgi::BC7_UNORM;

                case 27:
                    return dxgi::R8G8B8A8_UNORM; // R8G8B8A8_TYPELESS

                default:
                    return d;
            }
        };

        return family( f->m_Dxgi ) == family( dxgi_fmt );
    }

    // T# ['Core::Texture', 32 bytes].

    struct TSharp
    {
        std::uint32_t m_W[8] {};

        [[nodiscard]] std::uint32_t Format() const noexcept
        {
            return (m_W[1] >> 20) & 0x1FF;
        }


        [[nodiscard]] std::uint32_t Width() const noexcept
        {
            return (((m_W[1] >> 30) & 3) | ((m_W[2] & 0xFFF) << 2)) + 1;
        }


        [[nodiscard]] std::uint32_t Height() const noexcept
        {
            return ((m_W[2] >> 14) & 0x3FFF) + 1;
        }


        [[nodiscard]] std::uint32_t Swizzle() const noexcept
        {
            return m_W[3] & 0xFFF;
        }


        [[nodiscard]] std::uint32_t BaseMip() const noexcept
        {
            return (m_W[3] >> 12) & 0xF;
        }


        [[nodiscard]] std::uint32_t LastMip() const noexcept
        {
            return (m_W[3] >> 16) & 0xF;
        }


        [[nodiscard]] std::uint32_t TileMode() const noexcept
        {
            return (m_W[3] >> 20) & 0x1F;
        }


        [[nodiscard]] std::uint32_t Type() const noexcept
        {
            return (m_W[3] >> 28) & 0xF;
        }


        [[nodiscard]] std::uint32_t DepthField() const noexcept
        {
            return m_W[4] & 0x1FFF;
        }


        [[nodiscard]] std::uint32_t BaseArray() const noexcept
        {
            return (m_W[4] >> 16) & 0x1FFF;
        }


        [[nodiscard]] std::uint32_t MaxMip() const noexcept
        {
            return (m_W[5] >> 4) & 0xF;
        }


        [[nodiscard]] bool Is3d() const noexcept
        {
            return Type() == 10;
        }


        [[nodiscard]] bool IsCube() const noexcept
        {
            return Type() == 11;
        }


        [[nodiscard]] bool IsArrayLike() const noexcept
        {
            const auto t = Type();
            return t == 11 || t == 12 || t == 13 || t == 15;
        }


        [[nodiscard]] std::uint32_t NumMips() const noexcept
        {
            const auto t = Type();

            if (t == 14 || t == 15)
                return 1;

            return LastMip() + 1; // Mip indices are absolute, base mip is '0' in assets.
        }


        // Array slices as the engine counts them.
        [[nodiscard]] std::uint32_t NumSlices() const noexcept
        {
            return IsArrayLike() ? DepthField() - BaseArray() + 1 : 1;
        }


        [[nodiscard]] std::uint32_t Depth() const noexcept
        {
            return Is3d() ? DepthField() + 1 : 1;
        }


        void SetFormat(
            std::uint32_t f
        ) noexcept
        {
            m_W[1] = (m_W[1] & ~(0x1FFu << 20)) | ((f & 0x1FF) << 20);
        }


        void SetSize(
            std::uint32_t wd,
            std::uint32_t ht
        ) noexcept
        {
            const std::uint32_t wm = wd - 1, hm = ht - 1;
            m_W[1] = (m_W[1] & ~(3u << 30)) | ((wm & 3) << 30);
            m_W[2] = (m_W[2] & ~0xFFFu) | ((wm >> 2) & 0xFFF);
            m_W[2] = (m_W[2] & ~(0x3FFFu << 14)) | ((hm & 0x3FFF) << 14);
        }


        void SetLastMip(
            std::uint32_t m
        ) noexcept
        {
            m_W[3] = (m_W[3] & ~(0xFu << 16)) | ((m & 0xF) << 16);
            m_W[5] = (m_W[5] & ~(0xFu << 4)) | ((m & 0xF) << 4);
        }


        void SetTileMode(
            std::uint32_t t
        ) noexcept
        {
            m_W[3] = (m_W[3] & ~(0x1Fu << 20)) | ((t & 0x1F) << 20);
        }


        [[nodiscard]] static TSharp From( ByteSpan b ) noexcept
        {
            TSharp t;

            if (b.size() >= 32)
                std::memcpy( t.m_W, b.data(), 32 );

            return t;
        }
    };

    [[nodiscard]] inline std::string SwizzleStr(
        std::uint32_t s
    )
    {
        static constexpr char channel_chars[] = "01??RGBA";
        std::string o;

        for (int i = 0; i < 4; ++i)
            o.push_back( channel_chars[(s >> (3 * i)) & 7] );

        return o;
    }

    // Surface layout

    struct MipLayout
    {
        std::uint32_t m_W = 0, m_H = 0; // Real size in elements.
        std::uint32_t m_Lw = 0, m_Lh = 0; // Layout size.
        std::uint32_t m_Pw = 0, m_Ph = 0; // Padded size in elements.
        std::uint64_t m_Offset = 0; // Byte offset within one slice.
        std::uint64_t m_Size = 0; // Bytes reserved for this mip.
        std::uint32_t m_TailX = 0, m_TailY = 0; // Element offset inside the tail block.
        bool m_InTail = false;
    };

    struct SurfaceLayout
    {
        std::uint32_t m_Tile = 0;
        std::uint32_t m_BpeLog2 = 0; // 'log2(bytes per element)'
        std::uint32_t m_BlockW = 1, m_BlockH = 1; // Elements per tile block.
        std::uint32_t m_BlockBytes = 256;
        std::uint32_t m_FirstTail = 0;
        std::uint32_t m_Slices = 1;
        std::uint64_t m_SliceSize = 0;
        std::uint64_t m_Total = 0;
        std::vector<MipLayout> m_Mips;

        // Precomputed per coordinate swizzle contributions inside a block.
        std::vector<std::uint32_t> m_Xs, m_Ys;

        [[nodiscard]] std::uint32_t Bpe() const noexcept
        {
            return 1u << m_BpeLog2;
        }


        // Byte offset of element [x, y] of mip 'm' in slice 's'. Coordinates are in
        // elements and must be inside the true mip size.
        [[nodiscard]] std::uint64_t ElementOffset(
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t m,
            std::uint32_t s
        ) const noexcept
        {
            const MipLayout & mi = m_Mips[m];
            std::uint64_t base = s * m_SliceSize + mi.m_Offset;

            if (m_Tile == 0)
                return base + (static_cast<std::uint64_t>(y) * mi.m_Pw + x) * Bpe();

            x += mi.m_TailX;
            y += mi.m_TailY;

            const std::uint32_t bx = x / m_BlockW, by = y / m_BlockH;
            const std::uint64_t blk = static_cast<std::uint64_t>(by) * (mi.m_Pw / m_BlockW) + bx;

            return base + blk * m_BlockBytes + (m_Xs[x % m_BlockW] | m_Ys[y % m_BlockH]);
        }
    };

    // Upper bound for any surface we are willing to allocate.
    inline constexpr std::uint64_t MAX_SURFACE_BYTES = 2ull << 30;

    namespace detail
    {

        // Standard swizzle address bit assignment for the 256 byte micro tile,
        // indexed by 'log2(bytes per element)'. Values are address bit numbers.
        struct Micro
        {
            std::array<int, 4> m_X;
            int m_Nx;
            std::array<int, 4> m_Y;
            int m_Ny;
        };

        inline constexpr Micro MICRO[5] = {
            { { 0, 1, 2, 3 }, 4, { 4, 5, 6, 7 }, 4 },
            { { 1, 2, 3, 7 }, 4, { 4, 5, 6, 0 }, 3 },
            { { 2, 3, 7, 0 }, 3, { 4, 5, 6, 0 }, 3 },
            { { 3, 6, 7, 0 }, 3, { 4, 5, 0, 0 }, 2 },
            { { 6, 7, 0, 0 }, 2, { 4, 5, 0, 0 }, 2 },
        };

        // Mip tail element coordinates, by tile mode then 'log2(bpe)', per tail level.
        struct TailTable
        {
            std::uint32_t m_Tile, m_BpeLog2;
            std::array<std::array<std::uint16_t, 2>, 11> m_C;
            int m_N;
        };

        inline constexpr TailTable TAIL[] =
        {
            { 5, 0, { { { 32, 0 }, { 16, 32 }, { 0, 48 }, { 0, 32 }, { 16, 16 }, { 16, 0 }, { 0, 16 } } }, 7 },
            { 5, 1, { { { 32, 0 }, { 16, 16 }, { 0, 24 }, { 0, 16 }, { 16, 8 }, { 16, 0 }, { 0, 8 } } }, 7 },
            { 5, 2, { { { 16, 0 }, { 8, 16 }, { 0, 24 }, { 0, 16 }, { 8, 8 }, { 8, 0 }, { 0, 8 } } }, 7 },
            { 5, 3, { { { 16, 0 }, { 8, 8 }, { 0, 12 }, { 0, 8 }, { 8, 4 }, { 8, 0 }, { 0, 4 } } }, 7 },
            { 5, 4, { { { 8, 0 }, { 4, 8 }, { 0, 12 }, { 0, 8 }, { 4, 4 }, { 4, 0 }, { 0, 4 } } }, 7 },
            { 9, 0, { { { 128, 0 }, { 0, 128 }, { 64, 0 }, { 0, 64 }, { 32, 0 }, { 16, 32 }, { 0, 48 }, { 0, 32 }, { 16, 16 }, { 16, 0 }, { 0, 16 } } }, 11 },
            { 9, 1, { { { 128, 0 }, { 0, 64 }, { 64, 0 }, { 0, 32 }, { 32, 0 }, { 16, 16 }, { 0, 24 }, { 0, 16 }, { 16, 8 }, { 16, 0 }, { 0, 8 } } }, 11 },
            { 9, 2, { { { 64, 0 }, { 0, 64 }, { 32, 0 }, { 0, 32 }, { 16, 0 }, { 8, 16 }, { 0, 24 }, { 0, 16 }, { 8, 8 }, { 8, 0 }, { 0, 8 } } }, 11 },
            { 9, 3, { { { 64, 0 }, { 0, 32 }, { 32, 0 }, { 0, 16 }, { 16, 0 }, { 8, 8 }, { 0, 12 }, { 0, 8 }, { 8, 4 }, { 8, 0 }, { 0, 4 } } }, 11 },
            { 9, 4, { { { 32, 0 }, { 0, 32 }, { 16, 0 }, { 0, 16 }, { 8, 0 }, { 4, 8 }, { 0, 12 }, { 0, 8 }, { 4, 4 }, { 4, 0 }, { 0, 4 } } }, 11 },
        };


        [[nodiscard]] inline std::uint32_t Swizzle(
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t bl2,
            std::uint32_t top_bit
        ) noexcept
        {
            const Micro & mc = MICRO[bl2];
            std::uint32_t a = 0;

            for (int i = 0; i < mc.m_Nx; ++i)
                if ((x >> i) & 1) a |= 1u << mc.m_X[i];

            for (int i = 0; i < mc.m_Ny; ++i)
                if ((y >> i) & 1) a |= 1u << mc.m_Y[i];

            int xi = mc.m_Nx, yi = mc.m_Ny;

            for (std::uint32_t b = 8; b < top_bit; ++b)
            {
                if ((b & 1) == 0)
                {
                    if ((y >> yi) & 1)
                        a |= 1u << b;

                    ++yi;
                }
                else
                {
                    if ((x >> xi) & 1)
                        a |= 1u << b;

                    ++xi;
                }
            }

            return a;
        }
    }


    // Compute the surface layout exactly as 'libSceAgcGpuAddress.dll' does for a 2D texture.
    [[nodiscard]] inline Result<SurfaceLayout> ComputeLayout(
        std::uint32_t tile,
        std::uint32_t bits_per_element,
        std::uint32_t block_texels,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t num_mips,
        std::uint32_t slices = 1
    )
    {
        if (bits_per_element == 0 || bits_per_element % 8 != 0 || !std::has_single_bit( bits_per_element / 8 ) ||
            bits_per_element > 128)
        {
            return Fail( Errc::UNSUPPORTED,
                std::format( "{} bit elements are not supported by the native detiler", bits_per_element ) );
        }

        if (tile != 0 && tile != 1 && tile != 5 && tile != 9)
        {
            return Fail( Errc::UNSUPPORTED, std::format( "tile mode {} ({}) is not supported by the native detiler",
                tile, TileModeName( tile ) ) );
        }

        if (num_mips == 0 || num_mips > 16 || width == 0 || height == 0)
        {
            return Fail( Errc::INCONSISTENT, "invalid texture dimensions" );
        }

        SurfaceLayout l;
        l.m_Tile = tile;
        l.m_BpeLog2 = static_cast<std::uint32_t>(std::countr_zero( bits_per_element / 8 ));
        l.m_Slices = std::max( 1u, slices );

        const std::uint32_t bpe = 1u << l.m_BpeLog2;
        std::uint32_t top_bit = 8;

        if (tile == 0)
        {
            l.m_BlockW = 256 / bpe; // Pitch alignment in elements.
            l.m_BlockH = 1;
            l.m_BlockBytes = 256;
        }
        else
        {
            top_bit = tile == 1 ? 8 : tile == 5 ? 12 : 16;
            l.m_BlockBytes = 1u << top_bit;

            const std::uint32_t elems_log2 = top_bit - l.m_BpeLog2;

            l.m_BlockW = 1u << ((elems_log2 + 1) / 2);
            l.m_BlockH = 1u << (elems_log2 / 2);
            l.m_Xs.resize( l.m_BlockW );
            l.m_Ys.resize( l.m_BlockH );

            for (std::uint32_t x = 0; x < l.m_BlockW; ++x)
                l.m_Xs[x] = detail::Swizzle( x, 0, l.m_BpeLog2, top_bit );

            for (std::uint32_t y = 0; y < l.m_BlockH; ++y)
                l.m_Ys[y] = detail::Swizzle( 0, y, l.m_BpeLog2, top_bit );
        }

        l.m_Mips.resize( num_mips );

        std::uint32_t lw = (width + block_texels - 1) / block_texels;
        std::uint32_t lh = (height + block_texels - 1) / block_texels;

        for (std::uint32_t m = 0; m < num_mips; ++m)
        {
            auto & mi = l.m_Mips[m];

            const std::uint32_t tw = std::max( 1u, width >> m ), th = std::max( 1u, height >> m );
            mi.m_W = (tw + block_texels - 1) / block_texels;
            mi.m_H = (th + block_texels - 1) / block_texels;
            mi.m_Lw = lw;
            mi.m_Lh = lh;
            lw = (lw + 1) / 2;
            lh = (lh + 1) / 2;
        }

        l.m_FirstTail = num_mips;

        if (tile == 5 || tile == 9)
        {
            for (std::uint32_t m = 0; m < num_mips; ++m)
            {
                if (l.m_Mips[m].m_Lw <= l.m_BlockW / 2 && l.m_Mips[m].m_Lh <= l.m_BlockH)
                {
                    l.m_FirstTail = m;
                    break;
                }
            }
        }

        std::uint64_t off = 0;
        if (l.m_FirstTail < num_mips)
        {
            const detail::TailTable * tt = nullptr;

            for (const auto & t : detail::TAIL)
                if (t.m_Tile == tile && t.m_BpeLog2 == l.m_BpeLog2) tt = &t;

            for (std::uint32_t m = l.m_FirstTail; m < num_mips; ++m)
            {
                const std::uint32_t k = m - l.m_FirstTail;

                if (!tt || static_cast<int>( k ) >= tt->m_N)
                {
                    return Fail( Errc::UNSUPPORTED, "mip tail deeper than the known tail table" );
                }

                auto & mi = l.m_Mips[m];
                mi.m_InTail = true;
                mi.m_Pw = l.m_BlockW;
                mi.m_Ph = l.m_BlockH;
                mi.m_Offset = 0;
                mi.m_Size = l.m_BlockBytes;
                mi.m_TailX = tt->m_C[k][0];
                mi.m_TailY = tt->m_C[k][1];
            }

            off = l.m_BlockBytes;
        }

        for (int m = static_cast<int>(l.m_FirstTail) - 1; m >= 0; --m)
        {
            auto & mi = l.m_Mips[static_cast<std::size_t>(m)];

            mi.m_Pw = static_cast<std::uint32_t>(AlignUp( mi.m_Lw, l.m_BlockW ));
            mi.m_Ph = static_cast<std::uint32_t>(AlignUp( mi.m_Lh, l.m_BlockH ));
            mi.m_Offset = off;
            mi.m_Size = static_cast<std::uint64_t>(mi.m_Pw) * mi.m_Ph * bpe;
            off += mi.m_Size;
        }

        l.m_SliceSize = off;
        l.m_Total = off * l.m_Slices;

        if (l.m_Total > MAX_SURFACE_BYTES)
        {
            return Fail( Errc::INCONSISTENT, std::format( "surface of {} bytes is implausibly large", l.m_Total ) );
        }

        return l;
    }


    // Layout for a texture described by a T#. Pass 'force_linear' to model the
    // engine's handling of WAD low mip chunks.
    [[nodiscard]] inline Result<SurfaceLayout> LayoutFor(
        const TSharp & t,
        bool force_linear = false
    )
    {
        const FormatInfo * f = FindFormatInfo( t.Format() );

        if (!f)
            return Fail( Errc::UNSUPPORTED, std::format( "unknown texture format {}", t.Format() ) );

        if (t.Is3d())
        {
            // Volume textures are only modelled when linear.
            if ((force_linear || t.TileMode() == 0) && t.NumMips() == 1)
            {
                SMPACK_TRY( L, ComputeLayout( 0, f->m_Bits, f->m_Block, t.Width(), t.Height(), 1, t.Depth() ) );

                const std::uint64_t padded = L.m_Total;

                auto & m = L.m_Mips[0];
                m.m_Pw = m.m_Lw;
                m.m_Ph = m.m_Lh;
                m.m_Size = static_cast<std::uint64_t>(m.m_Pw) * m.m_Ph * L.Bpe();
                L.m_SliceSize = m.m_Size;
                L.m_Total = std::max( padded, L.m_SliceSize * L.m_Slices );
                return L;
            }

            return Fail( Errc::UNSUPPORTED, "tiled/mipmapped 3D textures need Sony's detiler [--sce-dll <game dir>]" );
        }

        if (t.Type() == 14 || t.Type() == 15)
            return Fail( Errc::UNSUPPORTED, "MSAA textures are not supported" );

        SMPACK_TRY( L, ComputeLayout( force_linear ? 0 : t.TileMode(), f->m_Bits, f->m_Block, t.Width(), t.Height(),
            t.NumMips(), t.NumSlices() ) );

        if (force_linear)
        {
            // WAD low mips. Each mip keeps its 256 byte pitched allocation (offsets and sizes as computed),
            // but the rows inside it are packed tightly as the D3D12 upload layout.
            for (auto & m : L.m_Mips)
                m.m_Pw = m.m_W;
        }

        return L;
    }


    // Tight DDS order layout. For each slice, for each mip, rows of elements.
    struct LinearImage
    {
        std::uint32_t m_Bpe = 0;
        std::uint32_t m_Slices = 1;

        struct Mip
        {
            std::uint32_t m_W, m_H; // Elements.
            std::uint64_t m_Offset, m_Size;
        };

        std::vector<Mip> m_Mips; // Per mip. Offsets relative to slice start.
        std::uint64_t m_SliceSize = 0;

        [[nodiscard]] std::uint64_t Total() const noexcept
        {
            return m_SliceSize * m_Slices;
        }
    };


    [[nodiscard]] inline LinearImage LinearImageFor(
        const SurfaceLayout & l 
    )
    {
        LinearImage img;
        img.m_Bpe = l.Bpe();
        img.m_Slices = l.m_Slices;
        std::uint64_t off = 0;

        for (const auto & m : l.m_Mips)
        {
            const std::uint64_t sz = static_cast<std::uint64_t>(m.m_W) * m.m_H * img.m_Bpe;

            img.m_Mips.push_back( { m.m_W, m.m_H, off, sz } );
            off += sz;
        }

        img.m_SliceSize = off;
        return img;
    }


    // Detile a GPU surface into tight DDS order. 'mips' limits output to the
    // first N mips [0 = all]. Missing source bytes read as zero instead of faulting.
    [[nodiscard]] inline Result<Bytes> Detile( 
        const SurfaceLayout & l, 
        ByteSpan src
    )
    {
        const LinearImage img = LinearImageFor( l );
        Bytes out( static_cast<std::size_t>(img.Total()) );
        const std::uint32_t bpe = l.Bpe();

        for (std::uint32_t s = 0; s < l.m_Slices; ++s)
        {
            for (std::uint32_t m = 0; m < l.m_Mips.size(); ++m)
            {
                const auto & mi = l.m_Mips[m];
                std::byte * dst = out.data() + s * img.m_SliceSize + img.m_Mips[m].m_Offset;

                for (std::uint32_t y = 0; y < mi.m_H; ++y)
                {
                    if (l.m_Tile == 0)
                    {
                        const std::uint64_t so = l.ElementOffset( 0, y, m, s );
                        const std::uint64_t n = static_cast<std::uint64_t>( mi.m_W ) * bpe;

                        if (so + n <= src.size())
                            std::memcpy( dst + static_cast<std::uint64_t>( y ) * n, src.data() + so, n );
                        continue;
                    }
                    for (std::uint32_t x = 0; x < mi.m_W; ++x)
                    {
                        const std::uint64_t so = l.ElementOffset( x, y, m, s );

                        if (so + bpe <= src.size())
                            std::memcpy( dst + (static_cast<std::uint64_t>( y ) * mi.m_W + x) * bpe, src.data() + so, bpe );
                    }
                }
            }
        }
        return out;
    }


    // Inverse of 'detile'. Scatter tight DDS order data into a GPU surface of
    // 'L.total' bytes. Padding bytes are zero.
    [[nodiscard]] inline Result<Bytes> Tile(
        const SurfaceLayout & l,
        ByteSpan linear
    )
    {
        const LinearImage img = LinearImageFor( l );

        if (linear.size() < img.Total())
        {
            return Fail( Errc::TRUNCATED, std::format( "pixel data is {} bytes, expected {}", linear.size(), img.Total() ) );
        }

        Bytes out( static_cast<std::size_t>(l.m_Total) );
        const std::uint32_t bpe = l.Bpe();

        for (std::uint32_t s = 0; s < l.m_Slices; ++s)
        {
            for (std::uint32_t m = 0; m < l.m_Mips.size(); ++m)
            {
                const auto & mi = l.m_Mips[m];
                const std::byte * srcp = linear.data() + s * img.m_SliceSize + img.m_Mips[m].m_Offset;

                for (std::uint32_t y = 0; y < mi.m_H; ++y)
                {
                    if (l.m_Tile == 0)
                    {
                        const std::uint64_t n = static_cast<std::uint64_t>( mi.m_W ) * bpe;
                        std::memcpy( out.data() + l.ElementOffset( 0, y, m, s ), srcp + y * n, n );
                        continue;
                    }

                    for (std::uint32_t x = 0; x < mi.m_W; ++x)
                    {
                        std::memcpy( out.data() + l.ElementOffset( x, y, m, s ),
                            srcp + (static_cast<std::uint64_t>( y ) * mi.m_W + x) * bpe, bpe );
                    }
                }
            }
        }

        return out;
    }

    // Byte range [begin, end] covered by the 'count' smallest mips of every
    // slice. For a single slice surface this is where texpack stream blocks are memcpy'd to.
    [[nodiscard]] inline std::pair<std::uint64_t, std::uint64_t> LowMipsRange( 
        const SurfaceLayout & l,
        std::uint32_t count
    )
    {
        const std::uint32_t n = static_cast<std::uint32_t>(l.m_Mips.size());
        const std::uint32_t first = count >= n ? 0 : n - count;
        std::uint64_t b = UINT64_MAX, e = 0;

        for (std::uint32_t s = 0; s < l.m_Slices; ++s)
        {
            for (std::uint32_t m = first; m < n; ++m)
            {
                const auto o = s * l.m_SliceSize + l.m_Mips[m].m_Offset;

                b = std::min( b, o );
                e = std::max( e, o + l.m_Mips[m].m_Size );
            }
        }

        if (b == UINT64_MAX)
            b = 0;

        return { b, e };
    }
}