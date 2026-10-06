// Wwise streamed media.
//
// Each entry is a Wwise .wem [RIFF, WAVE, Vorbis, Opus, PCM inside] stored in
// part file '<base>.<fileID & (numParts-1)>.audiopack' at 'fileOffset'
// Lookups are a binary search on 'fileID', so the table must stay sorted.

#pragma once

#include <algorithm>
#include <map>
#include <vector>

#include "smpack/reader.hpp"

namespace smpack::audiopack
{

    inline constexpr std::uint32_t MAGIC = 0x4B415041;  // 'APAK'

    struct TocHeader
    {
        std::uint32_t m_Magic;
        std::uint32_t m_Version;
        std::uint32_t m_NumEntries;
        std::uint32_t m_NumParts;
    };

    struct StreamEntry
    {
        std::uint32_t m_FileId;
        std::uint32_t m_FileSize;
        std::uint32_t m_FileOffset;
    };

    struct Toc
    {
        TocHeader m_Header {};
        std::vector<StreamEntry> m_Entries;

        [[nodiscard]] std::uint32_t PartOf(
            std::uint32_t id
        ) const noexcept
        {
            return m_Header.m_NumParts ? id & (m_Header.m_NumParts - 1) : 0;
        }
    };

    [[nodiscard]] inline bool LooksLikeToc(
        ByteSpan d
    ) noexcept
    {
        if (d.size() < sizeof( TocHeader ))
            return false;

        std::uint32_t m = 0;
        std::memcpy( &m, d.data(), 4 );
        return m == MAGIC;
    }


    [[nodiscard]] inline Result<Toc> Parse(
        ByteSpan d
    )
    {
        ByteReader r( d );
        SMPACK_TRY( h, r.At<TocHeader>( 0 ) );

        if (h.m_Magic != MAGIC)
            return Fail( Errc::BAD_MAGIC, "not an APAK table" );

        if (h.m_Version != 1)
            return Fail( Errc::BAD_VERSION, std::format( "APAK version {} unsupported", h.m_Version ) );

        if (h.m_NumParts == 0 || !std::has_single_bit( h.m_NumParts ))
        {
            return Fail( Errc::INCONSISTENT, std::format( "APAK part count {} is not a power of two", h.m_NumParts ) );
        }

        SMPACK_TRY( e, r.Array<StreamEntry>( sizeof( TocHeader ), h.m_NumEntries ) );
        return Toc { h, std::move( e ) };
    }

    // Rebuild all parts. 'get' returns the bytes of an entry [original or replacement].
    struct Built
    {
        Bytes m_Toc;
        std::vector<Bytes> m_Parts;
    };

    template <class Getter>
    [[nodiscard]] Result<Built> Build( 
        const Toc & base,
        std::map<std::uint32_t, Bytes> & added,
        Getter && get,
        std::uint32_t align = 1
    )
    {
        std::vector<std::uint32_t> ids;
        for (const auto & e : base.m_Entries)
            ids.push_back( e.m_FileId );

        for (const auto & [id, b] : added)
            if (std::find( ids.begin(), ids.end(), id ) == ids.end())
                ids.push_back( id );

        std::sort( ids.begin(), ids.end() );

        Built out;
        out.m_Parts.resize( base.m_Header.m_NumParts );
        std::vector<StreamEntry> entries;

        // Keep the original physical order inside each part [by old offset], new files last.
        std::vector<std::pair<std::uint64_t, std::uint32_t>> order;

        for (auto id : ids)
        {
            std::uint64_t key = UINT64_MAX;

            for (const auto & e : base.m_Entries)
                if (e.m_FileId == id)
                    key = e.m_FileOffset;

            order.emplace_back( key, id );
        }

        std::stable_sort( order.begin(), order.end() );
        std::map<std::uint32_t, StreamEntry> placed;

        for (const auto & [key, id] : order)
        {
            (void)key;
            SMPACK_TRY( bytes, get( id ) );

            const std::uint32_t part = base.PartOf( id );
            auto & pb = out.m_Parts[part];

            while (align > 1 && pb.size() % align)
                pb.push_back( std::byte { 0 } );

            if (pb.size() + bytes.size() > UINT32_MAX)
                return Fail( Errc::UNSUPPORTED, "audiopack part exceeds 4 GB" );

            placed[id] = StreamEntry { id, static_cast<std::uint32_t>(bytes.size()), static_cast<std::uint32_t>(pb.size()) };
            pb.insert( pb.end(), bytes.begin(), bytes.end() );
        }

        ByteWriter w;
        TocHeader h = base.m_Header;
        h.m_NumEntries = static_cast<std::uint32_t>(ids.size());
        w.Put( h );

        for (auto id : ids)
            w.Put( placed[id] );

        out.m_Toc = w.Take();
        return out;
    }
}