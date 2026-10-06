// Streamed animation file sets.
//
// One .as per WAD [same base name]. It is a sequence of 'anmFileSet' blobs,
// each starting on a 64 KB boundary.

#pragma once

#include <string>
#include <vector>

#include "smpack/formats.hpp"
#include "smpack/reader.hpp"
#include "smpack/wad.hpp"

namespace smpack::animset
{
    struct FileSet
    {
        FileSetHeader m_H {};
        std::uint64_t m_Offset = 0; // Where found.
        std::uint64_t m_Size = 0;
        std::vector<std::int64_t> m_FileOffsets;

        [[nodiscard]] std::string Name() const
        {
            return std::string( FixedStr( m_H.m_Name, sizeof( m_H.m_Name ) ) );
        }
    };

    [[nodiscard]] inline bool PlausibleHeader( const FileSetHeader & h, std::uint64_t at, std::uint64_t file_size ) noexcept
    {
        for (auto b : h.m_Streamable)
            if (b)
                return false;

        if (h.m_FileOffset != at || h.m_FileSize < sizeof( FileSetHeader ) || at + h.m_FileSize > file_size)
            return false;

        if (h.m_NumFiles > 4096)
            return false;

        return true;
    }

    [[nodiscard]] inline bool LooksLikeAs( ByteSpan d ) noexcept
    {
        if (d.size() < sizeof( FileSetHeader ))
            return false;

        FileSetHeader h {};
        std::memcpy( &h, d.data(), sizeof( h ) );
        return PlausibleHeader( h, 0, d.size() ) && h.m_Name[0] != 0;
    }


    [[nodiscard]] inline Result<std::vector<FileSet>> Parse(
        ByteSpan d
    )
    {
        ByteReader r( d );
        std::vector<FileSet> out;
        std::uint64_t o = 0;

        while (o + sizeof( FileSetHeader ) <= d.size())
        {
            SMPACK_TRY( h, r.At<FileSetHeader>( o ) );

            if (!PlausibleHeader( h, o, d.size() ))
            {
                // Tolerate zero padding. Skip to the next slot.
                bool zero = true;
                for (std::size_t i = 0; i < sizeof( FileSetHeader ) && zero; ++i)
                    zero = d[o + i] == std::byte { 0 };

                if (!zero)
                    return Fail( Errc::INCONSISTENT, std::format( "unrecognised data at .as offset {:#x}", o ) );

                o += SLOT_ALIGN;
                continue;
            }

            FileSet fs;
            fs.m_H = h;
            fs.m_Offset = o;
            fs.m_Size = h.m_FileSize;
            SMPACK_TRY( offs, r.Array<std::int64_t>( o + sizeof( FileSetHeader ), h.m_NumFiles ) );
            fs.m_FileOffsets = std::move( offs );
            out.push_back( std::move( fs ) );
            o = AlignUp( o + h.m_FileSize, SLOT_ALIGN );
        }

        return out;
    }


    // Lay blobs out again [64 KB slots] and fix every blob's '_fileInf'.
    [[nodiscard]] inline Result<Bytes> Build(
        std::vector<Bytes> blobs
    )
    {
        ByteWriter w;
        for (auto & b : blobs)
        {
            if (b.size() < sizeof( FileSetHeader ))
                return Fail( Errc::INCONSISTENT, "anim set blob too small" );

            w.Align( SLOT_ALIGN );
            const std::uint64_t at = w.Size();

            if (at > UINT32_MAX || b.size() > UINT32_MAX)
                return Fail( Errc::UNSUPPORTED, ".as exceeds 4 GB" );

            const std::uint32_t off = static_cast<std::uint32_t>(at), sz = static_cast<std::uint32_t>(b.size());
            std::memcpy( b.data() + 0x40, &off, 4 );
            std::memcpy( b.data() + 0x44, &sz, 4 );
            w.Put( ByteSpan( b ) );
        }

        w.Align( SLOT_ALIGN );
        return w.Take();
    }


    // Give a donor blob the identity [name, hash, id, type] of the set it
    // replaces so lookups by name keep resolving.
    inline void AdoptIdentity( 
        Bytes & blob,
        const FileSetHeader & target
    )
    {
        if (blob.size() < sizeof( FileSetHeader ))
            return;

        FileSetHeader h {};
        std::memcpy( &h, blob.data(), sizeof( h ) );
        h.m_NameHash = target.m_NameHash;
        std::memcpy( h.m_Name, target.m_Name, sizeof( h.m_Name ) );
        h.m_Id = target.m_Id;
        h.m_TypeHash = target.m_TypeHash;
        std::memcpy( blob.data(), &h, sizeof( h ) );
    }

    struct StubPatch
    {
        std::uint64_t m_NameHash;
        std::uint32_t m_OldOffset, m_OldSize;
        std::uint32_t m_NewOffset, m_NewSize;
    };

    // Rewrite '_fileInf' in every WAD stub that points at a moved/resized set.
    // Returns the number of stubs changed. 'wad_data' is the decompressed WAD.
    [[nodiscard]] inline std::size_t PatchWadStubs(
        Bytes & wad_data,
        const wad::Wad & w,
        const std::vector<StubPatch> & patches
    )
    {
        std::size_t changed = 0;
        for (const auto & en : w.m_Entries)
        {
            if (en.m_E.m_Id != 1 || en.m_E.m_ServerId != 2 || en.m_E.m_Id == 25)
                continue;

            const std::uint64_t b = en.m_DataOff, e = en.m_DataOff + en.m_E.m_Length;

            if (e > wad_data.size() || en.m_E.m_Length < 16)
                continue;

            for (std::uint64_t p = b + 8; p + 8 <= e; p += 8)
            {
                std::uint64_t hv = 0;
                std::memcpy( &hv, wad_data.data() + p, 8 );

                for (const auto & sp : patches)
                {
                    if (hv != sp.m_NameHash)
                        continue;

                    std::uint32_t o = 0, s = 0;
                    std::memcpy( &o, wad_data.data() + p - 8, 4 );
                    std::memcpy( &s, wad_data.data() + p - 4, 4 );

                    if (o != sp.m_OldOffset || s != sp.m_OldSize)
                        continue;

                    std::memcpy( wad_data.data() + p - 8, &sp.m_NewOffset, 4 );
                    std::memcpy( wad_data.data() + p - 4, &sp.m_NewSize, 4 );
                    ++changed;
                }
            }
        }

        return changed;
    }
}