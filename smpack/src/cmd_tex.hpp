// Texture commands. 'list', 'export' (DDS), 'import' (patch texpack and WAD), 'verify'.

#pragma once

#include "cli.hpp"
#include "smpack/agc.hpp"
#include "smpack/agc_dll.hpp"
#include "smpack/bootopts.hpp"
#include "smpack/dds.hpp"

namespace smpack::cli
{
    // Name index [texture names only exist in WADs, texpacks carry IDs and hashes.

    struct NameIndex
    {
        std::map<std::uint64_t, std::string> m_ByHash;
        std::map<std::uint64_t, std::string> m_ById;

        [[nodiscard]] std::optional<std::string> Lookup(
            std::uint64_t hash,
            std::uint64_t id
        ) const
        {
            if (auto it = m_ByHash.find( hash );
                it != m_ByHash.end())
                return it->second;

            if (auto it = m_ById.find( id ); it != m_ById.end())
                return it->second;

            return std::nullopt;
        }
    };

    [[nodiscard]] inline Result<NameIndex> LoadIndex(
        const std::optional<std::string> & path
    )
    {
        NameIndex ni;

        if (!path)
            return ni;

        SMPACK_TRY( raw, io::ReadFile( *path ) );
        SMPACK_TRY( doc, json::Parse( { reinterpret_cast<const char *>(raw.data()), raw.size() } ) );

        for (const auto & t : doc.Get( "textures" ).AsArray())
        {
            const std::string & n = t.Get( "name" ).AsString();

            if (auto h = t.Get( "content_hash" ).AsU64())
                ni.m_ByHash.emplace( h, n );

            if (auto i = t.Get( "tex_identifier" ).AsU64())
                ni.m_ById.emplace( i, n );
        }

        return ni;
    }

    // Sidecar JSON.

    [[nodiscard]] inline json::Value TsharpJson( const agc::TSharp & ts )
    {
        json::Value j = json::Value::MakeObject();

        const auto * f = agc::FindFormatInfo( ts.Format() );

        j["agc_format"] = ts.Format();
        j["format"] = f ? std::string( f->m_Name ) : std::format( "unknown({})", ts.Format() );
        j["dxgi"] = f ? f->m_Dxgi : 0u;
        j["width"] = ts.Width();
        j["height"] = ts.Height();
        j["mips"] = ts.NumMips();
        j["slices"] = ts.NumSlices();

        if (ts.Is3d())
            j["depth"] = ts.Depth();

        j["type"] = agc::TexTypeName( ts.Type() );
        j["tile_mode"] = ts.TileMode();
        j["swizzle"] = agc::SwizzleStr( ts.Swizzle() );
        return j;
    }

    // Game folder holding 'libSceAgcGpuAddress.dll', found from the input file.
    inline fs::path g_sce_dll_dir;

    inline void RememberGameDir( const fs::path & input )
    {
        if (auto root = FindGameRoot( input.parent_path() ); root && fs::exists( *root / "libSceAgcGpuAddress.dll" ))
            g_sce_dll_dir = *root;
    }


    // Export one tiled surface as DDS and sidecar. Returns the DDS path.
    [[nodiscard]] inline Result<fs::path> WriteTextureDds(
        const fs::path & base,
        const agc::TSharp & ts,
        ByteSpan surface,
        bool force_linear,
        json::Value sidecar,
        [[maybe_unused]] const Args & args
    )
    {
        const auto * f = agc::FindFormatInfo( ts.Format() );

        if (!f || !f->m_Dxgi)
        {
            return Fail( Errc::UNSUPPORTED, std::format( "format {} has no DDS equivalent (use '--raw')", f ? f->m_Name : "?" ) );
        }

        Bytes pixels;

        #if defined(_WIN32)
        if (auto dll = args.Get( "--sce-dll" ))
        {
            SMPACK_TRY( sce, agc::SceDll::Load( *dll ) );
            SMPACK_TRY( px, sce.Detile( ts, surface, force_linear ) );

            pixels = std::move( px );
        }
        #endif

        if (pixels.empty())
        {
            auto l = agc::LayoutFor( ts, force_linear );

            #if defined(_WIN32)
            // Surfaces the native code does not model. Fall back to Sony's
            // detiler from the game folder automatically.
            if (!l && !g_sce_dll_dir.empty())
            {
                SMPACK_TRY( sce, agc::SceDll::Load( g_sce_dll_dir ) );
                SMPACK_TRY( px, sce.Detile( ts, surface, force_linear ) );

                pixels = std::move( px );
            }
            #endif

            if (pixels.empty())
            {
                if (!l)
                    return Fail( l.error().m_Code, l.error().m_Message );

                SMPACK_TRY( px, agc::Detile( *l, surface ) );
                pixels = std::move( px );
            }
        }

        dds::Image img;
        img.m_Dxgi = f->m_Dxgi;
        img.m_Width = ts.Width();
        img.m_Height = ts.Height();
        img.m_Mips = ts.NumMips();
        img.m_Slices = ts.Is3d() ? 1 : ts.NumSlices();
        img.m_Depth = ts.Depth();
        img.m_Cube = ts.IsCube();
        img.m_Data = std::move( pixels );

        const fs::path dds_path = fs::path( base.string() + ".dds" );

        SMPACK_TRYV( io::WriteFile( dds_path, dds::Write( img ) ) );
        sidecar["texture"] = TsharpJson( ts );

        SMPACK_TRYV( io::WriteText( fs::path( base.string() + ".json" ), json::Dump( sidecar ) ) );
        return dds_path;
    }

    // Texpack export.

    struct TexpackSource
    {
        Input m_In; // Payload [or toc when no payload available].
        bool m_HasPayload = false;
        std::string m_PackName;
        texpack::Package m_Pkg;
    };

    // Open a texpack given either half of the pair.
    [[nodiscard]] inline Result<TexpackSource> OpenTexpack(
        const fs::path & p
    )
    {
        TexpackSource s;
        s.m_PackName = PackBase( p );

        fs::path payload = p;
        if (p.string().ends_with( ".toc" ))
            payload = fs::path( p.string().substr( 0, p.string().size() - 4 ) );

        std::error_code ec;
        if (fs::exists( payload, ec ) && payload != p)
        {
            SMPACK_TRY( in, Load( payload ) );
            s.m_In = std::move( in );
        }
        else
        {
            SMPACK_TRY( in, Load( p ) );
            s.m_In = std::move( in );
        }

        if (s.m_In.m_Kind != Kind::TEXPACK)
            return Fail( Errc::BAD_MAGIC, std::format( "'{}' is not a texpack", s.m_In.m_Path.string() ) );

        SMPACK_TRY( pkg, texpack::Parse( s.m_In.m_Data ) );

        s.m_Pkg = std::move( pkg );
        s.m_HasPayload = s.m_In.m_Data.size() > s.m_Pkg.m_Header.m_TocSizeBytes;
        return s;
    }


    [[nodiscard]] inline json::Value TexpackSidecar(
        const TexpackSource & src,
        const texpack::Texture & t,
        const texpack::AssembledTexture & at,
        const std::string & name
    )
    {
        json::Value j = json::Value::MakeObject();

        j["smpack"] = "texture";
        j["source"] = "texpack";
        j["pack"] = src.m_PackName;

        if (!name.empty())
            j["name"] = name;

        j["tex_identifier"] = json::Hex64( t.m_TexIdentifier );
        j["content_hash"] = json::Hex64( t.m_ContentHash );

        json::Value part = json::Value::MakeArray();

        for (const auto & b : t.m_Mips)
        {
            json::Value p = json::Value::MakeArray();

            p.Push( b.m_Info.m_SmallestMipIndex );
            p.Push( b.m_Info.m_LargestMipIndex );

            part.Push( p );
        }

        j["partition"] = part;
        j["gnf"] = ToHex( ByteSpan( at.m_Gnf ) );
        return j;
    }


    [[nodiscard]] inline Result<void> ExportTexpack(
        TexpackSource & src,
        const fs::path & out_dir,
        const Args & args
    )
    {
        RememberGameDir( src.m_In.m_Path );

        if (!src.m_HasPayload)
        {
            return Fail( Errc::NOT_FOUND, std::format( "no payload for '{}': the .texpack must sit next to the .toc", src.m_PackName ) );
        }

        SMPACK_TRY( names, LoadIndex( args.Get( "--index" ) ) );

        const auto filter = args.Get( "--filter" );
        const auto want_id = args.Get( "--id" ).and_then( ParseU64 );
        const auto exact = args.Get( "--name" );
        const bool raw = args.Has( "--raw" );
        std::size_t n = 0, failed = 0;

        for (const auto & t : src.m_Pkg.m_Textures)
        {
            const auto name = names.Lookup( t.m_ContentHash, t.m_TexIdentifier ).value_or( "" );

            if (want_id && *want_id != t.m_TexIdentifier && *want_id != t.m_ContentHash)
                continue;

            if (filter && !Icontains( name, *filter ) && !Icontains( std::format( "{:016x}", t.m_TexIdentifier ), *filter ))
                continue;

            if (exact && name != *exact)
                continue;

            const std::string stem = name.empty() ? std::format( "{:016x}", t.m_TexIdentifier )
                : std::format( "{}", io::SafeName( name ) );
            const fs::path base = out_dir / stem;

            if (raw)
            {
                for (std::size_t i = 0; i < t.m_Mips.size(); ++i)
                {
                    SMPACK_TRY( bytes, texpack::BlockBytes( src.m_In.m_Data, t.m_Mips[i] ) );
                    SMPACK_TRYV( io::WriteFile( fs::path( std::format( "{}.block{}.bin", base.string(), i ) ), bytes ) );
                }

                ++n;
                continue;
            }

            auto at = texpack::Assemble( src.m_In.m_Data, t );

            if (!at)
            {
                println( stderr, "  ! {:016x}: {}", t.m_TexIdentifier, at.error().m_Message );
                ++failed;
                continue;
            }

            auto r = WriteTextureDds( base, at->m_Tsharp, at->m_Surface, false, TexpackSidecar( src, t, *at, name ), args );

            if (!r)
            {
                println( stderr, "  ! {:016x}: {}", t.m_TexIdentifier, r.error().m_Message );
                ++failed;
                continue;
            }

            ++n;

            if (args.Has( "--verbose" ))
                println( "  {}", r->filename().string() );
        }

        println( "exported {} texture(s) to {}{}", n, out_dir.string(), failed ? std::format( " ({} failed)", failed ) : "" );
        return {};
    }

    // WAD texture export.

    [[nodiscard]] inline json::Value WadSidecar(
        const fs::path & wad_path,
        const wad::Wad & w,
        const wad::WadTexture & t
    )
    {
        json::Value j = json::Value::MakeObject();

        j["smpack"] = "texture";
        j["source"] = "wad";
        j["wad"] = GameName( wad_path );
        j["name"] = t.m_Parm.m_Name;
        j["parm_index"] = t.m_ParmIndex;

        if (t.m_GpuIndex)
            j["gpu_index"] = *t.m_GpuIndex;

        j["tex_identifier"] = json::Hex64( w.m_Entries[t.m_ParmIndex].Guid64() );
        j["content_hash"] = json::Hex64( t.m_Parm.m_ContentHash );
        j["streamed"] = t.m_Parm.Streamed();
        j["flags"] = std::format( "{:#06x}", t.m_Parm.m_Flags );
        j["mip_count_low"] = t.m_Parm.m_MipCountLow;
        j["mip_count_high"] = t.m_Parm.m_MipCountHigh;
        j["full_width"] = t.m_Parm.m_Width;
        j["full_height"] = t.m_Parm.m_Height;
        return j;
    }


    [[nodiscard]] inline Result<std::size_t> ExportWadTextures(
        const Input & in,
        const wad::Wad & w,
        const fs::path & out_dir,
        const Args & args
    )
    {
        const auto filter = args.Get( "--filter" );
        const auto exact = args.Get( "--name" );

        RememberGameDir( in.m_Path );
        std::size_t n = 0;

        for (const auto & t : wad::FindTextures( w, in.m_Data ))
        {
            if (filter && !Icontains( t.m_Parm.m_Name, *filter ))
                continue;

            if (exact && t.m_Parm.m_Name != *exact)
                continue;

            if (!t.m_GpuIndex)
                continue;

            const auto & g = w.m_Entries[*t.m_GpuIndex];
            const ByteSpan surf = wad::ChunkData( in.m_Data, g );

            auto r = WriteTextureDds( out_dir / io::SafeName( t.m_Parm.m_Name ), t.m_Parm.m_Tsharp, surf, /*force_linear*/ true,
                WadSidecar( in.m_Path, w, t ), args );

            if (!r)
            {
                println( stderr, "  ! {}: {}", t.m_Parm.m_Name, r.error().m_Message );
                continue;
            }

            ++n;
        }

        return n;
    }

    // Tex list.

    [[nodiscard]] inline Result<void> CmdTexList(
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "tex list <file.texpack|.toc|.wad>" );

        const fs::path p = args.m_Pos[0];

        SMPACK_TRY( names, LoadIndex( args.Get( "--index" ) ) );
        {
            SMPACK_TRY( probe, Load( p ) );

            if (probe.m_Kind == Kind::WAD)
            {
                SMPACK_TRY( w, wad::Parse( probe.m_Data ) );

                if (args.Has( "--json" ))
                {
                    json::Value arr = json::Value::MakeArray();

                    for (const auto & t : wad::FindTextures( w, probe.m_Data ))
                    {
                        const auto * f = agc::FindFormatInfo( t.m_Parm.m_Tsharp.Format() );

                        json::Value j = json::Value::MakeObject();

                        j["name"] = t.m_Parm.m_Name;
                        j["parm_index"] = t.m_ParmIndex;
                        j["content_hash"] = json::Hex64( t.m_Parm.m_ContentHash );
                        j["tex_identifier"] = json::Hex64( w.m_Entries[t.m_ParmIndex].Guid64() );
                        j["width"] = t.m_Parm.m_Width;
                        j["height"] = t.m_Parm.m_Height;
                        j["mips"] = t.m_Parm.m_MipCountHigh;
                        j["wad_width"] = t.m_Parm.m_Tsharp.Width();
                        j["wad_height"] = t.m_Parm.m_Tsharp.Height();
                        j["wad_mips"] = t.m_Parm.m_Tsharp.NumMips();
                        j["format"] = f ? std::string( f->m_Name ) : "?";
                        j["streamed"] = t.m_Parm.Streamed();
                        j["has_data"] = t.m_GpuIndex.has_value();

                        arr.Push( std::move( j ) );
                    }

                    json::Value doc = json::Value::MakeObject();

                    doc["smpack"] = "texlist";
                    doc["source"] = "wad";
                    doc["file"] = p.filename().string();
                    doc["textures"] = arr;

                    print( "{}", json::Dump( doc ) );
                    return {};
                }

                println( "{:<56} {:>18} {:>10} {:>5} {:>12} {:<12} {}", "name", "content_hash", "full", "mips", "wad mips", "format", "streamed" );

                for (const auto & t : wad::FindTextures( w, probe.m_Data ))
                {
                    const auto * f = agc::FindFormatInfo( t.m_Parm.m_Tsharp.Format() );

                    println( "{:<56} {:#018x} {:>5}x{:<4} {:>5} {:>5}x{:<4}{:>2} {:<12} {}", t.m_Parm.m_Name, t.m_Parm.m_ContentHash,
                        t.m_Parm.m_Width, t.m_Parm.m_Height, t.m_Parm.m_MipCountHigh, t.m_Parm.m_Tsharp.Width(), t.m_Parm.m_Tsharp.Height(),
                        t.m_Parm.m_Tsharp.NumMips(), f ? f->m_Name : "?",
                        t.m_Parm.Streamed() ? "yes" : "no" );
                }

                return {};
            }
        }

        SMPACK_TRY( src, OpenTexpack( p ) );

        auto top_format = [&] ( const texpack::Texture & t ) -> std::string
        {
            if (!src.m_HasPayload)
                return "-";

            if (auto bb = texpack::BlockBytes( src.m_In.m_Data, t.Top() ))
            {
                if (auto bc = texpack::ParseBlock( *bb ); bc && bc->m_Gnf)
                {
                    const auto ts = agc::TSharp::From( ByteSpan( *bc->m_Gnf ).subspan( 16, 32 ) );

                    if (const auto * f = agc::FindFormatInfo( ts.Format() ))
                        return std::string( f->m_Name );
                }
            }

            return "-";
        };

        if (args.Has( "--json" ))
        {
            json::Value arr = json::Value::MakeArray();
            std::size_t idx = 0;

            for (const auto & t : src.m_Pkg.m_Textures)
            {
                json::Value j = json::Value::MakeObject();

                j["index"] = idx++;
                j["tex_identifier"] = json::Hex64( t.m_TexIdentifier );
                j["content_hash"] = json::Hex64( t.m_ContentHash );
                j["width"] = t.Width();
                j["height"] = t.Height();
                j["mips"] = t.MipCount();
                j["format"] = top_format( t );
                j["bytes"] = t.MemBytes();
                j["name"] = names.Lookup( t.m_ContentHash, t.m_TexIdentifier ).value_or( "" );

                arr.Push( std::move( j ) );
            }

            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "texlist";
            doc["source"] = "texpack";
            doc["pack"] = src.m_PackName;
            doc["has_payload"] = src.m_HasPayload;
            doc["textures"] = arr;

            print( "{}", json::Dump( doc ) );
            return {};
        }

        println( "{:>5}  {:>18}  {:>18}  {:>11}  {:>4}  {:<12}  {:>10}  {}", "idx", "tex_identifier", "content_hash", "size", "mips",
            "format", "bytes", "name" );

        std::size_t i = 0;
        for (const auto & t : src.m_Pkg.m_Textures)
        {
            const std::string fmt = top_format( t );

            println( "{:>5}  {:#018x}  {:#018x}  {:>5}x{:<5}  {:>4}  {:<12}  {:>10}  {}", i++, t.m_TexIdentifier, t.m_ContentHash,
                t.Width(), t.Height(), t.MipCount(), fmt, io::Human( t.MemBytes() ),
                names.Lookup( t.m_ContentHash, t.m_TexIdentifier ).value_or( "" ) );
        }

        return {};
    }

    // Tex import.

    struct ImportItem
    {
        fs::path m_DdsPath;
        json::Value m_Side;
        dds::Image m_Img;
    };

    [[nodiscard]] inline Result<std::vector<fs::path>> CollectFiles(
        const std::vector<std::string> & inputs,
        std::string_view ext
    )
    {
        std::vector<fs::path> out;

        for (const auto & s : inputs)
        {
            fs::path p = s;
            std::error_code ec;
            if (fs::is_directory( p, ec ))
            {
                for (const auto & e : fs::recursive_directory_iterator( p, ec ))
                {
                    if (e.is_regular_file() && Icontains( e.path().extension().string(), ext ) &&
                        e.path().extension().string().size() == ext.size())
                        out.push_back( e.path() );
                }
            }
            else if (fs::exists( p, ec ))
            {
                out.push_back( p );
            }
            else
            {
                return Fail( Errc::NOT_FOUND, std::format( "'{}' does not exist", s ) );
            }
        }

        std::sort( out.begin(), out.end() );
        return out;
    }


    // Prepare pixel data of 'img' for the AGC format [channel order fixups].
    [[nodiscard]] inline Result<void> CheckFormat(
        ImportItem & it,
        std::uint32_t agc_fmt
    )
    {
        const auto * f = agc::FindFormatInfo( agc_fmt );

        if (!f)
            return Fail( Errc::UNSUPPORTED, "unknown target format" );

        if (it.m_Img.m_Dxgi == 87 /*B8G8R8A8*/ && f->m_Dxgi == agc::dxgi::R8G8B8A8_UNORM)
        {
            dds::SwapRb( it.m_Img.m_Data );
            it.m_Img.m_Dxgi = agc::dxgi::R8G8B8A8_UNORM;
        }

        if (!agc::DxgiCompatible( agc_fmt, it.m_Img.m_Dxgi ))
        {
            return Fail( Errc::BAD_ARGUMENT,
                std::format( "{}: DDS is DXGI format {}, the game texture is {} (DXGI {}). Save as that format "
                "(e.g. 'texconv -f {} -m 0')",
                it.m_DdsPath.filename().string(), it.m_Img.m_Dxgi, f->m_Name, f->m_Dxgi, f->m_Name ) );
        }

        return {};
    }


    [[nodiscard]] inline std::uint32_t FullChain(
        std::uint32_t w,
        std::uint32_t h
    )
    {
        return static_cast<std::uint32_t>(std::bit_width( std::max( w, h ) ));
    }


    // Build a tiled surface for a DDS given the template descriptor.
    [[nodiscard]] inline Result<std::pair<agc::TSharp, Bytes>> TileDds(
        const dds::Image & img,
        agc::TSharp ts,
        bool force_linear
    )
    {
        ts.SetSize( img.m_Width, img.m_Height );
        ts.SetLastMip( img.m_Mips - 1 );

        SMPACK_TRY( L, agc::LayoutFor( ts, force_linear ) );

        if (L.m_Slices != img.m_Slices * img.m_Depth)
        {
            return Fail( Errc::BAD_ARGUMENT, std::format( "DDS has {} slice(s) x depth {}, the game texture has {} layer(s)", img.m_Slices,
                img.m_Depth, L.m_Slices ) );
        }

        SMPACK_TRY( surf, agc::Tile( L, img.m_Data ) );
        return std::pair { ts, std::move( surf ) };
    }


    // Replace low mips of a WAD texture from a DDS that carries [at least] the
    // mips the WAD stores. Returns 'false' if the DDS chain does not contain them.
    [[nodiscard]] inline Result<bool> UpdateWadTexture(
        Bytes & wad_data,
        wad::Wad & w,
        std::vector<wad::ChunkPayload> & payloads,
        const wad::WadTexture & t,
        const dds::Image & img,
        bool whole_texture
    )
    {
        const agc::TSharp low = t.m_Parm.m_Tsharp;

        dds::Image sub;
        sub.m_Dxgi = img.m_Dxgi;
        sub.m_Slices = img.m_Slices;
        sub.m_Cube = img.m_Cube;
        std::uint32_t first = 0;

        if (whole_texture)
        {
            sub = img;
        }
        else
        {
            // Find the DDS mip matching the WAD descriptor's top size.
            bool found = false;
            for (std::uint32_t m = 0; m < img.m_Mips; ++m)
            {
                if (std::max( 1u, img.m_Width >> m ) == low.Width() && std::max( 1u, img.m_Height >> m ) == low.Height())
                {
                    first = m;
                    found = true;
                    break;
                }
            }

            if (!found || img.m_Mips - first < low.NumMips())
                return false;

            sub.m_Width = low.Width();
            sub.m_Height = low.Height();
            sub.m_Mips = low.NumMips();
            std::uint32_t blk = 1;

            const auto bits = dds::BitsPerElement( img.m_Dxgi, &blk );

            // Copy the mips [first, first+mips) of every slice.
            std::uint64_t slice_size = dds::TightSize( img.m_Dxgi, img.m_Width, img.m_Height, img.m_Mips, 1 );

            for (std::uint32_t s = 0; s < img.m_Slices; ++s)
            {
                std::uint64_t off = s * slice_size;

                for (std::uint32_t m = 0; m < img.m_Mips; ++m)
                {
                    const std::uint64_t ew = (std::max( 1u, img.m_Width >> m ) + blk - 1) / blk;
                    const std::uint64_t eh = (std::max( 1u, img.m_Height >> m ) + blk - 1) / blk;
                    const std::uint64_t sz = ew * eh * bits / 8;

                    if (m >= first && m < first + sub.m_Mips)
                        sub.m_Data.insert( sub.m_Data.end(), img.m_Data.begin() + static_cast<std::ptrdiff_t>( off ),
                            img.m_Data.begin() + static_cast<std::ptrdiff_t>( off + sz ) );

                    off += sz;
                }
            }
        }

        SMPACK_TRY( tiled, TileDds( sub, low, /*force_linear*/ true ) );

        auto [ts, surf] = std::move( tiled );

        if (!t.m_GpuIndex)
            return Fail( Errc::NOT_FOUND, std::format( "{}: WAD has no GPU chunk for this texture", t.m_Parm.m_Name ) );

        payloads[*t.m_GpuIndex].m_Data = std::move( surf );

        // Patch the parm. T#, GNF stream size, and full size fields.
        auto & parm = payloads[t.m_ParmIndex].m_Data;
        const std::size_t g = wad::TEX_PARM_GNF_OFFSET;
        std::memcpy( parm.data() + g + 16, ts.m_W, 32 );

        const std::uint64_t lin_total = payloads[*t.m_GpuIndex].m_Data.size();
        const std::uint32_t ss = static_cast<std::uint32_t>(lin_total + 4096); // Same rule as shipped files.
        std::memcpy( parm.data() + g + 12, &ss, 4 );

        if (whole_texture)
        {
            const std::uint16_t w16 = static_cast<std::uint16_t>(img.m_Width), h16 = static_cast<std::uint16_t>(img.m_Height);

            std::memcpy( parm.data() + 0x48, &w16, 2 );
            std::memcpy( parm.data() + 0x4A, &h16, 2 );

            const std::uint8_t mh = static_cast<std::uint8_t>(img.m_Mips);
            std::memcpy( parm.data() + 0x47, &mh, 1 );
        }

        (void)wad_data;
        (void)w;
        return true;
    }


    // Also update the WAD's 'full size' bookkeeping for a streamed texture
    // whose high mips come from a patch texpack with new dimensions.
    inline void UpdateParmHigh(
        Bytes & parm,
        std::uint32_t w,
        std::uint32_t h,
        std::uint32_t mips,
        std::uint64_t tiled_total
    )
    {
        const std::uint16_t w16 = static_cast<std::uint16_t>(w), h16 = static_cast<std::uint16_t>(h);

        std::memcpy( parm.data() + 0x48, &w16, 2 );
        std::memcpy( parm.data() + 0x4A, &h16, 2 );

        const std::uint8_t mh = static_cast<std::uint8_t>(mips);

        std::memcpy( parm.data() + 0x47, &mh, 1 );

        std::uint32_t contents = 0;
        std::memcpy( &contents, parm.data() + wad::TEX_PARM_GNF_OFFSET + 4, 4 );

        const std::uint32_t unc = static_cast<std::uint32_t>(tiled_total + contents + 8);
        std::memcpy( parm.data() + 0x5C, &unc, 4 );
    }

    struct WadEdit
    {
        fs::path m_Path;
        Input m_In;
        wad::Wad m_W;
        std::vector<wad::ChunkPayload> m_Payloads;
        std::vector<wad::WadTexture> m_Textures;
        std::size_t m_Changes = 0;
    };

    [[nodiscard]] inline Result<WadEdit> OpenWadEdit(
        const fs::path & p
    )
    {
        WadEdit e;
        e.m_Path = p;

        SMPACK_TRY( in, Load( p ) );

        if (in.m_Kind != Kind::WAD)
            return Fail( Errc::BAD_MAGIC, std::format( "'{}' is not a WAD", p.string() ) );

        e.m_In = std::move( in );

        SMPACK_TRY( w, wad::Parse( e.m_In.m_Data ) );

        e.m_W = std::move( w );
        e.m_Payloads.resize( e.m_W.m_Entries.size() );

        for (std::size_t i = 0; i < e.m_W.m_Entries.size(); ++i)
        {
            const auto & en = e.m_W.m_Entries[i];

            auto d = wad::ChunkData( e.m_In.m_Data, en );
            auto dbg = wad::DebugData( e.m_In.m_Data, en );
            auto tmp = wad::TempData( e.m_In.m_Data, en );

            e.m_Payloads[i].m_Data.assign( d.begin(), d.end() );
            e.m_Payloads[i].m_Debug.assign( dbg.begin(), dbg.end() );
            e.m_Payloads[i].m_Temp.assign( tmp.begin(), tmp.end() );
        }

        e.m_Textures = wad::FindTextures( e.m_W, e.m_In.m_Data );
        return e;
    }


    [[nodiscard]] inline Result<fs::path> SaveWadEdit(
        WadEdit & e,
        const fs::path & out_dir,
        const Args & args
    )
    {
        SMPACK_TRY( bytes, wad::Build( e.m_W.m_Header, e.m_W.m_Entries, e.m_Payloads ) );

        const fs::path out = out_dir / GameName( e.m_Path );
        SMPACK_TRYV( WriteMaybeLz4( out, bytes, !args.Has( "--no-compress" ), args.GetInt( "--level", 0 ) ) );
        return out;
    }


    [[nodiscard]] inline Result<void> InstallFiles(
        const fs::path & game,
        const std::vector<fs::path> & files,
        const std::optional<std::string> & texpatch,
        const std::optional<std::string> & lodpatch
    )
    {
        const fs::path wad_dir = game / "exec" / "wad" / "pc_le";

        std::error_code ec;
        if (!fs::exists( wad_dir, ec ))
            return Fail( Errc::NOT_FOUND, std::format( "'{}' not found", wad_dir.string() ) );

        for (const auto & f : files)
        {
            const fs::path dst = wad_dir / f.filename();

            if (fs::exists( dst, ec ) && !fs::exists( dst.string() + ".smpack-orig", ec ))
            {
                fs::copy_file( dst, dst.string() + ".smpack-orig", ec );

                if (ec)
                    return Fail( Errc::IO_FAILURE, std::format( "cannot back up '{}': {}", dst.string(), ec.message() ) );

                println( "  backup  {}", (dst.string() + ".smpack-orig") );
            }
            fs::copy_file( f, dst, fs::copy_options::overwrite_existing, ec );

            if (ec)
                return Fail( Errc::IO_FAILURE, std::format( "cannot copy to '{}': {}", dst.string(), ec.message() ) );

            println( "  install {}", dst.string() );
        }

        if (texpatch || lodpatch)
        {
            const fs::path bo = game / "exec" / "boot-options.json";

            SMPACK_TRY( raw, io::ReadFile( bo ) );
            SMPACK_TRY( doc, json::Parse( { reinterpret_cast<const char *>(raw.data()), raw.size() } ) );

            bool changed = false;

            if (texpatch)
            {
                SMPACK_TRY( c, bootopts::SetPatch( doc, "patch-texpacks", *texpatch, true ) );
                changed |= c;
            }

            if (lodpatch)
            {
                SMPACK_TRY( c, bootopts::SetPatch( doc, "patch-lodpacks", *lodpatch, true ) );
                changed |= c;
            }

            if (changed)
            {
                if (!fs::exists( bo.string() + ".smpack-orig", ec ))
                    fs::copy_file( bo, bo.string() + ".smpack-orig", ec );

                SMPACK_TRYV( io::WriteText( bo, json::Dump( doc ) ) );
                println( "  registered in {}", bo.string() );
            }
        }

        return {};
    }


    [[nodiscard]] inline Result<void> CmdTexImport(
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "tex import <dds files/dirs...> [--patch NAME] [-o DIR]" );

        const fs::path out_dir = args.GetOr( "--output", "." );
        const std::string patch = args.GetOr( "--patch", "smpack_patch" );

        if (patch.size() > bootopts::MAX_NAME_LEN)
            return Fail( Errc::BAD_ARGUMENT, "'--patch' name must be at most 31 characters" );

        SMPACK_TRY( files, CollectFiles( args.m_Pos, ".dds" ) );

        if (files.empty())
            return Fail( Errc::NOT_FOUND, "no .dds files given" );

        std::vector<texpack::NewTexture> newtex;
        std::map<std::string, WadEdit> wads; // By file name.
        std::vector<fs::path> extra_wads;

        for (const auto & w : args.All( "--wad" ))
            extra_wads.push_back( w );

        auto get_wad = [&] ( const std::string & fname, const fs::path & hint_dir ) -> Result<WadEdit *>
        {
            if (auto it = wads.find( fname ); it != wads.end())
                return &it->second;

            std::vector<fs::path> cands = { hint_dir / fname };
            for (const auto & w : extra_wads)
                if (GameName( w ) == fname)
                    cands.insert( cands.begin(), w );

            if (auto g = args.Get( "--game" ))
            {
                // Prefer the untouched original so rebuilding never stacks edits.
                const fs::path installed = fs::path( *g ) / "exec" / "wad" / "pc_le" / fname;

                cands.push_back( installed.string() + std::string( BACKUP_SUFFIX ) );
                cands.push_back( installed );
            }

            std::error_code ec;
            for (const auto & c : cands)
            {
                if (fs::exists( c, ec ))
                {
                    SMPACK_TRY( e, OpenWadEdit( c ) );
                    return &wads.emplace( fname, std::move( e ) ).first->second;
                }
            }

            return Fail( Errc::NOT_FOUND, std::format( "WAD '{}' not found (pass '--wad <path>' or '--game <dir>')", fname ) );
        };

        for (const auto & ddsp : files)
        {
            ImportItem it;
            it.m_DdsPath = ddsp;
            fs::path side = ddsp;
            side.replace_extension( ".json" );

            SMPACK_TRY( sraw, io::ReadFile( side ) );
            SMPACK_TRY( sdoc, json::Parse( { reinterpret_cast<const char *>(sraw.data()), sraw.size() } ) );

            it.m_Side = std::move( sdoc );

            SMPACK_TRY( draw, io::ReadFile( ddsp ) );
            SMPACK_TRY( img, dds::Read( draw ) );

            it.m_Img = std::move( img );

            const auto & tj = it.m_Side.Get( "texture" );
            const std::uint32_t agc_fmt = static_cast<std::uint32_t>(tj.Get( "agc_format" ).AsInt());

            SMPACK_TRYV( CheckFormat( it, agc_fmt ) );

            const std::string source = it.m_Side.Get( "source" ).AsString();
            const std::uint64_t hash = it.m_Side.Get( "content_hash" ).AsU64();
            const std::uint64_t id = it.m_Side.Get( "tex_identifier" ).AsU64();
            const std::string label = it.m_Side.Get( "name" ).AsString().empty() ? std::format( "{:016x}", id ) : it.m_Side.Get( "name" ).AsString();

            if (source == "texpack")
            {
                if (it.m_Img.m_Mips != FullChain( it.m_Img.m_Width, it.m_Img.m_Height ))
                {
                    return Fail( Errc::BAD_ARGUMENT,
                        std::format( "{}: DDS has {} mip(s). Streamed textures need the full chain ({}). "
                        "Regenerate with e.g. 'texconv -m 0'",
                        ddsp.filename().string(), it.m_Img.m_Mips, FullChain( it.m_Img.m_Width, it.m_Img.m_Height ) ) );
                }

                texpack::NewTexture nt;
                nt.m_TexIdentifier = id;
                nt.m_ContentHash = hash;

                const Bytes g = FromHex( it.m_Side.Get( "gnf" ).AsString() );

                if (g.size() != 256)
                    return Fail( Errc::BAD_ARGUMENT, std::format( "{}: sidecar has no GNF header", side.string() ) );

                std::memcpy( nt.m_Gnf.data(), g.data(), 256 );

                const auto tmpl = agc::TSharp::From( ByteSpan( nt.m_Gnf ).subspan( 16, 32 ) );

                SMPACK_TRY( tiled, TileDds( it.m_Img, tmpl, false ) );

                auto [ts, surf] = std::move( tiled );
                texpack::UpdateGnf( nt.m_Gnf, ts, surf.size() );
                nt.m_Surface = std::move( surf );

                const bool same_size = it.m_Img.m_Width == tmpl.Width() && it.m_Img.m_Height == tmpl.Height();

                if (same_size)
                {
                    for (const auto & p : it.m_Side.Get( "partition" ).AsArray())
                        nt.m_Partition.emplace_back( static_cast<std::uint8_t>(p.AsArray()[0].AsInt()), 
                            static_cast<std::uint8_t>(p.AsArray()[1].AsInt()) );
                }

                println( "  texpack  {:<48} {}x{} ({} mips){}", label, it.m_Img.m_Width, it.m_Img.m_Height, it.m_Img.m_Mips,
                    same_size ? "" : std::format( "  [resized from {}x{}]", tmpl.Width(), tmpl.Height() ) );

                // Optional. Keep the WAD resident low mips in sync.
                for (const auto & wp : extra_wads)
                {
                    SMPACK_TRY( we, get_wad( GameName( wp ), wp.parent_path() ) );

                    for (const auto & t : we->m_Textures)
                    {
                        if (t.m_Parm.m_ContentHash != hash)
                            continue;

                        SMPACK_TRY( ok, UpdateWadTexture( we->m_In.m_Owned, we->m_W, we->m_Payloads, t, it.m_Img, false ) );

                        if (ok)
                        {
                            UpdateParmHigh( we->m_Payloads[t.m_ParmIndex].m_Data, it.m_Img.m_Width, it.m_Img.m_Height, it.m_Img.m_Mips,
                                nt.m_Surface.size() );

                            ++we->m_Changes;
                            println( "           and low mips in {}", we->m_Path.filename().string() );
                        }
                        else
                        {
                            println( "           ! {}: DDS chain has no {}x{} level for the WAD low mips", we->m_Path.filename().string(),
                                t.m_Parm.m_Tsharp.Width(), t.m_Parm.m_Tsharp.Height() );
                        }
                    }
                }

                newtex.push_back( std::move( nt ) );
            }
            else if (source == "wad")
            {
                SMPACK_TRY( we, get_wad( it.m_Side.Get( "wad" ).AsString(), ddsp.parent_path().parent_path() ) );

                const std::size_t pi = static_cast<std::size_t>(it.m_Side.Get( "parm_index" ).AsInt());
                const wad::WadTexture * wt = nullptr;

                for (const auto & t : we->m_Textures)
                    if (t.m_ParmIndex == pi) wt = &t;

                if (!wt)
                    return Fail( Errc::NOT_FOUND, std::format( "{}: texture parm #{} not found in {}", label, pi, we->m_Path.string() ) );

                const bool streamed = it.m_Side.Get( "streamed" ).AsBool();
                const bool same = it.m_Img.m_Width == wt->m_Parm.m_Tsharp.Width() && it.m_Img.m_Height == wt->m_Parm.m_Tsharp.Height() &&
                    it.m_Img.m_Mips == wt->m_Parm.m_Tsharp.NumMips();

                if (streamed && !same)
                {
                    return Fail( Errc::BAD_ARGUMENT,
                        std::format( "{}: this is the WAD low-mip copy ({}x{}, {} mips) of a streamed texture. Edit the "
                        "high-res DDS exported from the texpack instead, or keep the same size",
                        label, wt->m_Parm.m_Tsharp.Width(), wt->m_Parm.m_Tsharp.Height(), wt->m_Parm.m_Tsharp.NumMips() ) );
                }

                SMPACK_TRY( ok, UpdateWadTexture( we->m_In.m_Owned, we->m_W, we->m_Payloads, *wt, it.m_Img, !streamed ) );

                if (!ok)
                    return Fail( Errc::INCONSISTENT, std::format( "{}: could not map DDS mips onto the WAD texture", label ) );

                ++we->m_Changes;
                println( "  wad      {:<48} {}x{} ({} mips) -> {}", label, it.m_Img.m_Width, it.m_Img.m_Height, it.m_Img.m_Mips, we->m_Path.filename().string() );
            }
            else
            {
                return Fail( Errc::BAD_ARGUMENT, std::format( "{}: unknown sidecar source '{}'", side.string(), source ) );
            }
        }

        std::vector<fs::path> outputs;
        if (!newtex.empty())
        {
            SMPACK_TRY( built, texpack::Build( newtex ) );

            const fs::path payload = out_dir / (patch + ".texpack");
            const fs::path toc = out_dir / (patch + ".texpack.toc");

            SMPACK_TRYV( io::WriteFile( payload, built.m_Payload ) );
            SMPACK_TRYV( WriteMaybeLz4( toc, built.m_Toc, !args.Has( "--no-compress" ), 9 ) );

            println( "wrote {} ({}) and {}", payload.string(), io::Human( built.m_Payload.size() ), toc.filename().string() );

            outputs.push_back( payload );
            outputs.push_back( toc );
        }

        for (auto & [fname, we] : wads)
        {
            if (!we.m_Changes)
                continue;

            SMPACK_TRY( outp, SaveWadEdit( we, out_dir, args ) );

            println( "wrote {} ({} texture(s) changed)", outp.string(), we.m_Changes );
            outputs.push_back( outp );
        }

        if (auto game = args.Get( "--install" ))
        {
            SMPACK_TRYV( InstallFiles( *game, outputs, newtex.empty() ? std::nullopt : std::optional<std::string>( patch ), std::nullopt ) );
        }
        else if (!newtex.empty())
        {
            println( "\nTo activate, copy '{0}.texpack' and '{0}.texpack.toc' into 'exec/wad/pc_le' and add \"{0}\" to\n"
                "\"patch-texpacks\" in 'exec/boot-options.json' (or run again with '--install <game dir>').",
                patch );
        }

        return {};
    }

    // Tex verify.

    [[nodiscard]] inline Result<void> CmdTexVerify(
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "tex verify <file.texpack> [--sce-dll <game dir>] [--limit N]" );

        SMPACK_TRY( src, OpenTexpack( args.m_Pos[0] ) );

        if (!src.m_HasPayload)
            return Fail( Errc::NOT_FOUND, "verify needs the .texpack payload" );

        const int limit = args.GetInt( "--limit", 0 );

        #if defined(_WIN32)
        std::optional<agc::SceDll> sce;
        if (auto d = args.Get( "--sce-dll" ))
        {
            SMPACK_TRY( s, agc::SceDll::Load( *d ) );
            sce = std::move( s );
        }
        #endif

        std::size_t n = 0, bad = 0, dll_checked = 0;
        for (const auto & t : src.m_Pkg.m_Textures)
        {
            if (limit && static_cast<int>(n) >= limit)
                break;
            ++n;

            auto at = texpack::Assemble( src.m_In.m_Data, t );

            if (!at)
            {
                println( "  ! {:016x}: {}", t.m_TexIdentifier, at.error().m_Message );
                ++bad;
                continue;
            }

            SMPACK_TRY( lin, agc::Detile( at->m_Layout, at->m_Surface ) );
            SMPACK_TRY( re, agc::Tile( at->m_Layout, lin ) );

            // 'tile' must be the exact inverse of 'detile' on every element the
            // texture uses [bytes of the tail block no mip covers are ignored].
            SMPACK_TRY( lin2, agc::Detile( at->m_Layout, re ) );

            if (lin2 != lin)
            {
                println( "  ! {:016x}: native tile/detile round trip mismatch", t.m_TexIdentifier );
                ++bad;
            }

            #if defined(_WIN32)
            if (sce)
            {
                SMPACK_TRY( ref, sce->Detile( at->m_Tsharp, at->m_Surface ) );
                ++dll_checked;

                if (ref != lin)
                {
                    println( "  ! {:016x}: native detile differs from 'libSceAgcGpuAddress'", t.m_TexIdentifier );
                    ++bad;
                }
            }
            #endif
        }

        println( "verified {} texture(s): {} problem(s){}", n, bad,
            dll_checked ? std::format( ", {} cross checked against Sony's detiler", dll_checked ) : "" );

        if (bad)
            return Fail( Errc::INCONSISTENT, "verification failed" );

        return {};
    }
}