// DDS read and write [DX10 extended header on write, legacy FourCC accepted on read].

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "smpack/agc.hpp"
#include "smpack/reader.hpp"

namespace smpack::dds
{

    inline constexpr std::uint32_t MAGIC = 0x20534444;  // 'DDS '

    struct PixelFormat
    {
        std::uint32_t m_Size = 32, m_Flags = 0, m_Fourcc = 0, m_RgbBits = 0, m_R = 0, m_G = 0, m_B = 0, m_A = 0;
    };

    struct Header
    {
        std::uint32_t m_Size = 124, m_Flags = 0, m_Height = 0, m_Width = 0, m_Pitch = 0, m_Depth = 0, m_Mips = 0;
        std::uint32_t m_Reserved1[11] {};
        PixelFormat m_Pf;
        std::uint32_t m_Caps = 0, m_Caps2 = 0, m_Caps3 = 0, m_Caps4 = 0, m_Reserved2 = 0;
    };

    struct HeaderDX10
    {
        std::uint32_t m_Dxgi = 0, m_Dimension = 3, m_Misc = 0, m_ArraySize = 1, m_Misc2 = 0;
    };

    constexpr std::uint32_t Fourcc(
        char a,
        char b,
        char c,
        char d
    )
    {
        return std::uint32_t( std::uint8_t( a ) ) | std::uint32_t( std::uint8_t( b ) ) << 8 |
            std::uint32_t( std::uint8_t( c ) ) << 16 | std::uint32_t( std::uint8_t( d ) ) << 24;
    }

    struct Image
    {
        std::uint32_t m_Dxgi = 0;
        std::uint32_t m_Width = 0, m_Height = 0, m_Mips = 1, m_Slices = 1;
        std::uint32_t m_Depth = 1; // > 1 for volume [3D] textures, then slices == 1.
        bool m_Cube = false;
        Bytes m_Data; // Tight pixel data.
    };

    [[nodiscard]] inline std::uint32_t BitsPerElement(
        std::uint32_t dxgi,
        std::uint32_t * block = nullptr
    )
    {
        using namespace agc::dxgi;

        if (block)
            *block = 1;
        switch (dxgi)
        {
            case 70:
            case BC1_UNORM:
            case BC1_UNORM_SRGB:
            case 79:
            case BC4_UNORM:
            case BC4_SNORM:
                if (block)
                    *block = 4;

                return 64;

            case 73:
            case BC2_UNORM:
            case BC2_UNORM_SRGB:
            case 76:
            case BC3_UNORM:
            case BC3_UNORM_SRGB:
            case 82:
            case BC5_UNORM:
            case BC5_SNORM:
            case 94:
            case BC6H_UF16:
            case BC6H_SF16:
            case 97:
            case BC7_UNORM:
            case BC7_UNORM_SRGB:
                if (block)
                    *block = 4;
                return 128;

            case 1:
            case 2:
            case 3:
            case 4:
                return 128;

            case 5:
            case 6:
            case 7:
            case 8:
                return 96;

            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
            case 17:
            case 18:
                return 64;

            case 19:
            case 20:
            case 21:
            case 22:
                return 64;

            case 23:
            case 24:
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
            case 32:
            case 33:
            case 34:
            case 35:
            case 36:
            case 37:
            case 38:
            case 39:
            case 40:
            case 41:
            case 42:
            case 43:
            case 44:
            case 45:
            case 46:
            case 47:
            case 67:
            case 87:
            case 88:
            case 89:
            case 90:
            case 91:
            case 92:
            case 93:
                return 32;

            case 48:
            case 49:
            case 50:
            case 51:
            case 52:
            case 53:
            case 54:
            case 55:
            case 56:
            case 57:
            case 58:
            case 59:
            case 85:
            case 86:
            case 115:
                return 16;

            case 60:
            case 61:
            case 62:
            case 63:
            case 64:
            case 65:
                return 8;

            default:
                return 0;
        }
    }


    [[nodiscard]] inline std::uint64_t TightSize(
        std::uint32_t dxgi,
        std::uint32_t w,
        std::uint32_t h,
        std::uint32_t mips,
        std::uint32_t slices,
        std::uint32_t depth = 1
    )
    {
        std::uint32_t blk = 1;
        const std::uint32_t bits = BitsPerElement( dxgi, &blk );
        std::uint64_t per = 0;

        for (std::uint32_t m = 0; m < mips; ++m)
        {
            const std::uint64_t ew = (std::max( 1u, w >> m ) + blk - 1) / blk;
            const std::uint64_t eh = (std::max( 1u, h >> m ) + blk - 1) / blk;
            per += ew * eh * bits / 8 * std::max( 1u, depth >> m );
        }

        return per * slices;
    }


    [[nodiscard]] inline Bytes Write(
        const Image & img
    )
    {
        ByteWriter w;
        w.Put( MAGIC );

        Header h;
        h.m_Flags = 0x1 | 0x2 | 0x4 | 0x1000; // CAPS|HEIGHT|WIDTH|PIXELFORMAT

        if (img.m_Mips > 1)
            h.m_Flags |= 0x20000;

        std::uint32_t blk = 1;
        const auto bits = BitsPerElement( img.m_Dxgi, &blk );

        if (blk > 1)
        {
            h.m_Flags |= 0x80000; // LINEARSIZE
            h.m_Pitch = static_cast<std::uint32_t>(((img.m_Width + 3) / 4) * ((img.m_Height + 3) / 4) * bits / 8);
        }
        else
        {
            h.m_Flags |= 0x8; // PITCH
            h.m_Pitch = (img.m_Width * bits + 7) / 8;
        }

        h.m_Width = img.m_Width;
        h.m_Height = img.m_Height;
        h.m_Mips = img.m_Mips;
        h.m_Pf.m_Flags = 0x4;  // FOURCC
        h.m_Pf.m_Fourcc = Fourcc( 'D', 'X', '1', '0' );
        h.m_Caps = 0x1000 | (img.m_Mips > 1 ? 0x400008u : 0u);

        if (img.m_Cube)
            h.m_Caps2 = 0xFE00, h.m_Caps |= 0x8;

        if (img.m_Depth > 1)
        {
            h.m_Flags |= 0x800000; // DEPTH
            h.m_Depth = img.m_Depth;
            h.m_Caps |= 0x8; // COMPLEX
            h.m_Caps2 |= 0x200000; // VOLUME
        }

        w.Put( h );
        HeaderDX10 x;
        x.m_Dxgi = img.m_Dxgi;
        x.m_Dimension = img.m_Depth > 1 ? 4 : 3; // TEXTURE3D, TEXTURE2D
        x.m_Misc = img.m_Cube ? 0x4 : 0;
        x.m_ArraySize = img.m_Depth > 1 ? 1 : img.m_Cube ? std::max( 1u, img.m_Slices / 6 ) : img.m_Slices;
        w.Put( x );
        w.Put( img.m_Data );
        return w.Take();
    }


    [[nodiscard]] inline Result<Image> Read(
        ByteSpan b
    )
    {
        ByteReader r( b );
        SMPACK_TRY( magic, r.At<std::uint32_t>( 0 ) );

        if (magic != MAGIC)
            return Fail( Errc::BAD_MAGIC, "not a DDS file" );

        SMPACK_TRY( h, r.At<Header>( 4 ) );

        Image img;
        img.m_Width = h.m_Width;
        img.m_Height = h.m_Height;
        img.m_Mips = std::max( 1u, (h.m_Flags & 0x20000) ? h.m_Mips : 1u );

        std::size_t data_off = 4 + sizeof( Header );

        if (h.m_Caps2 & 0x200)
            img.m_Cube = true;

        if ((h.m_Pf.m_Flags & 0x4) && h.m_Pf.m_Fourcc == Fourcc( 'D', 'X', '1', '0' ))
        {
            SMPACK_TRY( x, r.At<HeaderDX10>( data_off ) );
            data_off += sizeof( HeaderDX10 );
            img.m_Dxgi = x.m_Dxgi;
            img.m_Slices = std::max( 1u, x.m_ArraySize );
            if (x.m_Misc & 0x4)
            {
                img.m_Cube = true;
                img.m_Slices *= 6;
            }
            if (x.m_Dimension == 4)
            {
                img.m_Slices = 1;
                img.m_Depth = std::max( 1u, h.m_Depth );
            }
        }
        else if (h.m_Pf.m_Flags & 0x4)
        {
            using namespace agc::dxgi;
            const auto f = h.m_Pf.m_Fourcc;

            if (f == Fourcc( 'D', 'X', 'T', '1' ))
                img.m_Dxgi = BC1_UNORM;
            else if (f == Fourcc( 'D', 'X', 'T', '2' ) || f == Fourcc( 'D', 'X', 'T', '3' ))
                img.m_Dxgi = BC2_UNORM;
            else if (f == Fourcc( 'D', 'X', 'T', '4' ) || f == Fourcc( 'D', 'X', 'T', '5' ))
                img.m_Dxgi = BC3_UNORM;
            else if (f == Fourcc( 'A', 'T', 'I', '1' ) || f == Fourcc( 'B', 'C', '4', 'U' ))
                img.m_Dxgi = BC4_UNORM;
            else if (f == Fourcc( 'B', 'C', '4', 'S' ))
                img.m_Dxgi = BC4_SNORM;
            else if (f == Fourcc( 'A', 'T', 'I', '2' ) || f == Fourcc( 'B', 'C', '5', 'U' ))
                img.m_Dxgi = BC5_UNORM;
            else if (f == Fourcc( 'B', 'C', '5', 'S' ))
                img.m_Dxgi = BC5_SNORM;
            else if (f == 113)
                img.m_Dxgi = R16G16B16A16_FLOAT;
            else if (f == 36)
                img.m_Dxgi = R16G16B16A16_UNORM;
            else if (f == 116)
                img.m_Dxgi = R32G32B32A32_FLOAT;
            else
                return Fail( Errc::UNSUPPORTED, std::format( "unsupported DDS FourCC {:#x}", f ) );

            if (img.m_Cube)
                img.m_Slices = 6;
        }
        else if ((h.m_Pf.m_Flags & 0x40) && h.m_Pf.m_RgbBits == 32)
        {
            // RGB(A) 32-bit
            using namespace agc::dxgi;

            if (h.m_Pf.m_R == 0xFF && h.m_Pf.m_G == 0xFF00 && h.m_Pf.m_B == 0xFF0000)
                img.m_Dxgi = R8G8B8A8_UNORM;
            else if (h.m_Pf.m_R == 0xFF0000 && h.m_Pf.m_G == 0xFF00 && h.m_Pf.m_B == 0xFF)
                img.m_Dxgi = 87; // B8G8R8A8
            else
                return Fail( Errc::UNSUPPORTED, "unsupported uncompressed DDS channel layout" );
        }
        else if ((h.m_Pf.m_Flags & 0x20000) && h.m_Pf.m_RgbBits == 8)
        {
            img.m_Dxgi = agc::dxgi::R8_UNORM;
        }
        else
        {
            return Fail( Errc::UNSUPPORTED, "unsupported DDS pixel format" );
        }

        if (BitsPerElement( img.m_Dxgi ) == 0)
        {
            return Fail( Errc::UNSUPPORTED, std::format( "unsupported DXGI format {}", img.m_Dxgi ) );
        }

        if (!(h.m_Pf.m_Fourcc == Fourcc( 'D', 'X', '1', '0' )) && (h.m_Caps2 & 0x200000) && (h.m_Flags & 0x800000))
            img.m_Depth = std::max( 1u, h.m_Depth );

        const std::uint64_t need = TightSize( img.m_Dxgi, img.m_Width, img.m_Height, img.m_Mips, img.m_Slices, img.m_Depth );
        SMPACK_TRY( px, r.Bytes( data_off, need ) );

        img.m_Data.assign( px.begin(), px.end() );
        return img;
    }


    // Convert B8G8R8A8 to/from R8G8B8A8 in place.
    inline void SwapRb(
        Bytes & d
    )
    {
        for (std::size_t i = 0; i + 3 < d.size(); i += 4)
            std::swap( d[i], d[i + 2] );
    }
}