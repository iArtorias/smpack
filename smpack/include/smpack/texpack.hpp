// '.texpack', '.texpack.toc', streamed high resolution texture mips.

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "smpack/agc.hpp"
#include "smpack/formats.hpp"
#include "smpack/reader.hpp"

namespace smpack::texpack
{
    struct Block
    {
        std::uint64_t m_TocOff {}; // Where its 'StreamBlockFileInfo' lives.
        StreamBlockFileInfo m_Info {};

        [[nodiscard]] std::uint64_t Offset() const noexcept
        {
            return PayloadOffset( m_Info );
        }


        [[nodiscard]] std::uint32_t MipCount() const noexcept
        {
            return m_Info.m_LargestMipIndex - m_Info.m_SmallestMipIndex + 1u;
        }
    };

    struct Texture
    {
        std::uint64_t m_TexIdentifier {};
        std::uint64_t m_ContentHash {};
        std::vector<Block> m_Mips; // Chain order. Smallest first.

        [[nodiscard]] const Block & Top() const
        {
            return m_Mips.back();
        }


        [[nodiscard]] std::uint32_t MipCount() const
        {
            return m_Mips.empty() ? 0 : Top().m_Info.m_LargestMipIndex + 1u;
        }


        [[nodiscard]] std::uint32_t Width() const
        {
            return m_Mips.empty() ? 0 : Top().m_Info.m_LargestMipWidth;
        }


        [[nodiscard]] std::uint32_t Height() const
        {
            return m_Mips.empty() ? 0 : Top().m_Info.m_LargestMipHeight;
        }


        [[nodiscard]] std::uint64_t MemBytes() const
        {
            std::uint64_t s = 0;
            for (const auto & b : m_Mips) s += b.m_Info.m_MemSizeBytes;
            return s;
        }


        [[nodiscard]] std::uint64_t DiskBytes() const
        {
            std::uint64_t s = 0;

            for (const auto & b : m_Mips)
                s += b.m_Info.m_DiskSizeBytes;

            return s;
        }
    };

    struct Package
    {
        Header m_Header {};
        std::string m_Json;
        std::vector<Texture> m_Textures;
    };

    [[nodiscard]] inline Result<std::vector<Block>> WalkMipChain(
        const ByteReader & toc,
        std::uint64_t start
    )
    {
        constexpr std::size_t max_mips = 32;

        std::vector<Block> out;
        std::unordered_set<std::uint64_t> seen;
        std::uint64_t cur = start;

        while (cur != NULL_OFFSET)
        {
            if (!seen.insert( cur ).second)
                return Fail( Errc::INCONSISTENT, std::format( "mip chain cycles at {:#x}", cur ) );

            if (out.size() >= max_mips)
                return Fail( Errc::INCONSISTENT, "mip chain too long" );

            SMPACK_TRY( blk, toc.At<StreamBlockFileInfo>( cur ) );

            out.push_back( Block { cur, blk } );
            cur = blk.m_NextMipOff;
        }

        return out;
    }


    // Parse the table region [a whole .texpack.toc, or the prefix of a .texpack].
    [[nodiscard]] inline Result<Package> Parse( ByteSpan toc_span )
    {
        ByteReader toc { toc_span };
        SMPACK_TRY( hdr, toc.At<Header>( 0 ) );

        if (hdr.m_Version != EXPECTED_VERSION)
        {
            return Fail( Errc::BAD_VERSION,
                std::format( "texpack version {} unsupported (engine accepts only {})", hdr.m_Version, EXPECTED_VERSION ) );
        }

        if (hdr.m_TocSizeBytes < sizeof( Header ) || hdr.m_TocSizeBytes > toc.Size())
        {
            return Fail( Errc::INCONSISTENT, std::format( "toc_size_bytes {:#x} inconsistent with data size {:#x}",
                hdr.m_TocSizeBytes, toc.Size() ) );
        }

        Package pkg;
        pkg.m_Header = hdr;

        if (hdr.m_JsonSizeBytes != 0)
        {
            // No offset field exists, the only free space is between the file table
            // and the stream blocks.
            const std::uint64_t tbl_end = sizeof( Header ) + std::uint64_t( hdr.m_NumTexFiles ) * sizeof( TextureFileInfo );

            if (auto js = toc.Bytes( tbl_end, hdr.m_JsonSizeBytes ))
            {
                pkg.m_Json.assign( reinterpret_cast<const char *>(js->data()), js->size() );
            }
        }

        SMPACK_TRY( files, toc.Array<TextureFileInfo>( sizeof( Header ), hdr.m_NumTexFiles ) );
        pkg.m_Textures.reserve( files.size() );

        for (const auto & f : files)
        {
            Texture t;
            t.m_TexIdentifier = f.m_TexIdentifier;
            t.m_ContentHash = f.m_ContentHash;

            SMPACK_TRY( chain, WalkMipChain( toc, f.m_SmallestMipOff ) );

            t.m_Mips = std::move( chain );
            pkg.m_Textures.push_back( std::move( t ) );
        }

        return pkg;
    }

    // Block command stream.

    struct BlockContent
    {
        std::uint32_t m_DataOffset = 0; // Start of texel bytes inside the block.
        std::uint32_t m_TotalSize = 0;
        std::uint32_t m_LowMipCount = 0;
        std::uint32_t m_BlockMipCount = 0;
        std::uint32_t m_CopySize = 0;
        std::uint32_t m_CopySrc = 0;
        std::optional<std::array<std::byte, 256>> m_Gnf; // Present on the top block.
        ByteSpan m_Data; // 'm_CopySize' bytes.
    };

    [[nodiscard]] inline Result<BlockContent> ParseBlock(
        ByteSpan block
    )
    {
        ByteReader r( block );
        BlockContent bc;

        SMPACK_TRY( c0, r.At<std::uint32_t>( 0 ) );

        if (c0 != 1)
            return Fail( Errc::INCONSISTENT, std::format( "block does not start with a header command (got {})", c0 ) );

        SMPACK_TRY( doff, r.At<std::uint32_t>( 4 ) );
        SMPACK_TRY( tot, r.At<std::uint32_t>( 8 ) );

        bc.m_DataOffset = doff;
        bc.m_TotalSize = tot;
        std::uint64_t p = 12;
        bool have_copy = false;

        for (int guard = 0; guard < 64; ++guard)
        {
            SMPACK_TRY( cmd, r.At<std::uint32_t>( p ) );

            if (cmd == 2)
                break;

            if (cmd == 3)
            {
                SMPACK_TRY( src, r.At<std::uint32_t>( p + 4 ) );
                SMPACK_TRY( lmc, r.At<std::uint8_t>( p + 8 ) );
                SMPACK_TRY( bmc, r.At<std::uint16_t>( p + 10 ) );
                SMPACK_TRY( sz, r.At<std::uint32_t>( p + 12 ) );

                bc.m_CopySrc = src;
                bc.m_LowMipCount = lmc;
                bc.m_BlockMipCount = bmc;
                bc.m_CopySize = sz;
                have_copy = true;
                p += 16;
            }
            else if (cmd == 5)
            {
                SMPACK_TRY( g, r.Bytes( p + 4, 256 ) );

                std::array<std::byte, 256> a {};
                std::memcpy( a.data(), g.data(), 256 );
                bc.m_Gnf = a;
                p += 260;
            }
            else if (cmd == 4 || cmd == 1)
            {
                p += 12;
            }
            else
            {
                return Fail( Errc::INCONSISTENT, std::format( "unknown block command {}", cmd ) );
            }
        }

        if (!have_copy)
            return Fail( Errc::INCONSISTENT, "block has no memcpy command" );

        SMPACK_TRY( d, r.Bytes( std::uint64_t( bc.m_DataOffset ) + bc.m_CopySrc, bc.m_CopySize ) );
        bc.m_Data = d;
        return bc;
    }

    // Everything needed to turn a texpack texture into pixels.
    struct AssembledTexture
    {
        agc::TSharp m_Tsharp {};
        std::array<std::byte, 256> m_Gnf {};
        agc::SurfaceLayout m_Layout;
        Bytes m_Surface; // Tiled bytes, 'layout.total'.
    };

    [[nodiscard]] inline Result<ByteSpan> BlockBytes(
        ByteSpan payload,
        const Block & b
    )
    {
        ByteReader p( payload );
        return p.Bytes( b.Offset(), b.m_Info.m_DiskSizeBytes );
    }


    // Rebuild the tiled surface of one texture from its blocks.
    [[nodiscard]] inline Result<AssembledTexture> Assemble( ByteSpan payload, const Texture & t )
    {
        if (t.m_Mips.empty())
            return Fail( Errc::INCONSISTENT, "texture has no mip blocks" );

        AssembledTexture out;
        std::vector<BlockContent> contents;

        for (const auto & b : t.m_Mips)
        {
            SMPACK_TRY( bytes, BlockBytes( payload, b ) );
            SMPACK_TRY( bc, ParseBlock( bytes ) );

            contents.push_back( bc );
        }

        if (!contents.back().m_Gnf)
            return Fail( Errc::INCONSISTENT, "top mip block carries no GNF header" );

        out.m_Gnf = *contents.back().m_Gnf;
        out.m_Tsharp = agc::TSharp::From( ByteSpan( out.m_Gnf ).subspan( 16, 32 ) );

        SMPACK_TRY( L, agc::LayoutFor( out.m_Tsharp ) );

        out.m_Layout = std::move( L );
        out.m_Surface.assign( static_cast<std::size_t>(out.m_Layout.m_Total), std::byte { 0 } );

        for (const auto & bc : contents)
        {
            const auto [b, e] = agc::LowMipsRange( out.m_Layout, bc.m_LowMipCount );
            (void)b;

            if (bc.m_CopySize > e)
            {
                return Fail( Errc::INCONSISTENT,
                    std::format( "block of {} bytes does not fit low mip range end {}", bc.m_CopySize, e ) );
            }

            std::memcpy( out.m_Surface.data() + (e - bc.m_CopySize), bc.m_Data.data(), bc.m_CopySize );
        }

        return out;
    }

    // Writer.

    // One texture to be written into a patch texpack.
    struct NewTexture
    {
        std::uint64_t m_TexIdentifier {};
        std::uint64_t m_ContentHash {};
        std::array<std::byte, 256> m_Gnf {}; // T# at +16 describes 'surface'.
        Bytes m_Surface; // Tiled surface, 'layout.total' bytes.
        // Blocks as [m_SmallestMipIndex, m_LargestMipIndex], smallest first.
        // 
        // Empty = default partition.
        std::vector<std::pair<std::uint8_t, std::uint8_t>> m_Partition;
    };

    // Default partition used by the shipped packs. The small mips up to 512 texels [index 9] share a head block,
    // larger mips get one block each.
    [[nodiscard]] inline std::vector<std::pair<std::uint8_t, std::uint8_t>> DefaultPartition( 
        std::uint32_t mip_count
    )
    {
        std::vector<std::pair<std::uint8_t, std::uint8_t>> p;
        const std::uint32_t top = mip_count - 1;
        const std::uint32_t head = std::min<std::uint32_t>( top, 9 );

        p.emplace_back( 0, static_cast<std::uint8_t>(head) );

        for (std::uint32_t i = head + 1; i <= top; ++i)
            p.emplace_back( static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i) );

        return p;
    }


    // Produce the GNF header for a surface, starting from a template [original GNF from the game] and fixing size fields.
    inline void UpdateGnf(
        std::array<std::byte, 256> & gnf,
        const agc::TSharp & ts,
        std::uint64_t total
    )
    {
        std::memcpy( gnf.data() + 16, ts.m_W, 32 );

        // Shipped headers always carry 'stream_size == surface size + 4096'.
        const std::uint32_t stream_size = static_cast<std::uint32_t>(total + 4096);
        std::memcpy( gnf.data() + 12, &stream_size, 4 );

        std::uint32_t t7 = static_cast<std::uint32_t>(total); // T# dword7 carries the surface size.
        std::memcpy( gnf.data() + 16 + 28, &t7, 4 );
    }

    struct BuiltPack
    {
        Bytes m_Toc; // Header and tables [what goes into .texpack.toc].
        Bytes m_Payload; // Full .texpack [toc region and blocks].
    };

    [[nodiscard]] inline Result<BuiltPack> Build(
        const std::vector<NewTexture> & texs
    )
    {
        struct PlannedBlock
        {
            std::size_t m_Tex;
            std::uint8_t m_Smi, m_Lmi;
            std::uint64_t m_SrcBegin, m_Size; // Range inside the surface.
            std::uint32_t m_W, m_H;
            std::uint32_t m_LowCount;
        };

        std::vector<std::vector<PlannedBlock>> plan( texs.size() );
        std::size_t nblocks = 0;
        for (std::size_t i = 0; i < texs.size(); ++i)
        {
            const auto & t = texs[i];
            const auto ts = agc::TSharp::From( ByteSpan( t.m_Gnf ).subspan( 16, 32 ) );

            SMPACK_TRY( L, agc::LayoutFor( ts ) );

            if (t.m_Surface.size() != L.m_Total)
            {
                return Fail( Errc::INCONSISTENT, std::format( "texture {:#018x}: surface is {} bytes, layout needs {}",
                    t.m_TexIdentifier, t.m_Surface.size(), L.m_Total ) );
            }

            if (L.m_Slices != 1)
                return Fail( Errc::UNSUPPORTED, "array/cube textures cannot be streamed from texpacks" );

            const std::uint32_t n = static_cast<std::uint32_t>(L.m_Mips.size());
            auto part = t.m_Partition.empty() ? DefaultPartition( n ) : t.m_Partition;

            std::uint32_t expect = 0;

            for (auto [smi, lmi] : part)
            {
                if (smi != expect || lmi < smi || lmi >= n)
                {
                    return Fail( Errc::INCONSISTENT, std::format( "texture {:#018x}: bad mip partition", t.m_TexIdentifier ) );
                }

                expect = lmi + 1u;

                const auto [b0, e0] = agc::LowMipsRange( L, smi );
                const auto [b1, e1] = agc::LowMipsRange( L, lmi + 1u );
                (void)b0; (void)b1;

                PlannedBlock pb;
                pb.m_Tex = i;
                pb.m_Smi = smi;
                pb.m_Lmi = lmi;
                pb.m_SrcBegin = smi == 0 ? 0 : e0;
                pb.m_Size = e1 - pb.m_SrcBegin;

                const std::uint32_t gpu_mip = n - 1u - lmi;

                pb.m_W = std::max( 1u, ts.Width() >> gpu_mip );
                pb.m_H = std::max( 1u, ts.Height() >> gpu_mip );
                pb.m_LowCount = lmi + 1u;

                if (pb.m_Size + 300 > MAX_BLOCK_BYTES)
                {
                    return Fail( Errc::UNSUPPORTED,
                        std::format( "texture {:#018x}: mip block of {} exceeds the engine's 100 MB stream buffer",
                        t.m_TexIdentifier, pb.m_Size ) );
                }

                plan[i].push_back( pb );
                ++nblocks;
            }

            if (expect != n)
                return Fail( Errc::INCONSISTENT, "mip partition does not cover every mip" );
        }

        const std::uint32_t files_off = sizeof( Header );
        const std::uint32_t blocks_off = files_off + static_cast<std::uint32_t>(texs.size() * sizeof( TextureFileInfo ));
        const std::uint32_t toc_size = static_cast<std::uint32_t>(AlignUp( blocks_off + nblocks * sizeof( StreamBlockFileInfo ), 16 ));

        ByteWriter payload;
        payload.Zeros( toc_size );

        std::vector<TextureFileInfo> files( texs.size() );
        std::vector<StreamBlockFileInfo> sbs( nblocks );
        std::size_t bi = 0;

        for (std::size_t i = 0; i < texs.size(); ++i)
        {
            const auto & t = texs[i];

            files[i].m_TexIdentifier = t.m_TexIdentifier;
            files[i].m_ContentHash = t.m_ContentHash;
            files[i].m_SmallestMipOff = blocks_off + bi * sizeof( StreamBlockFileInfo );

            for (std::size_t k = 0; k < plan[i].size(); ++k, ++bi)
            {
                const auto & pb = plan[i][k];
                const bool top = k + 1 == plan[i].size();
                const std::uint32_t cmd_size = 12 + (top ? 260 : 0) + 16 + 4;

                // Shipped blocks round the whole block up to 16 bytes and record the
                // rounded size in both the header command and the TOC.
                const std::uint32_t block_total = static_cast<std::uint32_t>( AlignUp( cmd_size + pb.m_Size, 16 ) );
                payload.Align( PAYLOAD_ALIGN );

                const std::uint64_t at = payload.Size();

                payload.Put<std::uint32_t>( 1 );
                payload.Put<std::uint32_t>( cmd_size );
                payload.Put<std::uint32_t>( block_total );

                if (top)
                {
                    payload.Put<std::uint32_t>( 5 );
                    payload.Put( ByteSpan( t.m_Gnf ) );
                }

                payload.Put<std::uint32_t>( 3 );
                payload.Put<std::uint32_t>( 0 );
                payload.Put<std::uint8_t>( static_cast<std::uint8_t>(pb.m_LowCount) );
                payload.Put<std::uint8_t>( 0 );
                payload.Put<std::uint16_t>( static_cast<std::uint16_t>(pb.m_Lmi - pb.m_Smi + 1) );
                payload.Put<std::uint32_t>( static_cast<std::uint32_t>(pb.m_Size) );
                payload.Put<std::uint32_t>( 2 );
                payload.Put( ByteSpan( t.m_Surface ).subspan( static_cast<std::size_t>(pb.m_SrcBegin), static_cast<std::size_t>(pb.m_Size) ) );

                // Keep every block's total a multiple of 16 like the shipped packs.
                const std::uint64_t written = payload.Size() - at;
                const std::uint64_t padded = AlignUp( written, 16 );

                payload.Zeros( static_cast<std::size_t>(padded - written) );

                auto & sb = sbs[bi];

                if (at / PAYLOAD_ALIGN > UINT32_MAX)
                    return Fail( Errc::UNSUPPORTED, "texpack exceeds 64 GB" );

                sb.m_FileOffset16 = static_cast<std::uint32_t>(at / PAYLOAD_ALIGN);
                sb.m_MemSizeBytes = static_cast<std::uint32_t>(pb.m_Size);
                sb.m_DiskSizeBytes = block_total;
                sb.m_LargestMipIndex = pb.m_Lmi;
                sb.m_SmallestMipIndex = pb.m_Smi;
                sb.m_LargestMipWidth = static_cast<std::uint16_t>(pb.m_W);
                sb.m_LargestMipHeight = static_cast<std::uint16_t>(pb.m_H);
                sb.m_NextMipOff = top ? NULL_OFFSET : blocks_off + (bi + 1) * sizeof( StreamBlockFileInfo );
            }
        }

        Header h {};
        h.m_TocSizeBytes = toc_size;
        h.m_NumStreamBlocks = static_cast<std::uint32_t>(nblocks);
        h.m_StreamBlocksOff = blocks_off;
        h.m_NumTexFiles = static_cast<std::uint32_t>(texs.size());
        h.m_Version = EXPECTED_VERSION;
        h.m_JsonSizeBytes = 0;

        ByteWriter toc;
        toc.Put( h );

        for (const auto & f : files)
            toc.Put( f );

        for (const auto & s : sbs)
            toc.Put( s );

        toc.Align( 16 );

        BuiltPack out;
        out.m_Toc = toc.Take();
        out.m_Payload = payload.Take();
        std::memcpy( out.m_Payload.data(), out.m_Toc.data(), out.m_Toc.size() );
        return out;
    }
}