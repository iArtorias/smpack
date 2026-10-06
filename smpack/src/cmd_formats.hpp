// 'info', 'list', 'extract' for every format etc.

#pragma once

#include "cli.hpp"
#include "cmd_tex.hpp"

namespace smpack::cli
{
    // WAD

    [[nodiscard]] inline std::string EntryLabel( 
        const wad::Entry & en
    )
    {
        std::string t( wad::ChunkName( en.m_E.m_Id ) );

        if (en.m_E.m_Id == 1 && !wad::ServerName( en.m_E.m_ServerId ).empty())
            t += "-" + std::string( wad::ServerName( en.m_E.m_ServerId ) );

        return t;
    }


    inline void PrintWadInfo(
        const Input & in,
        const wad::Wad & w
    )
    {
        const auto & h = w.m_Header;

        println( "wad (WTOC v{})", h.m_Version );
        println( "  file              {} ({}{})", in.m_Path.string(), io::Human( in.m_DiskSize ), in.m_WasLz4 ? ", LZ4" : "" );
        println( "  decompressed      {}", io::Human( in.m_Data.size() ) );
        println( "  entries           {}", h.m_NumEntries );
        println( "  heap size         {}", io::Human( h.m_HeapSize ) );
        println( "  parm count        {}", h.m_ParmCount );
        println( "  external files    {}", h.m_Bits0 & 0xFF );
        println( "  game objects      {}", (h.m_Bits0 >> 8) & 0x1FFFF );

        for (std::uint32_t k = 0; k < wad::NUM_MEM_TYPES; ++k)
            if (h.m_TotalMemByType[k])
                println( "  arena {:<14} {}", wad::MemTypeName( static_cast<std::uint8_t>( k ) ), io::Human( h.m_TotalMemByType[k] ) );

        std::map<std::string, std::pair<std::size_t, std::uint64_t>> by;
        for (const auto & e : w.m_Entries)
        {
            auto & b = by[EntryLabel( e )];
            ++b.first;
            b.second += e.m_E.m_Length;
        }

        println( "  chunk types:" );

        for (const auto & [k, v] : by)
            println( "    {:<24} {:>6}  {:>10}", k, v.first, io::Human( v.second ) );

        const auto texs = wad::FindTextures( w, in.m_Data );

        if (!texs.empty())
            println( "  textures          {}", texs.size() );
    }


    inline void PrintWadList(
        const wad::Wad & w,
        const Args & args
    )
    {
        const auto filter = args.Get( "--filter" );
        if (args.Has( "--json" ))
        {
            json::Value arr = json::Value::MakeArray();

            for (std::size_t i = 0; i < w.m_Entries.size(); ++i)
            {
                const auto & e = w.m_Entries[i];

                if (filter && !Icontains( e.Name(), *filter ) && !Icontains( EntryLabel( e ), *filter ))
                    continue;

                json::Value j = json::Value::MakeObject();

                j["index"] = i;
                j["block"] = e.m_Block;
                j["type"] = EntryLabel( e );
                j["name"] = e.Name();
                j["length"] = e.m_E.m_Length;
                j["align"] = e.m_E.m_ChunkAlign;
                j["mem"] = e.m_E.m_MemType;
                j["version"] = e.m_E.m_Version;
                j["offset"] = e.m_DataOff;

                arr.Push( std::move( j ) );
            }

            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "list";
            doc["kind"] = "wad";
            doc["entries"] = arr;

            print( "{}", json::Dump( doc ) );
            return;
        }

        println( "{:>5} {:>3} {:<22} {:<48} {:>10} {:>6} {:>3} {:>5} {:>10}", "idx", "blk", "type", "name", "length", "align", "mem", "ver", "offset" );

        for (std::size_t i = 0; i < w.m_Entries.size(); ++i)
        {
            const auto & e = w.m_Entries[i];

            if (filter && !Icontains( e.Name(), *filter ) && !Icontains( EntryLabel( e ), *filter ))
                continue;

            println( "{:>5} {:>3} {:<22} {:<48} {:>10} {:>6} {:>3} {:>#5x} {:>#10x}{}", i, e.m_Block, EntryLabel( e ), e.Name(), e.m_E.m_Length,
                e.m_E.m_ChunkAlign, e.m_E.m_MemType, e.m_E.m_Version, e.m_DataOff, e.EndOfBlock() ? "  <eob>" : "" );
        }
    }


    [[nodiscard]] inline std::string ChunkFileName(
        std::size_t i,
        const wad::Entry & e
    )
    {
        return std::format( "{:05}_{}_{}.bin", i, EntryLabel( e ), io::SafeName( e.Name() ) );
    }


    [[nodiscard]] inline Result<void> ExtractWad(
        const Input & in,
        const wad::Wad & w,
        const fs::path & out,
        const Args & args
    )
    {
        json::Value man = json::Value::MakeObject();

        man["smpack"] = "wad";
        man["source"] = GameName( in.m_Path );
        man["lz4"] = in.m_WasLz4;
        man["header"] = wad::HeaderToJson( w.m_Header );

        json::Value ents = json::Value::MakeArray();

        const fs::path cdir = out / "chunks";
        std::size_t files = 0;
        for (std::size_t i = 0; i < w.m_Entries.size(); ++i)
        {
            const auto & en = w.m_Entries[i];
            json::Value j = wad::EntryToJson( en );

            if (en.m_E.m_Id != 25)
            {
                const std::string fn = ChunkFileName( i, en );
                SMPACK_TRYV( io::WriteFile( cdir / fn, wad::ChunkData( in.m_Data, en ) ) );
                j["file"] = "chunks/" + fn;
                ++files;
            }

            if (en.m_E.m_DebugSize)
            {
                const std::string fn = ChunkFileName( i, en ) + ".debug";
                SMPACK_TRYV( io::WriteFile( cdir / fn, wad::DebugData( in.m_Data, en ) ) );
                j["debug_file"] = "chunks/" + fn;
            }

            if (en.m_E.m_TempSize)
            {
                const std::string fn = ChunkFileName( i, en ) + ".temp";
                SMPACK_TRYV( io::WriteFile( cdir / fn, wad::TempData( in.m_Data, en ) ) );
                j["temp_file"] = "chunks/" + fn;
            }

            ents.Push( std::move( j ) );
        }

        man["entries"] = ents;

        SMPACK_TRYV( io::WriteText( out / "manifest.json", json::Dump( man ) ) );
        std::size_t ntex = 0;

        if (!args.Has( "--no-dds" ))
        {
            SMPACK_TRY( n, ExportWadTextures( in, w, out / "textures", args ) );
            ntex = n;
        }

        println( "extracted {} chunk(s){} to {}", files, ntex ? std::format( " and {} texture(s)", ntex ) : "", out.string() );
        println( "rebuild with: smpack wad pack {} -o <file>", out.string() );
        return {};
    }


    [[nodiscard]] inline Result<void> CmdWadPack(
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "wad pack <extracted dir> -o <out.wad>" );

        const fs::path dir = args.m_Pos[0];

        SMPACK_TRY( raw, io::ReadFile( dir / "manifest.json" ) );
        SMPACK_TRY( man, json::Parse( { reinterpret_cast<const char *>(raw.data()), raw.size() } ) );

        if (man.Get( "smpack" ).AsString() != "wad")
            return Fail( Errc::BAD_ARGUMENT, "manifest.json is not a WAD manifest" );

        wad::TocHeader h = wad::HeaderFromJson( man.Get( "header" ) );

        std::vector<wad::Entry> es;
        std::vector<wad::ChunkPayload> pl;
        for (const auto & j : man.Get( "entries" ).AsArray())
        {
            SMPACK_TRY( te, wad::EntryFromJson( j ) );

            wad::Entry en;
            en.m_E = te;
            wad::ChunkPayload p;

            if (auto f = j.Get( "file" ).AsString(); !f.empty())
            {
                SMPACK_TRY( d, io::ReadFile( dir / f ) );
                p.m_Data = std::move( d );
            }

            if (auto f = j.Get( "debug_file" ).AsString(); !f.empty())
            {
                SMPACK_TRY( d, io::ReadFile( dir / f ) );
                p.m_Debug = std::move( d );
            }

            if (auto f = j.Get( "temp_file" ).AsString(); !f.empty())
            {
                SMPACK_TRY( d, io::ReadFile( dir / f ) );
                p.m_Temp = std::move( d );
            }

            es.push_back( en );
            pl.push_back( std::move( p ) );
        }

        SMPACK_TRY( bytes, wad::Build( h, es, pl ) );

        const fs::path out = args.GetOr( "--output", (dir.filename().string() + ".wad") );
        const bool compress = !args.Has( "--no-compress" ) && man.Get( "lz4" ).AsBool( true );

        SMPACK_TRYV( WriteMaybeLz4( out, bytes, compress, args.GetInt( "--level", 0 ) ) );

        println( "wrote {} ({} entries, {}{})", out.string(), es.size(), io::Human( bytes.size() ), compress ? ", LZ4" : "" );
        return {};
    }


    [[nodiscard]] inline Result<std::size_t> ResolveChunk(
        const wad::Wad & w,
        std::string_view sel
    )
    {
        if (sel.starts_with( "#" ))
        {
            auto v = ParseU64( sel.substr( 1 ) );

            if (!v || *v >= w.m_Entries.size())
                return Fail( Errc::BAD_ARGUMENT, std::format( "no chunk {}", sel ) );

            return static_cast<std::size_t>(*v);
        }

        std::string name( sel ), type;
        if (auto at = name.find( '@' ); at != std::string::npos)
        {
            type = name.substr( at + 1 );
            name = name.substr( 0, at );
        }

        std::vector<std::size_t> hits;
        for (std::size_t i = 0; i < w.m_Entries.size(); ++i)
        {
            const auto & e = w.m_Entries[i];

            if (e.Name() == name && (type.empty() || Icontains( EntryLabel( e ), type )))
                hits.push_back( i );
        }

        if (hits.empty())
            return Fail( Errc::NOT_FOUND, std::format( "no chunk named '{}'", sel ) );

        if (hits.size() > 1)
        {
            std::string opts;
            for (auto i : hits)
                opts += std::format( " #{}({})", i, EntryLabel( w.m_Entries[i] ) );

            return Fail( Errc::BAD_ARGUMENT, std::format( "'{}' is ambiguous: {}. use #index or name@type", sel, opts ) );
        }

        return hits[0];
    }


    [[nodiscard]] inline Result<void> CmdWadReplace( 
        const Args & args
    )
    {
        if (args.m_Pos.size() < 3)
            return Fail( Errc::BAD_ARGUMENT, "wad replace <in.wad> <#index|name[@type]> <file> [-o out]" );

        SMPACK_TRY( e, OpenWadEdit( args.m_Pos[0] ) );
        SMPACK_TRY( idx, ResolveChunk( e.m_W, args.m_Pos[1] ) );
        SMPACK_TRY( d, io::ReadFile( args.m_Pos[2] ) );

        println( "chunk #{} {} '{}': {} -> {} bytes", idx, EntryLabel( e.m_W.m_Entries[idx] ), e.m_W.m_Entries[idx].Name(),
            e.m_Payloads[idx].m_Data.size(), d.size() );

        e.m_Payloads[idx].m_Data = std::move( d );

        if (auto f = args.Get( "--debug-file" ))
        {
            SMPACK_TRY( x, io::ReadFile( *f ) );
            e.m_Payloads[idx].m_Debug = std::move( x );
        }

        if (auto f = args.Get( "--temp-file" ))
        {
            SMPACK_TRY( x, io::ReadFile( *f ) );
            e.m_Payloads[idx].m_Temp = std::move( x );
        }

        const fs::path out_dir = args.GetOr( "--output", "out" );

        SMPACK_TRY( outp, SaveWadEdit( e, out_dir, args ) );
        println( "wrote {}", outp.string() );
        return {};
    }

    // lodpack

    struct LodSource
    {
        Input m_In;
        lodpack::Pack m_Pack;
        bool m_HasPayload = false;
        std::string m_Name;
    };

    [[nodiscard]] inline Result<LodSource> OpenLodpack(
        const fs::path & p
    )
    {
        LodSource s;
        s.m_Name = PackBase( p );
        fs::path payload = p;

        if (p.string().ends_with( ".toc" ))
            payload = fs::path( p.string().substr( 0, p.string().size() - 4 ) );

        std::error_code ec;
        SMPACK_TRY( in, Load( fs::exists( payload, ec ) ? payload : p ) );

        s.m_In = std::move( in );
        SMPACK_TRY( pk, lodpack::Parse( s.m_In.m_Data ) );

        s.m_Pack = std::move( pk );
        s.m_HasPayload = s.m_In.m_Data.size() > s.m_Pack.TocSize();
        return s;
    }

    inline void PrintLodInfo(
        const LodSource & s
    )
    {
        std::uint64_t total = 0;

        for (const auto & g : s.m_Pack.m_Groups)
            total += g.m_GroupDataSize;

        println( "lodpack" );
        println( "  file         {} ({})", s.m_In.m_Path.string(), io::Human( s.m_In.m_DiskSize ) );
        println( "  groups       {}", s.m_Pack.m_Header.m_GroupCount );
        println( "  blocks       {}", s.m_Pack.m_Header.m_BlockCount );
        println( "  flags        {:#x}{}", s.m_Pack.m_Header.m_HeaderFlags, s.m_Pack.m_Header.m_HeaderFlags & 1 ? " (use_lod_pack)" : "" );
        println( "  group data   {}", io::Human( total ) );
        println( "  payload      {}", s.m_HasPayload ? "present" : "missing (toc only)" );
    }


    inline void PrintLodList(
        const LodSource & s,
        bool as_json = false
    )
    {
        if (as_json)
        {
            std::vector<std::size_t> n( s.m_Pack.m_Groups.size() );

            for (const auto & b : s.m_Pack.m_Blocks)
                ++n[b.m_GroupIdx];

            json::Value arr = json::Value::MakeArray();

            for (std::size_t i = 0; i < s.m_Pack.m_Groups.size(); ++i)
            {
                const auto & g = s.m_Pack.m_Groups[i];
                json::Value j = json::Value::MakeObject();

                j["index"] = i;
                j["offset"] = g.m_FileOffset;
                j["hash"] = json::Hex64( g.m_GroupDataHash );
                j["size"] = g.m_GroupDataSize;
                j["blocks"] = n[i];

                arr.Push( std::move( j ) );
            }

            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "list";
            doc["kind"] = "lodpack";
            doc["entries"] = arr;

            print( "{}", json::Dump( doc ) );
            return;
        }

        println( "{:>5}  {:>12}  {:>18}  {:>10}  {:>6}", "group", "offset", "hash", "size", "blocks" );

        std::vector<std::size_t> cnt( s.m_Pack.m_Groups.size() );

        for (const auto & b : s.m_Pack.m_Blocks)
            ++cnt[b.m_GroupIdx];

        for (std::size_t i = 0; i < s.m_Pack.m_Groups.size(); ++i)
        {
            const auto & g = s.m_Pack.m_Groups[i];
            println( "{:>5}  {:#12x}  {:#018x}  {:>10}  {:>6}", i, g.m_FileOffset, g.m_GroupDataHash, g.m_GroupDataSize, cnt[i] );
        }
    }


    [[nodiscard]] inline Result<void> ExtractLodpack(
        const LodSource & s,
        const fs::path & out,
        const Args & args
    )
    {
        if (!s.m_HasPayload)
            return Fail( Errc::NOT_FOUND, "the .lodpack payload is needed to extract geometry" );

        json::Value man = json::Value::MakeObject();

        man["smpack"] = "lodpack";
        man["pack"] = s.m_Name;
        man["header_flags"] = s.m_Pack.m_Header.m_HeaderFlags;

        json::Value gs = json::Value::MakeArray();

        for (std::size_t gi = 0; gi < s.m_Pack.m_Groups.size(); ++gi)
        {
            const auto & g = s.m_Pack.m_Groups[gi];

            json::Value j = json::Value::MakeObject();

            j["index"] = gi;
            j["hash"] = json::Hex64( g.m_GroupDataHash );
            j["size"] = g.m_GroupDataSize;
            j["flags"] = g.m_GroupFlags;
            j["file"] = std::format( "groups/{:04}_{:016x}.bin", gi, g.m_GroupDataHash );

            json::Value bl = json::Value::MakeArray();

            for (const auto & b : s.m_Pack.m_Blocks)
            {
                if (b.m_GroupIdx != gi)
                    continue;

                json::Value x = json::Value::MakeObject();

                x["hash"] = json::Hex64( b.m_BlockDataHash );
                x["offset"] = b.m_GroupOffset;
                x["size"] = b.m_BlockDataSize;
                x["flags"] = b.m_BlockFlags;
                bl.Push( x );
            }

            j["blocks"] = bl;
            gs.Push( j );

            SMPACK_TRY( gb, lodpack::GroupBytes( s.m_In.m_Data, g ) );
            SMPACK_TRYV( io::WriteFile( out / std::format( "groups/{:04}_{:016x}.bin", gi, g.m_GroupDataHash ), gb ) );

            if (args.Has( "--blocks" ))
            {
                for (const auto & b : s.m_Pack.m_Blocks)
                {
                    if (b.m_GroupIdx != gi)
                        continue;

                    SMPACK_TRYV( io::WriteFile( out / "blocks" / std::format( "{:016x}.bin", b.m_BlockDataHash ),
                        gb.subspan( b.m_GroupOffset, b.m_BlockDataSize ) ) );
                }
            }
        }

        man["groups"] = gs;

        SMPACK_TRYV( io::WriteText( out / "manifest.json", json::Dump( man ) ) );
        println( "extracted {} group(s){} to {}", s.m_Pack.m_Groups.size(), args.Has( "--blocks" ) ? " and their blocks" : "", out.string() );
        return {};
    }


    // LOD patch.
    [[nodiscard]] inline Result<void> CmdLodPatch( const Args & args )
    {
        const auto packp = args.Get( "--pack" );

        if (!packp || args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "lod patch --pack <orig.lodpack> <block .bin files/dirs> [--patch NAME] [-o DIR]" );

        SMPACK_TRY( src, OpenLodpack( *packp ) );

        if (!src.m_HasPayload)
            return Fail( Errc::NOT_FOUND, "the original .lodpack payload is required" );

        SMPACK_TRY( files, CollectFiles( args.m_Pos, ".bin" ) );

        std::map<std::uint64_t, Bytes> repl;
        for (const auto & f : files)
        {
            auto h = ParseU64( f.stem().string() );

            if (!h)
                return Fail( Errc::BAD_ARGUMENT, std::format( "'{}': file name must be the 16 hex digit block hash", f.string() ) );

            SMPACK_TRY( d, io::ReadFile( f ) );
            repl[*h] = std::move( d );
        }

        std::map<std::uint32_t, std::vector<lodpack::BlockEntry>> touched;

        for (const auto & b : src.m_Pack.m_Blocks)
            if (repl.count( b.m_BlockDataHash )) touched[b.m_GroupIdx];

        for (const auto & [h, d] : repl)
        {
            bool found = false;

            for (const auto & b : src.m_Pack.m_Blocks)
                found |= b.m_BlockDataHash == h;

            if (!found)
                return Fail( Errc::NOT_FOUND, std::format( "block {:016x} is not in {}", h, src.m_Name ) );
        }

        std::vector<lodpack::NewGroup> groups;

        for (const auto & [gi, unused] : touched)
        {
            (void)unused;
            const auto & g = src.m_Pack.m_Groups[gi];

            SMPACK_TRY( gb, lodpack::GroupBytes( src.m_In.m_Data, g ) );

            std::vector<lodpack::BlockEntry> blocks;
            for (const auto & b : src.m_Pack.m_Blocks)
                if (b.m_GroupIdx == gi)
                    blocks.push_back( b );

            groups.push_back( lodpack::RebuildGroup( g, gb, std::move( blocks ), repl ) );
        }

        SMPACK_TRY( built, lodpack::Build( std::move( groups ), src.m_Pack.m_Header.m_HeaderFlags | 1u, 1 ) );

        const std::string patch = args.GetOr( "--patch", "smpack_lodpatch" );
        const fs::path out = args.GetOr( "--output", "." );
        const fs::path pp = out / (patch + ".lodpack"), tp = out / (patch + ".lodpack.toc");

        SMPACK_TRYV( io::WriteFile( pp, built.m_Pack ) );
        SMPACK_TRYV( WriteMaybeLz4( tp, built.m_Toc, !args.Has( "--no-compress" ), 9 ) );

        println( "wrote {} and {} ({} block(s) replaced in {} group(s))", pp.string(), tp.filename().string(), repl.size(), touched.size() );

        if (auto game = args.Get( "--install" ))
        {
            SMPACK_TRYV( InstallFiles( *game, { pp, tp }, std::nullopt, patch ) );
        }
        else
        {
            println( "\nTo activate, copy both files into 'exec/wad/pc_le' and add \"{}\" to \"patch-lodpacks\" in "
                "'exec/boot-options.json' (or run again with '--install <game dir>').",
                patch );
        }

        return {};
    }

    // Anim file sets [.as].

    inline void PrintAsList(
        const std::vector<animset::FileSet> & sets,
        const Args & args
    )
    {
        const auto filter = args.Get( "--filter" );

        if (args.Has( "--json" ))
        {
            json::Value arr = json::Value::MakeArray();

            for (std::size_t i = 0; i < sets.size(); ++i)
            {
                const auto & s = sets[i];

                if (filter && !Icontains( s.Name(), *filter ))
                    continue;

                json::Value j = json::Value::MakeObject();

                j["index"] = i;
                j["name"] = s.Name();
                j["offset"] = s.m_Offset;
                j["size"] = s.m_Size;
                j["name_hash"] = json::Hex64( s.m_H.m_NameHash );
                j["id"] = s.m_H.m_Id;
                j["files"] = s.m_H.m_NumFiles;

                arr.Push( std::move( j ) );
            }

            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "list";
            doc["kind"] = "animset";
            doc["entries"] = arr;

            print( "{}", json::Dump( doc ) );
            return;
        }

        println( "{:>4}  {:>10}  {:>10}  {:>18}  {:>5}  {:>6}  {}", "idx", "offset", "size", "name_hash", "id", "files", "name" );

        for (std::size_t i = 0; i < sets.size(); ++i)
        {
            const auto & s = sets[i];

            if (filter && !Icontains( s.Name(), *filter ))
                continue;

            println( "{:>4}  {:#10x}  {:>10}  {:#018x}  {:>5}  {:>6}  {}", i, s.m_Offset, s.m_Size, s.m_H.m_NameHash, s.m_H.m_Id, s.m_H.m_NumFiles, s.Name() );
        }
    }


    [[nodiscard]] inline Result<void> ExtractAs(
        const Input & in,
        const std::vector<animset::FileSet> & sets,
        const fs::path & out,
        const Args & args
    )
    {
        const auto filter = args.Get( "--filter" );

        json::Value man = json::Value::MakeObject();

        man["smpack"] = "animset";
        man["source"] = GameName( in.m_Path );

        json::Value arr = json::Value::MakeArray();

        std::size_t n = 0;
        for (std::size_t i = 0; i < sets.size(); ++i)
        {
            const auto & s = sets[i];

            json::Value j = json::Value::MakeObject();

            j["index"] = i;
            j["name"] = s.Name();
            j["name_hash"] = json::Hex64( s.m_H.m_NameHash );
            j["id"] = s.m_H.m_Id;
            j["type_hash"] = std::format( "{:#010x}", s.m_H.m_TypeHash );
            j["flags"] = json::Hex64( s.m_H.m_Flags );
            j["num_files"] = s.m_H.m_NumFiles;
            j["size"] = s.m_Size;

            const std::string fn = std::format( "{:04}_{}.anmset", i, io::SafeName( s.Name() ) );
            j["file"] = fn;
            arr.Push( j );

            if (filter && !Icontains( s.Name(), *filter ))
                continue;

            SMPACK_TRYV( io::WriteFile( out / fn, in.m_Data.subspan( static_cast<std::size_t>(s.m_Offset), static_cast<std::size_t>(s.m_Size) ) ) );
            ++n;
        }

        man["sets"] = arr;

        SMPACK_TRYV( io::WriteText( out / "manifest.json", json::Dump( man ) ) );
        println( "extracted {} anim set(s) to {}", n, out.string() );
        return {};
    }


    // 'anim replace <x.as> <x.wad> --set <name|#idx> --blob <file.anmset> [-o DIR]'.
    [[nodiscard]] inline Result<void> CmdAnimReplace(
        const Args & args
    )
    {
        if (args.m_Pos.size() < 2)
        {
            return Fail( Errc::BAD_ARGUMENT, "anim replace <file.as> <file.wad> --set <name|#index> --blob <file.anmset> [--set ... --blob ...] [-o DIR]" );
        }

        SMPACK_TRY( as_in, Load( args.m_Pos[0] ) );
        SMPACK_TRY( sets, animset::Parse( as_in.m_Data ) );
        SMPACK_TRY( we, OpenWadEdit( args.m_Pos[1] ) );

        const auto sel = args.All( "--set" );
        const auto blobs = args.All( "--blob" );

        if (sel.empty() || sel.size() != blobs.size())
            return Fail( Errc::BAD_ARGUMENT, "give one --blob per --set" );

        std::vector<Bytes> out_blobs;
        for (const auto & s : sets)
        {
            auto d = as_in.m_Data.subspan( static_cast<std::size_t>(s.m_Offset), static_cast<std::size_t>(s.m_Size) );
            out_blobs.emplace_back( d.begin(), d.end() );
        }

        for (std::size_t k = 0; k < sel.size(); ++k)
        {
            std::optional<std::size_t> idx;

            if (sel[k].starts_with( "#" )) idx = ParseU64( sel[k].substr( 1 ) );
            else
                for (std::size_t i = 0; i < sets.size(); ++i)
                    if (sets[i].Name() == sel[k])
                        idx = i;

            if (!idx || *idx >= sets.size())
                return Fail( Errc::NOT_FOUND, std::format( "anim set '{}' not found", sel[k] ) );

            SMPACK_TRY( b, io::ReadFile( blobs[k] ) );

            if (b.size() < sizeof( animset::FileSetHeader ))
                return Fail( Errc::BAD_ARGUMENT, std::format( "'{}' is not an anim set blob", blobs[k] ) );

            if (!args.Has( "--no-identity" ))
                animset::AdoptIdentity( b, sets[*idx].m_H );

            println( "  set #{} '{}': {} -> {} bytes", *idx, sets[*idx].Name(), sets[*idx].m_Size, b.size() );
            out_blobs[*idx] = std::move( b );
        }

        SMPACK_TRY( new_as, animset::Build( out_blobs ) );
        SMPACK_TRY( new_sets, animset::Parse( new_as ) );

        std::vector<animset::StubPatch> patches;
        for (std::size_t i = 0; i < sets.size(); ++i)
        {
            const auto & o = sets[i];
            const auto & n = new_sets[i];

            if (o.m_Offset != n.m_Offset || o.m_Size != n.m_Size)
                patches.push_back( { o.m_H.m_NameHash, static_cast<std::uint32_t>( o.m_Offset ), static_cast<std::uint32_t>( o.m_Size ),
                    static_cast<std::uint32_t>( n.m_Offset ), static_cast<std::uint32_t>( n.m_Size ) } );
        }

        // Patch stubs inside the WAD payloads [anim server parms].
        std::size_t changed = 0;
        for (std::size_t i = 0; i < we.m_W.m_Entries.size(); ++i)
        {
            const auto & en = we.m_W.m_Entries[i];

            if (en.m_E.m_Id != 1 || en.m_E.m_ServerId != 2)
                continue;

            auto & d = we.m_Payloads[i].m_Data;

            for (std::size_t p = 8; p + 8 <= d.size(); p += 8)
            {
                std::uint64_t hv = 0;
                std::memcpy( &hv, d.data() + p, 8 );

                for (const auto & sp : patches)
                {
                    if (hv != sp.m_NameHash)
                        continue;

                    std::uint32_t o = 0, s = 0;
                    std::memcpy( &o, d.data() + p - 8, 4 );
                    std::memcpy( &s, d.data() + p - 4, 4 );

                    if (o != sp.m_OldOffset || s != sp.m_OldSize)
                        continue;

                    std::memcpy( d.data() + p - 8, &sp.m_NewOffset, 4 );
                    std::memcpy( d.data() + p - 4, &sp.m_NewSize, 4 );
                    ++changed;
                }
            }
        }

        if (patches.size() && changed < patches.size())
        {
            println( "  warning: {} set(s) moved but only {} WAD stub(s) were found and updated", patches.size(), changed );
        }

        const fs::path out = args.GetOr( "--output", "out" );
        const fs::path asp = out / GameName( as_in.m_Path );

        SMPACK_TRYV( io::WriteFile( asp, new_as ) );
        SMPACK_TRY( wp, SaveWadEdit( we, out, args ) );

        println( "wrote {} and {} ({} stub(s) updated)", asp.string(), wp.string(), changed );

        if (auto game = args.Get( "--install" ))
            SMPACK_TRYV( InstallFiles( *game, { asp, wp }, std::nullopt, std::nullopt ) );

        return {};
    }

    // shaderpack

    inline void PrintShaderInfo(
        const Input & in,
        const shaderpack::Package & p
    )
    {
        const auto & h = p.m_Header;

        println( "shaderpack (v{})", h.m_Version );
        println( "  file                 {} ({}{})", in.m_Path.string(), io::Human( in.m_DiskSize ), in.m_WasLz4 ? ", LZ4" : "" );
        println( "  unique shaders       {}", h.m_NumUniqueShaders );
        println( "  root signatures      {}", h.m_NumRootSignatures );
        println( "  shader combos        {}", h.m_NumShaderCombos );
        println( "  psos                 {}", h.m_NumPsos );
        println( "  wad shaders          {} ({})", h.m_NumWadShaders, io::Human( h.m_NumWadShaderBytes ) );
        println( "  shader bin bytes     {}{}", io::Human( h.m_NumUniqueShaderCompressed ), p.BinsCompressed() ? " (compressed)" : "" );

        for (const auto & s : p.m_Sections)
            println( "  section {:<24} {:#10x} {:>10}", s.m_Name, s.m_Offset, io::Human( s.m_Size ) );
    }


    inline void PrintShaderList(
        const shaderpack::Package & p,
        const Args & args
    )
    {
        const auto filter = args.Get( "--filter" );

        if (args.Has( "--json" ))
        {
            json::Value arr = json::Value::MakeArray();

            for (std::size_t i = 0; i < p.m_Shaders.size(); ++i)
            {
                const auto & s = p.m_Shaders[i];

                if (filter && !Icontains( shaderpack::ShaderName( s ), *filter ))
                    continue;

                json::Value j = json::Value::MakeObject();

                j["index"] = i;
                j["name"] = shaderpack::ShaderName( s );
                j["bytecode_hash"] = json::Hex64( s.m_BytecodeHash );
                j["profile"] = std::string( shaderpack::ProfileName( s.m_Profile ) );
                j["offset"] = s.m_BinOffset;
                j["size"] = s.m_BinSize;

                arr.Push( std::move( j ) );
            }

            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "list";
            doc["kind"] = "shaderpack";
            doc["entries"] = arr;

            print( "{}", json::Dump( doc ) );
            return;
        }

        println( "{:>5}  {:>18}  {:>8}  {:>10}  {:>10}  {}", "idx", "bytecode_hash", "profile", "offset", "size", "name" );

        for (std::size_t i = 0; i < p.m_Shaders.size(); ++i)
        {
            const auto & s = p.m_Shaders[i];

            if (filter && !Icontains( shaderpack::ShaderName( s ), *filter ))
                continue;

            println( "{:>5}  {:#018x}  {:>8}  {:#10x}  {:>10}  {}", i, s.m_BytecodeHash, shaderpack::ProfileName( s.m_Profile ), s.m_BinOffset,
                s.m_BinSize, shaderpack::ShaderName( s ) );
        }
    }


    [[nodiscard]] inline Result<void> ExtractShaders(
        const Input & in,
        const shaderpack::Package & p,
        const fs::path & out,
        const Args & args
    )
    {
        json::Value man = json::Value::MakeObject();

        man["smpack"] = "shaderpack";
        man["source"] = GameName( in.m_Path );

        json::Value arr = json::Value::MakeArray();

        const auto filter = args.Get( "--filter" );
        std::size_t n = 0;

        for (std::size_t i = 0; i < p.m_Shaders.size(); ++i)
        {
            const auto & s = p.m_Shaders[i];

            const std::string nm = shaderpack::ShaderName( s );
            const std::string fn = std::format( "shaders/{:04}_{}.dxbc", i, io::SafeName( nm.empty() ? std::format( "{:016x}", s.m_BytecodeHash ) : nm ) );

            json::Value j = json::Value::MakeObject();

            j["index"] = i;
            j["name"] = nm;
            j["bytecode_hash"] = json::Hex64( s.m_BytecodeHash );
            j["profile"] = s.m_Profile;
            j["cs_group"] = json::Value( json::Array { s.m_CsGroupX, s.m_CsGroupY, s.m_CsGroupZ } );
            j["vs_num_outputs"] = s.m_VsNumOutputs;
            j["size"] = s.m_BinSize;
            j["file"] = fn;
            arr.Push( j );

            if (filter && !Icontains( nm, *filter ))
                continue;

            if (!p.BinsCompressed())
            {
                SMPACK_TRYV( io::WriteFile( out / fn, shaderpack::ShaderBytes( in.m_Data, p, s ) ) );
                ++n;
            }
        }

        man["shaders"] = arr;

        for (const auto & s : p.m_Sections)
        {
            if (s.m_Name == "unique_shader_bins" || s.m_Name == "unique_shader_records" || !s.m_Size)
                continue;

            SMPACK_TRYV( io::WriteFile( out / "sections" / (s.m_Name + ".bin"),
                in.m_Data.subspan( static_cast<std::size_t>(s.m_Offset), static_cast<std::size_t>(s.m_Size) ) ) );
        }

        SMPACK_TRYV( io::WriteText( out / "manifest.json", json::Dump( man ) ) );
        println( "extracted {} shader(s) to {}", n, out.string() );
        return {};
    }


    [[nodiscard]] inline Result<void> CmdShaderReplace(
        const Args & args
    )
    {
        if (args.m_Pos.size() < 3)
            return Fail( Errc::BAD_ARGUMENT, "shader replace <pack.shaderpack> <#index|name|hash> <file.dxbc> [-o out]" );

        SMPACK_TRY( in, Load( args.m_Pos[0] ) );
        SMPACK_TRY( p, shaderpack::Parse( in.m_Data ) );

        std::optional<std::size_t> idx;
        const std::string sel = args.m_Pos[1];

        if (sel.starts_with( "#" ))
            idx = ParseU64( sel.substr( 1 ) );

        const auto hv = ParseU64( sel );

        for (std::size_t i = 0; i < p.m_Shaders.size() && !idx; ++i)
            if (shaderpack::ShaderName( p.m_Shaders[i] ) == sel || (hv && p.m_Shaders[i].m_BytecodeHash == *hv))
                idx = i;

        if (!idx || *idx >= p.m_Shaders.size())
            return Fail( Errc::NOT_FOUND, std::format( "shader '{}' not found", sel ) );

        SMPACK_TRY( blob, io::ReadFile( args.m_Pos[2] ) );
        SMPACK_TRY( out, shaderpack::Rebuild( in.m_Data, p, { { *idx, blob } } ) );

        const fs::path outp = args.GetOr( "--output", (fs::path( "out" ) / GameName( in.m_Path )).string() );

        SMPACK_TRYV( WriteMaybeLz4( outp, out, in.m_WasLz4 && !args.Has( "--no-compress" ), args.GetInt( "--level", 0 ) ) );

        println( "wrote {} (shader #{} '{}' replaced)", outp.string(), *idx, shaderpack::ShaderName( p.m_Shaders[*idx] ) );
        println( "note: D3D12 requires validly signed DXIL. Resign edited shaders (e.g. 'dxil-signing' tool) before use." );
        return {};
    }

    // audiopack [Wwise .wem streams].

    struct AudioSource
    {
        fs::path m_TocPath;
        std::string m_Base; // Path prefix for parts. '<base>.<n>.audiopack'.
        audiopack::Toc m_Toc;
        std::vector<io::MappedFile> m_Parts;
    };

    // Read '<file>.smpack-orig' backups where present.
    [[nodiscard]] inline Result<AudioSource> OpenAudiopack( 
        const fs::path & p, 
        bool pristine = false
    )
    {
        auto orig = [&] ( const fs::path & f )
        {
            std::error_code ec;
            const fs::path b = f.string() + ".smpack-orig";
            return pristine && fs::exists( b, ec ) ? b : f;
        };

        AudioSource a;
        std::string s = p.string();

        // Accept the '.toc' or any '<base>.<n>.audiopack' part.
        if (s.ends_with( ".audiopack.toc" ))
        {
            a.m_Base = s.substr( 0, s.size() - 14 );
        }
        else if (s.ends_with( ".audiopack" ))
        {
            std::string b = s.substr( 0, s.size() - 10 );

            if (auto dot = b.rfind( '.' ); dot != std::string::npos)
                b = b.substr( 0, dot );

            a.m_Base = b;
        }
        else
        {
            a.m_Base = s;
        }

        a.m_TocPath = a.m_Base + ".audiopack.toc";

        SMPACK_TRY( in, Load( orig( a.m_TocPath ) ) );
        SMPACK_TRY( t, audiopack::Parse( in.m_Data ) );

        a.m_Toc = std::move( t );

        for (std::uint32_t i = 0; i < a.m_Toc.m_Header.m_NumParts; ++i)
        {
            const fs::path pp = orig( std::format( "{}.{}.audiopack", a.m_Base, i ) );
            std::error_code ec;
            if (!fs::exists( pp, ec ))
            {
                a.m_Parts.emplace_back();
                continue;
            }

            SMPACK_TRY( m, io::MappedFile::Open( pp ) );
            a.m_Parts.push_back( std::move( m ) );
        }

        return a;
    }


    [[nodiscard]] inline Result<ByteSpan> AudioEntry(
        const AudioSource & a,
        const audiopack::StreamEntry & e
    )
    {
        const auto part = a.m_Toc.PartOf( e.m_FileId );

        if (part >= a.m_Parts.size() || a.m_Parts[part].Size() == 0)
        {
            return Fail( Errc::NOT_FOUND, std::format( "part {} of {} is missing", part, a.m_Base ) );
        }

        return ByteReader( a.m_Parts[part].Span() ).Bytes( e.m_FileOffset, e.m_FileSize );
    }


    inline void PrintAudio(
        const AudioSource & a,
        bool list,
        bool as_json = false
    )
    {
        if (list && as_json)
        {
            json::Value arr = json::Value::MakeArray();

            for (const auto & e : a.m_Toc.m_Entries)
            {
                json::Value j = json::Value::MakeObject();

                j["file_id"] = e.m_FileId;
                j["size"] = e.m_FileSize;
                j["part"] = a.m_Toc.PartOf( e.m_FileId );
                j["offset"] = e.m_FileOffset;
                arr.Push( std::move( j ) );
            }

            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "list";
            doc["kind"] = "audiopack";
            doc["entries"] = arr;

            print( "{}", json::Dump( doc ) );
            return;
        }

        if (!list)
        {
            std::uint64_t total = 0;
            for (const auto & e : a.m_Toc.m_Entries)
                total += e.m_FileSize;

            println( "audiopack (APAK v{})", a.m_Toc.m_Header.m_Version );
            println( "  toc        {}", a.m_TocPath.string() );
            println( "  parts      {}", a.m_Toc.m_Header.m_NumParts );
            println( "  streams    {}", a.m_Toc.m_Header.m_NumEntries );
            println( "  data       {}", io::Human( total ) );
            return;
        }

        println( "{:>12}  {:>10}  {:>4}  {:>12}  {}", "file_id", "size", "part", "offset", "kind" );

        for (const auto & e : a.m_Toc.m_Entries)
        {
            std::string kind = "?";

            if (auto b = AudioEntry( a, e ); b && b->size() >= 12 && std::memcmp( b->data(), "RIFF", 4 ) == 0)
                kind = "wem (RIFF)";

            println( "{:>12}  {:>10}  {:>4}  {:#12x}  {}", e.m_FileId, e.m_FileSize, a.m_Toc.PartOf( e.m_FileId ), e.m_FileOffset, kind );
        }
    }


    [[nodiscard]] inline Result<void> ExtractAudio(
        const AudioSource & a,
        const fs::path & out
    )
    {
        std::size_t n = 0;
        for (const auto & e : a.m_Toc.m_Entries)
        {
            SMPACK_TRY( b, AudioEntry( a, e ) );
            SMPACK_TRYV( io::WriteFile( out / std::format( "{}.wem", e.m_FileId ), b ) );
            ++n;
        }
        println( "extracted {} stream(s) to {} (Wwise .wem, named by decimal file id)", n, out.string() );
        return {};
    }


    [[nodiscard]] inline Result<void> CmdAudioReplace(
        const Args & args
    )
    {
        if (args.m_Pos.size() < 2)
            return Fail( Errc::BAD_ARGUMENT, "audio replace <x.audiopack.toc> <ID.wem files/dirs...> [-o DIR] [--add] [--pristine]" );

        SMPACK_TRY( a, OpenAudiopack( args.m_Pos[0], args.Has( "--pristine" ) ) );
        SMPACK_TRY( files, CollectFiles( std::vector<std::string>( args.m_Pos.begin() + 1, args.m_Pos.end() ), ".wem" ) );

        std::map<std::uint32_t, Bytes> repl;
        for (const auto & f : files)
        {
            const std::string st = f.stem().string();
            std::optional<std::uint64_t> id = st.starts_with( "0x" ) ? ParseU64( st ) : std::nullopt;

            if (!id)
            {
                std::uint64_t v = 0;
                auto r = std::from_chars( st.data(), st.data() + st.size(), v, 10 );

                if (r.ec == std::errc {} && r.ptr == st.data() + st.size())
                    id = v;
            }

            if (!id || *id > UINT32_MAX)
                return Fail( Errc::BAD_ARGUMENT, std::format( "'{}': name must be the Wwise file ID (decimal or '0x' hex)", f.string() ) );

            const bool exists = std::any_of( a.m_Toc.m_Entries.begin(), a.m_Toc.m_Entries.end(), [&] ( auto & e )
            {
                return e.m_FileId == *id;
            } );

            if (!exists && !args.Has( "--add" ))
                return Fail( Errc::NOT_FOUND, std::format( "stream {} is not in this pack (use '--add' to append)", *id ) );

            SMPACK_TRY( d, io::ReadFile( f ) );
            repl[static_cast<std::uint32_t>(*id)] = std::move( d );
        }

        auto getter = [&] ( std::uint32_t id ) -> Result<Bytes>
        {
            if (auto it = repl.find( id ); it != repl.end())
                return it->second;

            for (const auto & e : a.m_Toc.m_Entries)
                if (e.m_FileId == id)
                {
                    SMPACK_TRY( b, AudioEntry( a, e ) );
                    return Bytes( b.begin(), b.end() );
                }

            return Fail( Errc::NOT_FOUND, "stream vanished" );
        };

        SMPACK_TRY( built, audiopack::Build( a.m_Toc, repl, getter ) );

        const fs::path out = args.GetOr( "--output", "out" );
        const std::string name = fs::path( a.m_Base ).filename().string();

        SMPACK_TRYV( io::WriteFile( out / (name + ".audiopack.toc"), built.m_Toc ) );

        for (std::size_t i = 0; i < built.m_Parts.size(); ++i)
            SMPACK_TRYV( io::WriteFile( out / std::format( "{}.{}.audiopack", name, i ), built.m_Parts[i] ) );

        println( "wrote {} stream(s) into {} ({} replaced/added)", 
            (built.m_Toc.size() - sizeof( audiopack::TocHeader )) / sizeof( audiopack::StreamEntry ),
            out.string(), repl.size() );

        std::vector<fs::path> outs { out / (name + ".audiopack.toc") };

        for (std::size_t i = 0; i < built.m_Parts.size(); ++i)
            outs.push_back( out / std::format( "{}.{}.audiopack", name, i ) );

        if (auto game = args.Get( "--install" ))
        {
            // Audio packs live in 'exec/sound/pc_le', not 'exec/wad/pc_le'.
            const fs::path dir = fs::path( *game ) / "exec" / "sound" / "pc_le";
            std::error_code ec;

            for (const auto & f : outs)
            {
                const fs::path dst = dir / f.filename();

                if (fs::exists( dst, ec ) && !fs::exists( dst.string() + ".smpack-orig", ec ))
                    fs::copy_file( dst, dst.string() + ".smpack-orig", ec );

                fs::copy_file( f, dst, fs::copy_options::overwrite_existing, ec );

                if (ec)
                    return Fail( Errc::IO_FAILURE, std::format( "cannot copy to '{}': {}", dst.string(), ec.message() ) );

                println( "  install {}", dst.string() );
            }
        }

        return {};
    }

    // 'index', find.

    [[nodiscard]] inline Result<void> CmdIndex(
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "index <exec/wad/pc_le dir> [-o texindex.json]" );

        const fs::path dir = args.m_Pos[0];

        json::Value doc = json::Value::MakeObject();
        doc["smpack"] = "texindex";

        json::Value texs = json::Value::MakeArray();

        std::map<std::uint64_t, std::string> hash_to_pack, id_to_pack;
        std::error_code ec;
        std::vector<fs::path> tocs, wads;
        for (const auto & e : fs::directory_iterator( dir, ec ))
        {
            const auto s = e.path().string();

            if (s.ends_with( ".texpack.toc" ))
                tocs.push_back( e.path() );
            else if (s.ends_with( ".wad" )) 
                wads.push_back( e.path() );
        }

        std::sort( tocs.begin(), tocs.end() );
        std::sort( wads.begin(), wads.end() );

        for (const auto & t : tocs)
        {
            auto in = Load( t );

            if (!in)
                continue;

            auto pkg = texpack::Parse( in->m_Data );

            if (!pkg)
                continue;

            for (const auto & x : pkg->m_Textures)
            {
                hash_to_pack.emplace( x.m_ContentHash, PackBase( t ) );
                id_to_pack.emplace( x.m_TexIdentifier, PackBase( t ) );
            }
        }

        std::set<std::uint64_t> seen;
        std::size_t nw = 0;

        for (const auto & w : wads)
        {
            auto in = Load( w );

            if (!in || in->m_Kind != Kind::WAD)
                continue;

            auto wd = wad::Parse( in->m_Data );

            if (!wd)
                continue;

            ++nw;

            if (nw % 10 == 0)
                println( stderr, "  scanned {}/{} wads...", nw, wads.size() );

            for (const auto & t : wad::FindTextures( *wd, in->m_Data ))
            {
                json::Value j = json::Value::MakeObject();

                j["name"] = t.m_Parm.m_Name;
                j["content_hash"] = json::Hex64( t.m_Parm.m_ContentHash );
                j["tex_identifier"] = json::Hex64( wd->m_Entries[t.m_ParmIndex].Guid64() );
                j["wad"] = w.filename().string();
                j["width"] = t.m_Parm.m_Width;
                j["height"] = t.m_Parm.m_Height;
                const auto * f = agc::FindFormatInfo( t.m_Parm.m_Tsharp.Format() );
                j["format"] = f ? std::string( f->m_Name ) : "?";
                j["mips"] = t.m_Parm.m_MipCountHigh;
                j["streamed"] = t.m_Parm.Streamed();

                if (auto it = hash_to_pack.find( t.m_Parm.m_ContentHash );
                    it != hash_to_pack.end()) j["texpack"] = it->second;
                else if (auto it2 = id_to_pack.find( wd->m_Entries[t.m_ParmIndex].Guid64() );
                    it2 != id_to_pack.end()) j["texpack"] = it2->second;

                texs.Push( j );
            }
        }

        doc["textures"] = texs;
        const fs::path out = args.GetOr( "--output", "texindex.json" );

        SMPACK_TRYV( io::WriteText( out, json::Dump( doc ) ) );

        println( "indexed {} texture reference(s) from {} WAD(s) and {} texpack(s) -> {}", texs.AsArray().size(), nw, tocs.size(), out.string() );
        return {};
    }


    [[nodiscard]] inline Result<void> CmdFind(
        const Args & args
    )
    {
        if (args.m_Pos.size() < 2)
            return Fail( Errc::BAD_ARGUMENT, "find <texindex.json> <name substring|hex hash, ID>" );

        SMPACK_TRY( raw, io::ReadFile( args.m_Pos[0] ) );
        SMPACK_TRY( doc, json::Parse( { reinterpret_cast<const char *>(raw.data()), raw.size() } ) );

        const std::string q = args.m_Pos[1];
        const auto qv = ParseU64( q );
        std::size_t n = 0;

        for (const auto & t : doc.Get( "textures" ).AsArray())
        {
            const bool hit = Icontains( t.Get( "name" ).AsString(), q ) ||
                (qv && (t.Get( "content_hash" ).AsU64() == *qv || t.Get( "tex_identifier" ).AsU64() == *qv));

            if (!hit)
                continue;

            println( "{:<56} {} {}x{} {:<10} wad={} texpack={}", t.Get( "name" ).AsString(), t.Get( "content_hash" ).AsString(),
                t.Get( "width" ).AsInt(), t.Get( "height" ).AsInt(), t.Get( "format" ).AsString(), t.Get( "wad" ).AsString(),
                t.Get( "texpack" ).AsString().empty() ? "-" : t.Get( "texpack" ).AsString() );

            if (++n >= 200)
            {
                println( "... (more results truncated)" );
                break;
            }
        }

        if (!n)
            println( "no matches" );

        return {};
    }

    // Patch 'add', 'remove', 'list'.

    [[nodiscard]] inline Result<void> CmdPatch(
        const std::string & sub,
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "patch add|remove|list <game dir> [--texpack NAME] [--lodpack NAME]" );

        const fs::path bo = fs::path( args.m_Pos[0] ) / "exec" / "boot-options.json";

        SMPACK_TRY( raw, io::ReadFile( bo ) );
        SMPACK_TRY( doc, json::Parse( { reinterpret_cast<const char *>(raw.data()), raw.size() } ) );

        if (sub == "list")
        {
            SMPACK_TRY( e, bootopts::Entry( doc ) );

            if (args.Has( "--json" ))
            {
                json::Value o = json::Value::MakeObject();

                o["patch-texpacks"] = e->Get( "patch-texpacks" ).IsArray() ? e->Get( "patch-texpacks" ) : json::Value::MakeArray();
                o["patch-lodpacks"] = e->Get( "patch-lodpacks" ).IsArray() ? e->Get( "patch-lodpacks" ) : json::Value::MakeArray();

                print( "{}", json::Dump( o ) );
                return {};
            }

            for (std::string_view k : {"patch-texpacks", "patch-lodpacks"})
            {
                print( "{}:", k );

                for (const auto & v : e->Get( k ).AsArray())
                    print( " {}", v.AsString() );

                println( "" );
            }

            return {};
        }

        const bool add = sub == "add";
        bool changed = false;

        for (const auto & n : args.All( "--texpack" ))
        {
            SMPACK_TRY( c, bootopts::SetPatch( doc, "patch-texpacks", n, add ) );
            changed |= c;
        }

        for (const auto & n : args.All( "--lodpack" ))
        {
            SMPACK_TRY( c, bootopts::SetPatch( doc, "patch-lodpacks", n, add ) );
            changed |= c;
        }

        if (!changed)
        {
            println( "boot-options.json unchanged" );
            return {};
        }

        std::error_code ec;
        if (!fs::exists( bo.string() + ".smpack-orig", ec ))
            fs::copy_file( bo, bo.string() + ".smpack-orig", ec );

        SMPACK_TRYV( io::WriteText( bo, json::Dump( doc ) ) );
        println( "updated {}", bo.string() );
        return {};
    }
}