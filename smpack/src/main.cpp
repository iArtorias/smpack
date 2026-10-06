// smpack, God of War Ragnarok AIO modding tool (CLI).

#include "cmd_formats.hpp"
#include "cmd_mesh.hpp"
#include "cmd_mesh_import.hpp"
#include "cmd_tex.hpp"

using namespace smpack;
using namespace smpack::cli;

namespace
{
    constexpr std::string_view VERSION = "1.3.0";
    constexpr std::string_view AUTHOR = "iArtorias [https://github.com/iArtorias]";
    constexpr std::string_view YEAR = "2026";

    void Usage()
    {
        println( stderr, R"(smpack v{} [by {}]
God of War Ragnarok AIO modding tool (CLI).

>> GENERIC (format detected from content. LZ4 frames unwrapped transparently)
  smpack info    <file>
  smpack list    <file>                [--filter TEXT] [--index texindex.json] [--json]
  smpack extract <file> [-o DIR]       [--filter TEXT] [--raw] [--no-dds] [--blocks]

      .wad, .wypdb  - Chunks and 'manifest.json'.
      .texpack      - '<ID or name>.dds' and .json per texture (needs the payload).
      .lodpack      - Groups and blocks with -'-blocks' and 'manifest.json'.
      .as           - One .anmset per animation set and 'manifest.json'.
      .shaderpack   - 'shaders/*.dxbc' and sections, and 'manifest.json'.
      .audiopack    - '<file id>.wem' (Wwise streams)
      Other LZ4     - Decompressed file.

>> TEXTURES
  smpack tex list    <file.texpack|.toc|.wad>   [--index texindex.json] [--json]
  smpack tex export  <file.texpack|.wad> [-o DIR] [--filter TEXT] [--name EXACT] [--id HEX] [--index F] [--raw]
  smpack tex import  <dds files/dirs...> [--patch NAME] [-o DIR] [--wad FILE.wad ...] [--game DIR]
                     [--install GAME_DIR] [--no-compress]

      DDS files must keep the .json sidecar written by export. Texpack textures
      become a patch texpack '<NAME>.texpack/.toc'. WAD textures rewrite the WAD.
      '--wad' also refreshes the WAD resident low mips of streamed textures.

  smpack tex verify  <file.texpack> [--limit N] [--sce-dll GAME_DIR (Windows)]

>> WAD
  smpack wad pack    <extracted dir> [-o out.wad] [--no-compress] [--level N]
  smpack wad replace <in.wad> <#index|name[type]> <file> [-o DIR] [--debug-file F] [--temp-file F]

>> MODELS
  smpack mesh list   <file.wad> [--filter TEXT] [--json]
  smpack mesh export <file.wad> [-o DIR] [--name EXACT | --id N | --filter TEXT] [--as glb|gltf|fbx]
                     [--lod N|all] [--variant NAME|N|all] [--all-parts] [--no-textures] [--world] [--json]

      Metres, Y up, bind pose with skeleton and skin weights. Streamed LODs are read from the
      .lodpack files next to the WAD. A missing one falls back to the next LOD.
      Parts on hidden joints (wounds, damage states) are left out unless '--all-parts' is given.
      Models with visual variants export the default variant unless '--variant' picks one.
      Materials reference 'textures/<name>.png' (base color with the cutout in alpha, normal map).
      Level props are moved to the origin unless '--world' is given.

  smpack mesh import <file.wad> <model.glb|gltf> [--name EXACT | --id N] [--lods all|same] [--game DIR]
                     [--patch NAME] [--append] [-o DIR] [--install GAME_DIR] [--json]

      Replaces the geometry of a model with an edited export. Nodes and materials are matched by
      the names the export wrote ('<model>_part<N>', 'MAT_<hash>'). '--lods all' (default) also
      writes the new mesh into the coarser LODs. Streamed geometry goes into a patch lodpack.

>> GEOMETRY, ANIMATION, SHADERS
  smpack lod patch   --pack <orig.lodpack> <blocks/HASH.bin ...> [--patch NAME] [-o DIR] [--install GAME_DIR]
  smpack anim replace <x.as> <x.wad> --set <name|#i> --blob <f.anmset> [...] [-o DIR] [--no-identity]
  smpack shader replace <x.shaderpack> <#i|name|hash> <file.dxbc> [-o FILE]
  smpack audio replace <x.audiopack.toc> <ID.wem ...> [-o DIR] [--add] [--pristine] [--install GAME_DIR]

      '--pristine' reads '*.smpack-orig' backups so rebuilding never stacks edits.

>> GAME INTEGRATION
  smpack index <exec/wad/pc_le> [-o texindex.json]
  smpack find  <texindex.json> <name|hash>
  smpack patch add|remove|list <GAME_DIR> [--texpack NAME] [--lodpack NAME] [--json]

>> UTILITIES
  smpack lz4 d <in> <out>        smpack lz4 c <in> <out> [--level N]

>> INFO
  smpack --version

      Prints the current program version.

  smpack --author

      Prints the program's author.
)",
VERSION, AUTHOR );
    }

    Result<void> CmdInfoListExtract(
        const std::string & cmd,
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, std::format( "{} requires a file", cmd ) );

        const fs::path p = args.m_Pos[0];

        // Audio pack parts are raw RIFF data, route them through their .toc.
        if (p.string().ends_with( ".audiopack" ) && !p.string().ends_with( ".audiopack.toc" ))
        {
            SMPACK_TRY( a, OpenAudiopack( p ) );

            const fs::path o = args.GetOr( "--output", fs::path( a.m_Base ).filename().string() + "_audio" );

            if (cmd == "extract")
                return ExtractAudio( a, o );

            PrintAudio( a, cmd == "list", args.Has( "--json" ) );

            return {};
        }

        SMPACK_TRY( in, Load( p ) );
        const fs::path out = args.GetOr( "--output", (fs::path( PackBase( p ) + (in.m_Kind == Kind::LZ4_OTHER ? "" : "_x") )).string() );

        switch (in.m_Kind)
        {
            case Kind::WAD:
            {
                SMPACK_TRY( w, wad::Parse( in.m_Data ) );

                if (cmd == "info")
                    PrintWadInfo( in, w );
                else if (cmd == "list")
                    PrintWadList( w, args );
                else
                    return ExtractWad( in, w, out, args );

                return {};
            }

            case Kind::TEXPACK:
            {
                SMPACK_TRY( src, OpenTexpack( p ) );

                if (cmd == "list")
                    return CmdTexList( args );

                if (cmd == "extract")
                    return ExportTexpack( src, out, args );

                const auto & h = src.m_Pkg.m_Header;

                std::uint64_t mem = 0, disk = 0, blocks = 0;
                for (const auto & t : src.m_Pkg.m_Textures)
                {
                    mem += t.MemBytes();
                    disk += t.DiskBytes();
                    blocks += t.m_Mips.size();
                }

                println( "texpack (v{})", h.m_Version );
                println( "  file              {} ({}{})", src.m_In.m_Path.string(), io::Human( src.m_In.m_DiskSize ), src.m_In.m_WasLz4 ? ", LZ4" : "" );
                println( "  payload           {}", src.m_HasPayload ? "present" : "missing (toc only, export needs the .texpack)" );
                println( "  toc size          {:#x}", h.m_TocSizeBytes );
                println( "  textures          {}", h.m_NumTexFiles );
                println( "  stream blocks     {} ({} chained)", h.m_NumStreamBlocks, blocks );
                println( "  texel data        {}", io::Human( mem ) );
                println( "  stored            {}", io::Human( disk ) );

                return {};
            }

            case Kind::LODPACK:
            {
                SMPACK_TRY( s, OpenLodpack( p ) );

                if (cmd == "info")
                    PrintLodInfo( s );
                else
                    if (cmd == "list") PrintLodList( s, args.Has( "--json" ) );
                else
                    return ExtractLodpack( s, out, args );

                return {};
            }

            case Kind::SHADERPACK:
            {
                SMPACK_TRY( sp, shaderpack::Parse( in.m_Data ) );

                if (cmd == "info")
                    PrintShaderInfo( in, sp );
                else if (cmd == "list")
                    PrintShaderList( sp, args );
                else
                    return ExtractShaders( in, sp, out, args );


                return {};
            }

            case Kind::ANIMSET:
            {
                SMPACK_TRY( sets, animset::Parse( in.m_Data ) );

                if (cmd == "info")
                {
                    std::uint64_t used = 0;
                    for (const auto & s : sets)
                        used += s.m_Size;

                    println( "anim file set container (.as)" );
                    println( "  file       {} ({})", in.m_Path.string(), io::Human( in.m_DiskSize ) );
                    println( "  sets       {}", sets.size() );
                    println( "  set bytes  {}", io::Human( used ) );
                }
                else if (cmd == "list")
                {
                    PrintAsList( sets, args );
                }
                else
                {
                    return ExtractAs( in, sets, out, args );
                }

                return {};
            }

            case Kind::AUDIOPACK:
            {
                SMPACK_TRY( a, OpenAudiopack( p ) );

                if (cmd == "extract")
                    return ExtractAudio( a, out );

                PrintAudio( a, cmd == "list", args.Has( "--json" ) );

                return {};
            }

            case Kind::DDS:
            {
                SMPACK_TRY( img, dds::Read( in.m_Data ) );
                println( "dds {}x{}{} mips {} slices {} dxgi {}{}", img.m_Width, img.m_Height, img.m_Depth > 1 ? std::format( "x{}", img.m_Depth ) : "",
                    img.m_Mips, img.m_Slices, img.m_Dxgi, img.m_Cube ? " cube" : img.m_Depth > 1 ? " volume" : "" );

                return {};
            }

            case Kind::LZ4_OTHER:
            {
                if (cmd == "extract")
                {
                    fs::path o = args.Get( "--output" ) ? fs::path( *args.Get( "--output" ) ) : fs::path( p.filename().string() + ".dec" );
                    SMPACK_TRYV( io::WriteFile( o, in.m_Data ) );

                    println( "decompressed {} -> {} ({})", p.string(), o.string(), io::Human( in.m_Data.size() ) );
                }
                else
                {
                    println( "LZ4 frame: {} -> {} (content not recognised)", io::Human( in.m_DiskSize ), io::Human( in.m_Data.size() ) );
                }
                
                return {};
            }

            default:
                return Fail( Errc::UNSUPPORTED, std::format( "'{}': unrecognised format", p.string() ) );
        }
    }


    Result<void> CmdLz4(
        const Args & args
    )
    {
        if (args.m_Pos.size() < 3)
            return Fail( Errc::BAD_ARGUMENT, "lz4 d|c <in> <out>" );

        SMPACK_TRY( d, io::ReadFile( args.m_Pos[1] ) );

        if (args.m_Pos[0] == "d")
        {
            SMPACK_TRY( o, lz4::DecompressFrame( d ) );
            SMPACK_TRYV( io::WriteFile( args.m_Pos[2], o ) );

            println( "{} -> {}", io::Human( d.size() ), io::Human( o.size() ) );
        }
        else
        {
            SMPACK_TRY( o, lz4::CompressFrame( d, args.GetInt( "--level", 9 ) ) );
            SMPACK_TRYV( io::WriteFile( args.m_Pos[2], o ) );

            println( "{} -> {}", io::Human( d.size() ), io::Human( o.size() ) );
        }

        return {};
    }


    Result<void> Run( 
        int argc,
        char ** argv
    )
    {
        if (argc < 2)
        {
            Usage();
            return Fail( Errc::BAD_ARGUMENT, "no command given" );
        }

        const std::string cmd = argv[1];

        if (cmd == "-h" || cmd == "--help" || cmd == "help")
        {
            Usage();
            return {};
        }

        if (cmd == "--version" || cmd == "version")
        {
            println( "smpack {}", VERSION );
            return {};
        }

        if (cmd == "--author" || cmd == "author")
        {
            println( "Created by {} in {}", AUTHOR, YEAR );
            return {};
        }

        const bool grouped = cmd == "tex" || cmd == "wad" || cmd == "lod" || cmd == "anim" || cmd == "shader" || cmd == "patch" ||
            cmd == "audio" || cmd == "mesh";

        const std::string sub = grouped && argc > 2 ? argv[2] : "";
        SMPACK_TRY( args, ParseArgs( argc, argv, grouped ? 3 : 2 ) );

        if (args.Has( "-h" ) || args.Has( "--help" ))
        {
            Usage();
            return {};
        }

        if (cmd == "info" || cmd == "list" || cmd == "extract")
            return CmdInfoListExtract( cmd, args );

        if (cmd == "lz4")
            return CmdLz4( args );

        if (cmd == "index")
            return CmdIndex( args );

        if (cmd == "find")
            return CmdFind( args );

        if (cmd == "tex")
        {
            if (sub == "list")
                return CmdTexList( args );

            if (sub == "export")
            {
                if (args.m_Pos.empty())
                    return Fail( Errc::BAD_ARGUMENT, "tex export <file>" );

                SMPACK_TRY( in, Load( args.m_Pos[0] ) );

                const fs::path out = args.GetOr( "--output", PackBase( args.m_Pos[0] ) + "_tex" );

                if (in.m_Kind == Kind::WAD)
                {
                    SMPACK_TRY( w, wad::Parse( in.m_Data ) );
                    SMPACK_TRY( n, ExportWadTextures( in, w, out, args ) );

                    println( "exported {} texture(s) to {}", n, out.string() );
                    return {};
                }

                SMPACK_TRY( src, OpenTexpack( args.m_Pos[0] ) );
                return ExportTexpack( src, out, args );
            }

            if (sub == "import")
                return CmdTexImport( args );

            if (sub == "verify")
                return CmdTexVerify( args );
        }

        if (cmd == "wad")
        {
            if (sub == "pack")
                return CmdWadPack( args );

            if (sub == "replace")
                return CmdWadReplace( args );
        }

        if (cmd == "mesh" && sub == "list")
            return CmdMeshList( args );

        if (cmd == "mesh" && sub == "export")
            return CmdMeshExport( args );

        if (cmd == "mesh" && sub == "import")
            return CmdMeshImport( args );

        if (cmd == "lod" && sub == "patch")
            return CmdLodPatch( args );

        if (cmd == "anim" && sub == "replace")
            return CmdAnimReplace( args );

        if (cmd == "shader" && sub == "replace")
            return CmdShaderReplace( args );

        if (cmd == "audio" && sub == "replace")
            return CmdAudioReplace( args );

        if (cmd == "patch" && (sub == "add" || sub == "remove" || sub == "list"))
            return CmdPatch( sub, args );

        Usage();

        return Fail( Errc::BAD_ARGUMENT, std::format( "unknown command '{}{}{}'", cmd, sub.empty() ? "" : " ", sub ) );
    }
}


int main( 
    int argc,
    char ** argv
)
{
    auto r = Run( argc, argv );

    if (!r)
    {
        println( stderr, "error: {}", r.error().m_Message );
        return r.error().m_Code == Errc::BAD_ARGUMENT ? 2 : 1;
    }

    return 0;
}