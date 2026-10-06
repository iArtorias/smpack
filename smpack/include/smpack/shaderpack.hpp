// .shaderpack, D3D12 shader cache.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "smpack/formats.hpp"
#include "smpack/reader.hpp"

namespace smpack::shaderpack
{
    struct Section
    {
        std::string m_Name;
        std::uint64_t m_Offset {};
        std::uint64_t m_Count {};
        std::uint64_t m_Size {};
    };

    struct Package
    {
        Header m_Header {};
        std::vector<ShaderRecord> m_Shaders;
        std::vector<Section> m_Sections;

        [[nodiscard]] bool BinsCompressed() const noexcept
        {
            return m_Header.m_NumUniqueShaderBytes != 0 &&
                m_Header.m_NumUniqueShaderBytes != m_Header.m_NumUniqueShaderCompressed;
        }
    };


    [[nodiscard]] inline std::string ProfileName(
        std::uint32_t p
    )
    {
        return std::format( "{:#x}", p );
    }


    [[nodiscard]] inline Result<Package> Parse( ByteSpan data )
    {
        ByteReader r( data );
        SMPACK_TRY( h, r.At<Header>( 0 ) );

        if (h.m_Magic != MAGIC)
            return Fail( Errc::BAD_MAGIC, "not a SHDRPACK" );

        if (h.m_Version != EXPECTED_VERSION)
        {
            return Fail( Errc::BAD_VERSION, std::format( "shaderpack version {} unsupported (engine accepts only {})", h.m_Version, EXPECTED_VERSION ) );
        }

        Package p;
        p.m_Header = h;

        SMPACK_TRY( recs, r.Array<ShaderRecord>( h.m_UniqueShaderRecordsOff, h.m_NumUniqueShaders ) );

        p.m_Shaders = std::move( recs );
        p.m_Sections =
        {
            { "unique_shader_records", h.m_UniqueShaderRecordsOff, h.m_NumUniqueShaders },
            { "unique_shader_bins", h.m_UniqueShaderBinsOff, 0 },
            { "root_signature_records", h.m_RootSignatureRecordsOff, h.m_NumRootSignatures },
            { "root_signature_bins", h.m_RootSignatureBinsOff, 0 },
            { "shader_combo_records", h.m_ShaderComboRecordsOff, h.m_NumShaderCombos },
            { "pso_records", h.m_PsoRecordsOff, h.m_NumPsos },
        };

        // Extents from the next distinct section start.
        std::vector<std::uint64_t> starts;
        for (const auto & s : p.m_Sections)
            starts.push_back( s.m_Offset );

        starts.push_back( data.size() );
        std::sort( starts.begin(), starts.end() );

        for (auto & s : p.m_Sections)
        {
            if (s.m_Offset > data.size())
                return Fail( Errc::BAD_OFFSET, std::format( "section {} past end of file", s.m_Name ) );

            auto it = std::upper_bound( starts.begin(), starts.end(), s.m_Offset );
            s.m_Size = (it == starts.end() ? data.size() : *it) - s.m_Offset;

            const bool empty_records = s.m_Name != "unique_shader_bins" && s.m_Name != "root_signature_bins" && s.m_Count == 0;

            if (empty_records || (s.m_Name == "root_signature_bins" && h.m_NumRootSignatures == 0))
                s.m_Size = 0;

            if (s.m_Name == "unique_shader_bins")
                s.m_Size = h.m_NumUniqueShaderCompressed;
        }

        if (!p.BinsCompressed())
        {
            for (const auto & s : p.m_Shaders)
            {
                if (h.m_UniqueShaderBinsOff + s.m_BinOffset + s.m_BinSize > data.size())
                {
                    return Fail( Errc::BAD_OFFSET, "shader bin past end of file" );
                }
            }
        }

        return p;
    }


    [[nodiscard]] inline ByteSpan ShaderBytes(
        ByteSpan data,
        const Package & p,
        const ShaderRecord & s
    )
    {
        return data.subspan( static_cast<std::size_t>(p.m_Header.m_UniqueShaderBinsOff + s.m_BinOffset), s.m_BinSize );
    }


    [[nodiscard]] inline std::string ShaderName(
        const ShaderRecord & s
    )
    {
        return std::string( FixedStr( s.m_DebugName, sizeof( s.m_DebugName ) ) );
    }


    // Replace shader blobs [by index] and reload the bins section. Sections
    // that follow the bins are moved and their header offsets updated.
    [[nodiscard]] inline Result<Bytes> Rebuild(
        ByteSpan data,
        const Package & p,
        const std::vector<std::pair<std::size_t, Bytes>> & repl
    )
    {
        if (p.BinsCompressed())
            return Fail( Errc::UNSUPPORTED, "compressed shader bins are not supported for rebuild" );

        const auto & h = p.m_Header;
        const std::uint64_t bins_off = h.m_UniqueShaderBinsOff;
        const std::uint64_t old_bins_size = h.m_NumUniqueShaderCompressed;
        std::vector<ShaderRecord> recs = p.m_Shaders;

        // New bins, keeping original order by offset.
        std::vector<std::size_t> order( recs.size() );
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;

        std::sort( order.begin(), order.end(), [&] ( auto a, auto b )
        {
            return recs[a].m_BinOffset < recs[b].m_BinOffset;
        } );

        ByteWriter bins;
        for (auto i : order)
        {
            ByteSpan src = ShaderBytes( data, p, recs[i] );

            for (const auto & [ri, blob] : repl)
                if (ri == i)
                    src = ByteSpan( blob );

            bins.Align( 16 ); // Shipped packs start every shader on a 16 byte boundary.

            recs[i].m_BinOffset = static_cast<std::uint32_t>( bins.Size() );
            recs[i].m_BinSize = static_cast<std::uint32_t>( src.size() );

            bins.Put( src );
        }

        bins.Align( 16 );

        const std::int64_t delta = static_cast<std::int64_t>(bins.Size()) - static_cast<std::int64_t>(old_bins_size);

        Header nh = h;
        nh.m_NumUniqueShaderCompressed = bins.Size();

        auto shift = [&] ( std::uint64_t & off )
        {
            if (off >= bins_off + old_bins_size)
                off = static_cast<std::uint64_t>(static_cast<std::int64_t>(off) + delta);
        };

        shift( nh.m_RootSignatureRecordsOff );
        shift( nh.m_ShaderComboRecordsOff );
        shift( nh.m_PsoRecordsOff );
        shift( nh.m_RootSignatureBinsOff );

        ByteWriter out;
        out.Put( nh );
        out.Put( data.subspan( sizeof( Header ), static_cast<std::size_t>(h.m_UniqueShaderRecordsOff - sizeof( Header )) ) );

        for (const auto & rcd : recs)
            out.Put( rcd );

        const std::uint64_t rec_end = h.m_UniqueShaderRecordsOff + recs.size() * sizeof( ShaderRecord );

        out.Put( data.subspan( static_cast<std::size_t>(rec_end), static_cast<std::size_t>(bins_off - rec_end) ) );
        out.Put( ByteSpan( bins.Buf() ) );
        out.Put( data.subspan( static_cast<std::size_t>(bins_off + old_bins_size) ) );

        return out.Take();
    }
}