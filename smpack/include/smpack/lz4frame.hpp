// LZ4 frame container support.

#pragma once

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "smpack/reader.hpp"

#include "lz4frame.h"
#include "lz4hc.h"

namespace smpack::lz4
{
    inline constexpr std::uint32_t FRAME_MAGIC = 0x184D2204u;

    [[nodiscard]] constexpr bool IsSkippableMagic( 
        std::uint32_t m
    ) noexcept
    {
        return (m & 0xFFFF'FFF0u) == 0x184D'2A50u;
    }


    [[nodiscard]] inline bool LooksLikeFrame(
        ByteSpan d
    ) noexcept
    {
        if (d.size() < 4)
            return false;

        std::uint32_t m {};
        std::memcpy( &m, d.data(), 4 );
        return m == FRAME_MAGIC || IsSkippableMagic( m );
    }

    struct FrameInfo
    {
        bool m_HasContentSize {};
        std::uint64_t m_ContentSize {};
        std::uint32_t m_BlockMaxSize {};
        bool m_BlockIndependent {};
        bool m_BlockChecksum {};
        bool m_ContentChecksum {};
        std::size_t m_HeaderSize {};
    };

    [[nodiscard]] inline Result<FrameInfo> ParseFrameHeader(
        ByteSpan d
    )
    {
        if (d.size() < 7)
            return Fail( Errc::TRUNCATED, "LZ4 frame header truncated" );

        std::uint32_t magic {};
        std::memcpy( &magic, d.data(), 4 );

        if (magic != FRAME_MAGIC)
            return Fail( Errc::BAD_MAGIC, "not an LZ4 frame" );

        const auto flg = static_cast<std::uint8_t>(d[4]);
        const auto bd = static_cast<std::uint8_t>(d[5]);
        if (((flg >> 6) & 0x3u) != 1u)
        {
            return Fail( Errc::UNSUPPORTED, std::format( "LZ4 frame version {} unsupported", (flg >> 6) & 3u ) );
        }

        FrameInfo fi;
        fi.m_BlockIndependent = ((flg >> 5) & 1u) != 0;
        fi.m_BlockChecksum = ((flg >> 4) & 1u) != 0;
        fi.m_HasContentSize = ((flg >> 3) & 1u) != 0;
        fi.m_ContentChecksum = ((flg >> 2) & 1u) != 0;

        const bool dict_id = (flg & 1u) != 0;
        constexpr std::uint32_t block_max[8] = { 0, 0, 0, 0, 64u * 1024, 256u * 1024, 1024u * 1024, 4096u * 1024 };
        fi.m_BlockMaxSize = block_max[(bd >> 4) & 0x7u];

        std::size_t off = 6;
        if (fi.m_HasContentSize)
        {
            if (d.size() < off + 8)
                return Fail( Errc::TRUNCATED, "LZ4 content size truncated" );

            std::memcpy( &fi.m_ContentSize, d.data() + off, 8 );
            off += 8;
        }

        if (dict_id)
            off += 4;

        off += 1;

        if (d.size() < off)
            return Fail( Errc::TRUNCATED, "LZ4 frame header truncated" );

        fi.m_HeaderSize = off;
        return fi;
    }


    // Decompress a whole LZ4 frame [or a sequence of concatenated frames].
    [[nodiscard]] inline Result<Bytes> DecompressFrame(
        ByteSpan in
    )
    {
        auto fi = ParseFrameHeader( in );

        if (!fi)
            return std::unexpected( fi.error() );

        LZ4F_dctx * ctx = nullptr;
        if (LZ4F_isError( LZ4F_createDecompressionContext( &ctx, LZ4F_VERSION ) ))
        {
            return Fail( Errc::IO_FAILURE, "LZ4F_createDecompressionContext failed" );
        }

        struct CtxGuard
        {
            LZ4F_dctx * m_C;

            ~CtxGuard()
            {
                LZ4F_freeDecompressionContext( m_C );
            }
        }

        guard { ctx };

        Bytes out;

        if (fi->m_HasContentSize)
        {
            if (fi->m_ContentSize > (1ull << 40))
            {
                return Fail( Errc::INCONSISTENT, "LZ4 declared content size is implausible" );
            }
            out.resize( static_cast<std::size_t>(fi->m_ContentSize) );
        }
        else
        {
            out.resize( in.size() * 3 + 65536 );
        }

        std::size_t out_pos = 0;
        const std::byte * src = in.data();
        std::size_t remaining = in.size();

        while (remaining > 0)
        {
            if (out_pos == out.size())
                out.resize( out.size() * 2 + 65536 );

            std::size_t dst_size = out.size() - out_pos;
            std::size_t src_size = remaining;

            const std::size_t hint = LZ4F_decompress( ctx, out.data() + out_pos, &dst_size, src, &src_size, nullptr );

            if (LZ4F_isError( hint ))
            {
                return Fail( Errc::INCONSISTENT, std::format( "LZ4F_decompress: {}", LZ4F_getErrorName( hint ) ) );
            }

            out_pos += dst_size;
            src += src_size;
            remaining -= src_size;

            if (hint == 0)
            {
                // Frame complete. Continue only if another frame follows.
                if (remaining < 4 || !LooksLikeFrame( { src, remaining } ))
                    break;
            }

            if (src_size == 0 && dst_size == 0)
                break;
        }

        out.resize( out_pos );

        if (fi->m_HasContentSize && out_pos < fi->m_ContentSize)
        {
            return Fail( Errc::INCONSISTENT, std::format( "LZ4 frame declared {} bytes but produced {}",
                fi->m_ContentSize, out_pos ) );
        }
        return out;
    }


    // Compress into an LZ4 frame using the same parameters as shipped files.
    // '0' = fast LZ4, '3-12' = LZ4HC.
    [[nodiscard]] inline Result<Bytes> CompressFrame( 
        ByteSpan in,
        int level = 0
    )
    {
        LZ4F_preferences_t prefs {};
        prefs.frameInfo.blockSizeID = LZ4F_max64KB;
        prefs.frameInfo.blockMode = LZ4F_blockIndependent;
        prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
        prefs.frameInfo.blockChecksumFlag = LZ4F_noBlockChecksum;
        prefs.frameInfo.frameType = LZ4F_frame;
        prefs.frameInfo.contentSize = in.size();
        prefs.compressionLevel = level;

        const std::size_t bound = LZ4F_compressFrameBound( in.size(), &prefs );

        Bytes out( bound );
        const std::size_t n = LZ4F_compressFrame( out.data(), out.size(), in.data(), in.size(), &prefs );

        if (LZ4F_isError( n ))
        {
            return Fail( Errc::IO_FAILURE, std::format( "LZ4F_compressFrame: {}", LZ4F_getErrorName( n ) ) );
        }

        out.resize( n );
        return out;
    }


    // Transparently unwrap an LZ4 frame. Nonframe input is returned as is.
    [[nodiscard]] inline Result<Bytes> MaybeDecompress( Bytes data )
    {
        if (!LooksLikeFrame( data ))
            return data;

        return DecompressFrame( data );
    }
}