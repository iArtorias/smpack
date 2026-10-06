// WAD (WTOC) containers. .wad, .wypdb and every other TOC and chunks file.

#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "smpack/agc.hpp"
#include "smpack/formats.hpp"
#include "smpack/json.hpp"
#include "smpack/reader.hpp"

namespace smpack::wad
{
    [[nodiscard]] inline std::string_view ChunkName( 
        std::uint16_t id
    ) noexcept
    {
        switch (id)
        {
            case 0:
                return "DynaString";

            case 1:
                return "ClientParm";

            case 2:
                return "GroupStart";

            case 3:
                return "GroupEnd";

            case 7:
                return "DataBlock";

            case 8:
                return "DataBlockWLength";

            case 9:
                return "DataBlockAlign16";

            case 11:
                return "DcVersion";

            case 12:
                return "DcData";

            case 13:
                return "DcExportTable";

            case 14:
                return "DcImportTable";

            case 15:
                return "DcPointerTable";

            case 16:
                return "DcStringTable";

            case 17:
                return "DcSymbolTable";

            case 19:
                return "PushContext";

            case 21:
                return "PushHeap";

            case 22:
                return "PopHeap";

            case 25:
                return "AutoPad";

            case 29:
                return "GPUChunk";

            case 31:
                return "ExtDataCompiler";

            case 32:
                return "ExtSoundBank";

            case 35:
                return "DcNetIndexTable";

            case 36:
                return "LuaFile";

            case 46:
                return "Alias";

            case 51:
                return "DcUniqueIDTable";

            case 52:
                return "ExtWypSourceMesh";

            case 53:
                return "ExtWypStitchingDB";

            case 54:
                return "ExtLocSoundBank";

            case 56:
                return "Budgets";

            case 57:
                return "ExtSoundBankProd";

            case 58:
                return "ExtLocSoundBankProd";

            case 60:
                return "ReferenceParms";

            default:
                return "Chunk";
        }
    }


    [[nodiscard]] inline std::string_view ServerName(
        std::uint8_t id
    ) noexcept
    {
        switch (id)
        {
            case 1:
                return "go";

            case 2:
                return "anim";

            case 3:
                return "behavior";

            case 4:
                return "multiContext";

            case 5:
                return "goScript";

            case 6:
                return "wyp";

            case 8:
                return "fx";

            case 9:
                return "light";

            case 10:
                return "material";

            case 11:
                return "wwise";

            case 12:
                return "model";

            case 13:
                return "particle";

            case 14:
                return "decal";

            case 15:
                return "physics";

            case 16:
                return "wind";

            case 17:
                return "entity";

            case 18:
                return "procAnim";

            case 19:
                return "lua";

            case 20:
                return "cubemapRegion";

            case 21:
                return "interact";

            case 22:
                return "levelScripting";

            case 23:
                return "gameModule";

            case 25:
                return "texture";

            case 26:
                return "wad";

            case 27:
                return "banter";

            case 29:
                return "behaviorTree";

            case 30:
                return "encounter";

            default:
                return "";
        }
    }


    [[nodiscard]] inline std::string_view MemTypeName(
        std::uint8_t t
    ) noexcept
    {
        static constexpr std::string_view k[] = { "cpu", "gpu", "texlowmips", "pool", "root",
            "debug", "debugheap_cpu", "debugheap_gpu", "temp_cpu" };
        return t < 9 ? k[t] : "?";
    }


    [[nodiscard]] inline bool IsExternalFileChunk(
        std::uint16_t id
    ) noexcept
    {
        return id <= 0x3A && ((0x0670000180000000ull >> id) & 1);
    }

    struct Entry
    {
        TocEntry m_E {};
        std::uint64_t m_DataOff = 0; // File offset of main data.
        std::uint64_t m_DebugFileOff = 0;
        std::uint64_t m_TempFileOff = 0;
        std::uint32_t m_Block = 0; // Load block index.

        [[nodiscard]] std::string Name() const
        {
            return std::string( FixedStr( m_E.m_Name, sizeof( m_E.m_Name ) ) );
        }


        [[nodiscard]] bool EndOfBlock() const noexcept
        {
            return m_E.m_Bits & 1;
        }


        [[nodiscard]] std::uint64_t Guid64() const noexcept
        {
            return static_cast<std::uint64_t>(m_E.m_Guid[2]) << 32 | m_E.m_Guid[3];
        }
    };

    struct Wad
    {
        TocHeader m_Header {};
        std::vector<Entry> m_Entries;
        std::uint64_t m_DataEnd = 0; // Computed end of the last block.
    };

    // Compute arena offsets or file layout for a list of toc entry exactly like
    // the loader does. When 'assign' is true, mem, debug, temp offsets are
    // rewritten [used by the rebuilder], otherwise they are validated and the
    // file offsets derived from them.
    struct LayoutResult
    {
        std::uint32_t m_Totals[NUM_MEM_TYPES] {};
        std::uint64_t m_End = 0;
    };

    inline LayoutResult ComputeLayout(
        std::vector<Entry> & es,
        std::uint64_t data_start,
        bool assign
    )
    {
        LayoutResult lr;
        std::uint64_t bs[NUM_MEM_TYPES] {}, bo[NUM_MEM_TYPES] {};
        std::uint64_t pos = data_start;
        std::uint64_t autopad = 0;
        std::size_t block_first = 0;
        std::uint32_t block_idx = 0;
        std::vector<std::uint64_t> blk_off( es.size() ); // Aligned arena offset per entry.

        for (std::size_t i = 0; i < es.size(); ++i)
        {
            auto & en = es[i];

            const std::uint32_t t = std::min<std::uint32_t>( en.m_E.m_MemType, NUM_MEM_TYPES - 1 );
            const std::uint64_t al = en.m_E.m_ChunkAlign ? en.m_E.m_ChunkAlign : 1;
            const std::uint64_t start = bo[t] + bs[t];
            const std::uint64_t aligned = AlignUp( start, al );

            blk_off[i] = aligned;

            if (en.m_E.m_Id == 25)
            {
                autopad = en.m_E.m_Length;
                bs[t] = aligned - bo[t];
            }
            else
            {
                bs[t] = aligned - bo[t] + en.m_E.m_Length;
                if (assign) en.m_E.m_MemOff = aligned;
            }

            if (en.m_E.m_DebugSize && assign)
                en.m_E.m_DebugOff = bo[5] + bs[5];

            if (en.m_E.m_TempSize && assign)
                en.m_E.m_TempOff = bo[8] + bs[8];

            bs[5] += en.m_E.m_DebugSize;
            bs[8] += en.m_E.m_TempSize;

            en.m_Block = block_idx;
            const bool last = i + 1 == es.size();

            if ((en.m_E.m_Bits & 1) || last)
            {
                std::uint64_t region[NUM_MEM_TYPES];
                std::uint64_t p = pos;

                for (std::uint32_t k = 0; k < NUM_MEM_TYPES; ++k)
                {
                    region[k] = p;
                    p += bs[k];
                }

                for (std::size_t j = block_first; j <= i; ++j)
                {
                    auto & x = es[j];
                    const std::uint32_t tt = std::min<std::uint32_t>( x.m_E.m_MemType, NUM_MEM_TYPES - 1 );

                    if (x.m_E.m_Id == 25)
                        x.m_DataOff = p;
                    else x.m_DataOff = region[tt] + (x.m_E.m_MemOff - bo[tt]);

                    if (x.m_E.m_DebugSize)
                        x.m_DebugFileOff = region[5] + (x.m_E.m_DebugOff - bo[5]);

                    if (x.m_E.m_TempSize)
                        x.m_TempFileOff = region[8] + (x.m_E.m_TempOff - bo[8]);
                }

                for (std::uint32_t k = 0; k < NUM_MEM_TYPES; ++k)
                {
                    bo[k] += bs[k];
                    bs[k] = 0;
                }

                pos = p + autopad;
                autopad = 0;
                block_first = i + 1;
                ++block_idx;
            }
        }

        for (std::uint32_t k = 0; k < NUM_MEM_TYPES; ++k)
            lr.m_Totals[k] = static_cast<std::uint32_t>( bo[k] );

        lr.m_End = pos;
        return lr;
    }


    [[nodiscard]] inline bool LooksLikeWad(
        ByteSpan d
    ) noexcept
    {
        if (d.size() < sizeof( TocHeader ))
            return false;

        std::uint32_t m = 0;
        std::memcpy( &m, d.data(), 4 );
        return m == MAGIC;
    }


    [[nodiscard]] inline Result<Wad> Parse(
        ByteSpan data
    )
    {
        ByteReader r( data );
        SMPACK_TRY( h, r.At<TocHeader>( 0 ) );

        if (h.m_Magic != MAGIC)
            return Fail( Errc::BAD_MAGIC, "not a WTOC container" );

        if (h.m_Version != VERSION)
            return Fail( Errc::BAD_VERSION, std::format( "WTOC version {} unsupported", h.m_Version ) );

        SMPACK_TRY( raw, r.Array<TocEntry>( sizeof( TocHeader ), h.m_NumEntries ) );

        Wad w;
        w.m_Header = h;
        w.m_Entries.reserve( raw.size() );

        for (const auto & e : raw)
            w.m_Entries.push_back( Entry { e } );

        const std::uint64_t start = sizeof( TocHeader ) + std::uint64_t( h.m_NumEntries ) * sizeof( TocEntry );
        auto lr = ComputeLayout( w.m_Entries, start, false );

        w.m_DataEnd = lr.m_End;

        for (const auto & en : w.m_Entries)
        {
            if (en.m_E.m_Id == 25)
                continue;

            if (en.m_DataOff + en.m_E.m_Length > data.size())
            {
                return Fail( Errc::BAD_OFFSET, std::format( "chunk '{}' data [{:#x},+{:#x}) is past end of file {:#x}",
                    en.Name(), en.m_DataOff, en.m_E.m_Length, data.size() ) );
            }
        }

        return w;
    }


    [[nodiscard]] inline ByteSpan ChunkData( ByteSpan wad, const Entry & e )
    {
        if (e.m_E.m_Id == 25 || e.m_DataOff + e.m_E.m_Length > wad.size())
            return {};

        return wad.subspan( static_cast<std::size_t>(e.m_DataOff), e.m_E.m_Length );
    }


    [[nodiscard]] inline ByteSpan DebugData( ByteSpan wad, const Entry & e )
    {
        if (!e.m_E.m_DebugSize || e.m_DebugFileOff + e.m_E.m_DebugSize > wad.size())
            return {};

        return wad.subspan( static_cast<std::size_t>(e.m_DebugFileOff), e.m_E.m_DebugSize );
    }


    [[nodiscard]] inline ByteSpan TempData(
        ByteSpan wad,
        const Entry & e
    )
    {
        if (!e.m_E.m_TempSize || e.m_TempFileOff + e.m_E.m_TempSize > wad.size())
            return {};

        return wad.subspan( static_cast<std::size_t>(e.m_TempFileOff), e.m_E.m_TempSize );
    }

    // Chunk payloads supplied to the rebuilder [sizes may differ from the TOC].
    struct ChunkPayload
    {
        Bytes m_Data, m_Debug, m_Temp;
    };

    // Rebuild a WTOC image from entries and payloads. Offsets, sizes, arena
    // totals and file placement are all recomputed.
    [[nodiscard]] inline Result<Bytes> Build( TocHeader header, std::vector<Entry> es, const std::vector<ChunkPayload> & payloads )
    {
        if (es.size() != payloads.size())
            return Fail( Errc::INCONSISTENT, "entry/payload count mismatch" );

        if (es.empty())
            return Fail( Errc::INCONSISTENT, "WAD has no entries" );

        for (std::size_t i = 0; i < es.size(); ++i)
        {
            if (es[i].m_E.m_Id != 25)
                es[i].m_E.m_Length = static_cast<std::uint32_t>( payloads[i].m_Data.size() );

            es[i].m_E.m_DebugSize = static_cast<std::uint32_t>( payloads[i].m_Debug.size() );
            es[i].m_E.m_TempSize = static_cast<std::uint32_t>( payloads[i].m_Temp.size() );
        }

        // The last entry must terminate the final block.
        es.back().m_E.m_Bits |= 1;

        const std::uint64_t start = sizeof( TocHeader ) + es.size() * sizeof( TocEntry );
        auto lr = ComputeLayout( es, start, true );

        header.m_NumEntries = static_cast<std::uint32_t>( es.size() );

        for (std::uint32_t k = 0; k < NUM_MEM_TYPES; ++k)
            header.m_TotalMemByType[k] = lr.m_Totals[k];

        Bytes out( static_cast<std::size_t>( lr.m_End ), std::byte { 0 } );
        std::memcpy( out.data(), &header, sizeof( header ) );

        for (std::size_t i = 0; i < es.size(); ++i)
        {
            std::memcpy( out.data() + sizeof( TocHeader ) + i * sizeof( TocEntry ), &es[i].m_E, sizeof( TocEntry ) );

            const auto & p = payloads[i];

            if (es[i].m_E.m_Id != 25 && !p.m_Data.empty())
                std::memcpy( out.data() + es[i].m_DataOff, p.m_Data.data(), p.m_Data.size() );

            if (!p.m_Debug.empty())
                std::memcpy( out.data() + es[i].m_DebugFileOff, p.m_Debug.data(), p.m_Debug.size() );

            if (!p.m_Temp.empty())
                std::memcpy( out.data() + es[i].m_TempFileOff, p.m_Temp.data(), p.m_Temp.size() );
        }
        return out;
    }

    // Convert entries to JSON.

    [[nodiscard]] inline json::Value EntryToJson( 
        const Entry & en
    )
    {
        const auto & e = en.m_E;

        json::Value j = json::Value::MakeObject();

        j["id"] = e.m_Id;
        j["type"] = ChunkName( e.m_Id );
        j["version"] = e.m_Version;
        j["name"] = en.Name();
        j["guid"] = std::format( "{:08x}{:08x}{:08x}{:08x}", e.m_Guid[0], e.m_Guid[1], e.m_Guid[2], e.m_Guid[3] );
        j["dc_version_hash"] = std::format( "{:#010x}", e.m_DcVersionHash );
        j["length"] = e.m_Length;
        j["align"] = e.m_ChunkAlign;
        j["mem_type"] = e.m_MemType;
        j["server"] = e.m_ServerId;
        j["client"] = e.m_ClientId;

        if (!ServerName( e.m_ServerId ).empty() && e.m_Id == 1)
            j["server_name"] = ServerName( e.m_ServerId );

        j["ct_flags"] = e.m_CtFlags;
        j["group_start"] = e.m_GroupStartType;
        j["group_end"] = e.m_GroupEndCount;
        j["end_of_block"] = static_cast<bool>(e.m_Bits & 1);

        if (e.m_Bits & 2)
            j["removed"] = true;

        if (e.m_DebugSize)
            j["debug_size"] = e.m_DebugSize;

        if (e.m_TempSize)
            j["temp_size"] = e.m_TempSize;

        if (e.m_Id == 25)
            j["mem_off"] = json::Hex64( e.m_MemOff );

        const std::uint8_t * ap = e.m_AlignPad;
        bool nz = false;

        for (int i = 0; i < 12; ++i)
            nz |= ap[i] != 0;

        if (nz)
            j["align_pad"] = ToHex( { reinterpret_cast<const std::byte *>( ap ), 12 } );

        if (e.m_Pad || e.m_Pad2)
            j["pad"] = std::format( "{:#x}:{:#x}", e.m_Pad, e.m_Pad2 );

        return j;
    }


    [[nodiscard]] inline Result<TocEntry> EntryFromJson(
        const json::Value & j
    )
    {
        TocEntry e {};
        e.m_Id = static_cast<std::uint16_t>( j.Get( "id" ).AsInt() );
        e.m_Version = static_cast<std::uint16_t>( j.Get( "version" ).AsInt() );
        e.m_Length = static_cast<std::uint32_t>(j.Get( "length" ).AsInt());

        const std::string & name = j.Get( "name" ).AsString();

        if (name.size() >= sizeof( e.m_Name ))
            return Fail( Errc::BAD_ARGUMENT, std::format( "chunk name '{}' longer than 55 chars", name ) );

        std::memcpy( e.m_Name, name.data(), name.size() );
        const std::string & g = j.Get( "guid" ).AsString();

        if (g.size() == 32)
        {
            for (int i = 0; i < 4; ++i)
            {
                std::uint32_t v = 0;
                std::from_chars( g.data() + i * 8, g.data() + i * 8 + 8, v, 16 );
                e.m_Guid[i] = v;
            }
        }

        e.m_DcVersionHash = static_cast<std::uint32_t>( j.Get( "dc_version_hash" ).AsU64() );
        e.m_ChunkAlign = static_cast<std::uint32_t>( j.Get( "align" ).AsInt( 1 ) );
        e.m_MemType = static_cast<std::uint8_t>( j.Get( "mem_type" ).AsInt() );
        e.m_ServerId = static_cast<std::uint8_t>( j.Get( "server" ).AsInt() );
        e.m_ClientId = static_cast<std::uint8_t>(j.Get( "client" ).AsInt());
        e.m_CtFlags = static_cast<std::uint8_t>(j.Get( "ct_flags" ).AsInt());
        e.m_GroupStartType = static_cast<std::uint8_t>(j.Get( "group_start" ).AsInt());
        e.m_GroupEndCount = static_cast<std::uint8_t>(j.Get( "group_end" ).AsInt());
        e.m_Bits = static_cast<std::uint8_t>((j.Get( "end_of_block" ).AsBool() ? 1 : 0) | (j.Get( "removed" ).AsBool() ? 2 : 0));
        e.m_DebugSize = static_cast<std::uint32_t>(j.Get( "debug_size" ).AsInt());
        e.m_TempSize = static_cast<std::uint32_t>(j.Get( "temp_size" ).AsInt());
        e.m_MemOff = j.Get( "mem_off" ).AsU64();

        const std::string & ap = j.Get( "align_pad" ).AsString();

        for (std::size_t i = 0; i + 1 < ap.size() && i / 2 < 12; i += 2)
        {
            unsigned v = 0;
            std::from_chars( ap.data() + i, ap.data() + i + 2, v, 16 );
            e.m_AlignPad[i / 2] = static_cast<std::uint8_t>(v);
        }

        const std::string & pad = j.Get( "pad" ).AsString();

        if (!pad.empty())
        {
            const auto c = pad.find( ':' );

            json::Value a( pad.substr( 0, c ) ), b( pad.substr( c + 1 ) );

            e.m_Pad = static_cast<std::uint8_t>(a.AsU64());
            e.m_Pad2 = static_cast<std::uint32_t>(b.AsU64());
        }

        if (e.m_MemType >= NUM_MEM_TYPES)
            return Fail( Errc::BAD_ARGUMENT, "mem_type out of range" );

        return e;
    }


    [[nodiscard]] inline json::Value HeaderToJson(
        const TocHeader & h
    )
    {
        json::Value j = json::Value::MakeObject();

        j["version"] = h.m_Version;
        j["heap_size"] = h.m_HeapSize;
        j["parm_count"] = h.m_ParmCount;

        json::Value t = json::Value::MakeArray();

        for (auto v : h.m_TotalMemByType)
            t.Push( v );

        j["total_mem_by_type"] = t;
        j["num_external_files"] = h.m_Bits0 & 0xFF;
        j["num_game_objects"] = (h.m_Bits0 >> 8) & 0x1FFFF;
        j["has_unoptimized_version"] = static_cast<bool>((h.m_Bits0 >> 25) & 1);
        j["bits0"] = std::format( "{:#010x}", h.m_Bits0 );
        j["bits1"] = std::format( "{:#010x}", h.m_Bits1 );

        return j;
    }

    [[nodiscard]] inline TocHeader HeaderFromJson( 
        const json::Value & j
    )
    {
        TocHeader h {};
        h.m_Magic = MAGIC;
        h.m_Version = static_cast<std::uint32_t>(j.Get( "version" ).AsInt( VERSION ));
        h.m_HeapSize = static_cast<std::uint32_t>(j.Get( "heap_size" ).AsInt());
        h.m_ParmCount = static_cast<std::uint32_t>(j.Get( "parm_count" ).AsInt());
        h.m_Bits0 = static_cast<std::uint32_t>(j.Get( "bits0" ).AsU64());
        h.m_Bits1 = static_cast<std::uint32_t>(j.Get( "bits1" ).AsU64());

        return h;
    }

    // Textures stored in WADs.

    // 'RenTextureLoadParmPS3' [0x68], followed by a truncated GNF header.
    struct TextureParm
    {
        std::uint32_t m_ClientType = 0; // UID [server 25, client 1].
        std::string m_Name;
        std::uint16_t m_Flags = 0; // bit0, never use texpacks [unless forced].
        std::uint8_t m_MipCountLow = 0; // 0xFF, not streamed
        std::uint8_t m_MipCountHigh = 0;
        std::uint16_t m_Width = 0, m_Height = 0;
        std::uint32_t m_UncompressedSize = 0;
        std::uint32_t m_ExtraDataOffset = 0;
        agc::TSharp m_Tsharp {}; // Descriptor of the WAD resident mips.
        std::uint32_t m_GnfStreamSize = 0;
        std::uint64_t m_ContentHash = 0; // 'USER' block.
        bool m_HasGnf = false;

        [[nodiscard]] bool Streamed() const noexcept
        {
            return m_MipCountLow != 0xFF && !(m_Flags & 1);
        }
    };

    inline constexpr std::size_t TEX_PARM_GNF_OFFSET = 0x68;

    [[nodiscard]] inline std::optional<TextureParm> ParseTextureParm(
        ByteSpan b
    )
    {
        if (b.size() < TEX_PARM_GNF_OFFSET + 48)
            return std::nullopt;

        ByteReader r( b );
        TextureParm p;
        p.m_ClientType = *r.At<std::uint32_t>( 0 );
        p.m_Name = std::string( FixedStr( reinterpret_cast<const char *>(b.data() + 0x0C), 56 ) );
        p.m_Flags = *r.At<std::uint16_t>( 0x44 );
        p.m_MipCountLow = *r.At<std::uint8_t>( 0x46 );
        p.m_MipCountHigh = *r.At<std::uint8_t>( 0x47 );
        p.m_Width = *r.At<std::uint16_t>( 0x48 );
        p.m_Height = *r.At<std::uint16_t>( 0x4A );
        p.m_UncompressedSize = *r.At<std::uint32_t>( 0x5C );
        p.m_ExtraDataOffset = *r.At<std::uint32_t>( 0x60 );

        if (*r.At<std::uint32_t>( TEX_PARM_GNF_OFFSET ) != 0x20464E47)
            return p;  // 'GNF '

        p.m_HasGnf = true;
        p.m_GnfStreamSize = *r.At<std::uint32_t>( TEX_PARM_GNF_OFFSET + 12 );
        p.m_Tsharp = agc::TSharp::From( b.subspan( TEX_PARM_GNF_OFFSET + 16, 32 ) );

        // USER block right after the T#. 'USER' u32 size, u64 content hash.
        if (auto u = r.At<std::uint32_t>( TEX_PARM_GNF_OFFSET + 48 ); u && *u == 0x52455355)
        {
            if (auto h = r.At<std::uint64_t>( TEX_PARM_GNF_OFFSET + 56 ))
                p.m_ContentHash = *h;
        }

        return p;
    }

    [[nodiscard]] inline bool IsTextureParm( 
        const Entry & e
    ) noexcept
    {
        return e.m_E.m_Id == 1 && e.m_E.m_ServerId == 25 && e.m_E.m_ClientId == 1;
    }

    // Pair each texture parm with its GPU chunk [same name, id 29, texture
    // server]. Streamed textures keep their low mips in the 'TEXLOWMIPS' arena,
    // fully WAD-resident ones in the GPU arena.
    struct WadTexture
    {
        std::size_t m_ParmIndex = 0;
        std::optional<std::size_t> m_GpuIndex;
        TextureParm m_Parm;
    };

    [[nodiscard]] inline std::vector<WadTexture> FindTextures(
        const Wad & w,
        ByteSpan data
    )
    {
        std::vector<WadTexture> out;
        std::map<std::string, std::size_t> gpu;

        for (std::size_t i = 0; i < w.m_Entries.size(); ++i)
        {
            const auto & e = w.m_Entries[i];

            if (e.m_E.m_Id == 29 && e.m_E.m_ServerId == 25)
                gpu.emplace( e.Name(), i );
        }

        for (std::size_t i = 0; i < w.m_Entries.size(); ++i)
        {
            const auto & e = w.m_Entries[i];

            if (!IsTextureParm( e ))
                continue;

            auto p = ParseTextureParm( ChunkData( data, e ) );

            if (!p || !p->m_HasGnf)
                continue;

            WadTexture t;
            t.m_ParmIndex = i;
            t.m_Parm = *p;

            // Shipped WADs always place the GPU chunk right before its parm,
            // fall back to a name lookup otherwise.
            if (i > 0 && w.m_Entries[i - 1].m_E.m_Id == 29 && w.m_Entries[i - 1].m_E.m_ServerId == 25 && w.m_Entries[i - 1].Name() == e.Name())
            {
                t.m_GpuIndex = i - 1;
            }
            else if (auto it = gpu.find( e.Name() ); it != gpu.end())
            {
                t.m_GpuIndex = it->second;
            }

            out.push_back( std::move( t ) );
        }

        return out;
    }
}