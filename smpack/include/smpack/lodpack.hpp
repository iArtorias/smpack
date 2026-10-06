// '.lodpack', '.lodpack.toc'. Streamed model LOD geometry.

#pragma once

#include <algorithm>
#include <map>
#include <span>
#include <vector>

#include "smpack/formats.hpp"
#include "smpack/reader.hpp"

namespace smpack::lodpack
{
    struct Pack
    {
        Header m_Header {};
        std::vector<GroupEntry> m_Groups;
        std::vector<BlockEntry> m_Blocks;

        [[nodiscard]] std::uint64_t TocSize() const noexcept
        {
            return sizeof( Header ) + (m_Groups.size() + m_Blocks.size()) * 24ull;
        }
    };

    [[nodiscard]] inline bool LooksLikeToc( 
        ByteSpan d
    ) noexcept
    {
        if (d.size() < sizeof( Header ))
            return false;

        Header h {};
        std::memcpy( &h, d.data(), sizeof( h ) );

        if (h.m_GroupCount == 0 || h.m_GroupCount > 1'000'000 || h.m_BlockCount > 10'000'000)
            return false;

        if (h.m_HeaderFlags > 0xFF)
            return false;

        const std::uint64_t need = sizeof( Header ) + (std::uint64_t( h.m_GroupCount ) + h.m_BlockCount) * 24;

        if (need > d.size())
            return false;

        // First group must start right after the table in a .lodpack, and the
        // blocks must be sorted by hash.
        GroupEntry g {};
        std::memcpy( &g, d.data() + sizeof( Header ), sizeof( g ) );

        if (g.m_FileOffset < need || g.m_FileOffset > need + 0x10000)
            return false;

        return true;
    }


    [[nodiscard]] inline Result<Pack> Parse(
        ByteSpan d
    )
    {
        ByteReader r( d );
        Pack p;
        SMPACK_TRY( h, r.At<Header>( 0 ) );
        p.m_Header = h;
        SMPACK_TRY( g, r.Array<GroupEntry>( sizeof( Header ), h.m_GroupCount ) );
        SMPACK_TRY( b, r.Array<BlockEntry>( sizeof( Header ) + std::uint64_t( h.m_GroupCount ) * 24, h.m_BlockCount ) );

        p.m_Groups = std::move( g );
        p.m_Blocks = std::move( b );

        for (const auto & blk : p.m_Blocks)
        {
            if (blk.m_GroupIdx >= p.m_Groups.size())
                return Fail( Errc::INCONSISTENT, "block references a missing group" );

            if (std::uint64_t( blk.m_GroupOffset ) + blk.m_BlockDataSize > p.m_Groups[blk.m_GroupIdx].m_GroupDataSize)
            {
                return Fail( Errc::INCONSISTENT, std::format( "block {:#018x} overruns its group", blk.m_BlockDataHash ) );
            }
        }

        return p;
    }


    [[nodiscard]] inline Result<ByteSpan> GroupBytes(
        ByteSpan lodpack,
        const GroupEntry & g
    )
    {
        return ByteReader( lodpack ).Bytes( g.m_FileOffset, g.m_GroupDataSize );
    }

    // A group to write. Original entry metadata and possibly modified bytes.
    struct NewGroup
    {
        GroupEntry m_Entry {}; // 'm_FileOffset' and 'm_GroupDataSize' are recomputed.
        Bytes m_Data;
        std::vector<BlockEntry> m_Blocks; // 'm_GroupIdx' recomputed, 'm_GroupOffset' kept.
    };

    struct Built
    {
        Bytes m_Toc;
        Bytes m_Pack;
    };

    [[nodiscard]] inline Result<Built> Build( 
        std::vector<NewGroup> groups, 
        std::uint32_t header_flags = 1,
        std::uint32_t group_align = 16
    )
    {
        Header h {};
        h.m_GroupCount = static_cast<std::uint32_t>(groups.size());
        std::vector<BlockEntry> all;

        for (std::size_t gi = 0; gi < groups.size(); ++gi)
        {
            for (auto b : groups[gi].m_Blocks)
            {
                b.m_GroupIdx = static_cast<std::uint32_t>( gi );

                if (std::uint64_t( b.m_GroupOffset ) + b.m_BlockDataSize > groups[gi].m_Data.size())
                {
                    return Fail( Errc::INCONSISTENT, std::format( "block {:#018x} overruns group data", b.m_BlockDataHash ) );
                }

                all.push_back( b );
            }
        }

        std::sort( all.begin(), all.end(), [] ( const BlockEntry & a, const BlockEntry & b )
        {
            return a.m_BlockDataHash < b.m_BlockDataHash;
        } );

        for (std::size_t i = 1; i < all.size(); ++i)
        {
            if (all[i].m_BlockDataHash == all[i - 1].m_BlockDataHash)
            {
                return Fail( Errc::INCONSISTENT, std::format( "duplicate block hash {:#018x}", all[i].m_BlockDataHash ) );
            }
        }

        h.m_BlockCount = static_cast<std::uint32_t>( all.size() );
        h.m_PrimParmCount = 0;
        h.m_HeaderFlags = header_flags;

        const std::uint64_t toc_size = sizeof( Header ) + (groups.size() + all.size()) * 24;
        std::uint64_t pos = AlignUp( toc_size, group_align );

        for (auto & g : groups)
        {
            g.m_Entry.m_FileOffset = pos;
            g.m_Entry.m_GroupDataSize = static_cast<std::uint32_t>(g.m_Data.size());
            pos = AlignUp( pos + g.m_Data.size(), group_align );
        }

        ByteWriter toc;
        toc.Put( h );

        for (const auto & g : groups)
            toc.Put( g.m_Entry );

        for (const auto & b : all)
            toc.Put( b );

        Built out;
        out.m_Toc = toc.Take();
        ByteWriter pack;
        pack.Put( ByteSpan( out.m_Toc ) );

        for (const auto & g : groups)
        {
            pack.Zeros( static_cast<std::size_t>(g.m_Entry.m_FileOffset - pack.Size()) );
            pack.Put( ByteSpan( g.m_Data ) );
        }

        out.m_Pack = pack.Take();
        return out;
    }

    // Relay a lodpack group with some blocks replaced. Unchanged blocks keep
    // their bytes, bytes between blocks are kept, blocks stay 16 byte aligned.
    [[nodiscard]] inline NewGroup RebuildGroup(
        const GroupEntry & g,
        ByteSpan gb,
        std::vector<BlockEntry> blocks,
        const std::map<std::uint64_t, Bytes> & repl
    )
    {
        std::sort( blocks.begin(), blocks.end(), [] ( auto & a, auto & b )
        {
            return a.m_GroupOffset < b.m_GroupOffset;
        } );

        NewGroup ng;
        ng.m_Entry = g;
        std::uint64_t src_pos = 0;

        for (auto b : blocks)
        {
            const std::uint64_t orig_off = b.m_GroupOffset, orig_size = b.m_BlockDataSize;

            if (orig_off > src_pos)
                ng.m_Data.insert( ng.m_Data.end(), gb.begin() + static_cast<std::ptrdiff_t>(src_pos), gb.begin() + static_cast<std::ptrdiff_t>(orig_off) );

            while (ng.m_Data.size() % 16)
                ng.m_Data.push_back( std::byte { 0 } );

            auto it = repl.find( b.m_BlockDataHash );
            const ByteSpan bytes = it != repl.end() ? ByteSpan( it->second ) : gb.subspan( static_cast<std::size_t>(orig_off), static_cast<std::size_t>(orig_size) );

            b.m_GroupOffset = static_cast<std::uint32_t>(ng.m_Data.size());
            b.m_BlockDataSize = static_cast<std::uint32_t>(bytes.size());
            ng.m_Data.insert( ng.m_Data.end(), bytes.begin(), bytes.end() );
            src_pos = std::max( src_pos, orig_off + orig_size );

            ng.m_Blocks.push_back( b );
        }

        if (src_pos < gb.size())
            ng.m_Data.insert( ng.m_Data.end(), gb.begin() + static_cast<std::ptrdiff_t>( src_pos ), gb.end() );

        return ng;
    }
}