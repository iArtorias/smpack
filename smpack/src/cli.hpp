// CLI. Argument parsing, input loading and format detection.

#pragma once

#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "smpack/animset.hpp"
#include "smpack/audiopack.hpp"
#include "smpack/formats.hpp"
#include "smpack/io.hpp"
#include "smpack/json.hpp"
#include "smpack/lodpack.hpp"
#include "smpack/lz4frame.hpp"
#include "smpack/print_compat.hpp"
#include "smpack/reader.hpp"
#include "smpack/shaderpack.hpp"
#include "smpack/texpack.hpp"
#include "smpack/wad.hpp"

namespace smpack::cli
{
    namespace fs = std::filesystem;

    using smpack::out::print;
    using smpack::out::println;

    // Arguments

    struct Args
    {
        std::vector<std::string> m_Pos; // Positionals after the command words.
        std::map<std::string, std::vector<std::string>> m_Opt;  // --key value
        std::set<std::string> m_Flags; // --flag

        [[nodiscard]] bool Has(
            std::string_view f
        ) const
        {
            return m_Flags.count( std::string( f ) ) != 0;
        }


        [[nodiscard]] std::optional<std::string> Get(
            std::string_view k
        ) const
        {
            auto it = m_Opt.find( std::string( k ) );

            if (it == m_Opt.end() || it->second.empty())
                return std::nullopt;

            return it->second.back();
        }


        [[nodiscard]] std::vector<std::string> All(
            std::string_view k
        ) const
        {
            auto it = m_Opt.find( std::string( k ) );
            return it == m_Opt.end() ? std::vector<std::string>{} : it->second;
        }


        [[nodiscard]] std::string GetOr(
            std::string_view k,
            std::string def
        ) const
        {
            return Get( k ).value_or( std::move( def ) );
        }


        [[nodiscard]] int GetInt(
            std::string_view k,
            int def
        ) const
        {
            auto v = Get( k );
            return v ? std::atoi( v->c_str() ) : def;
        }
    };

    // Options that take a value. Everything else starting with '--' is a flag.
    inline const std::set<std::string> VALUE_OPTS = 
    {
        "-o", "--output", "--toc", "--payload", "--filter", "--id", "--name", "--patch", "--pack", "--wad",
        "--wad-out", "--install", "--level", "--sce-dll", "--index", "--set", "--blob", "--debug-file",
        "--temp-file", "--texpack", "--lodpack", "--game", "--limit", "--as", "--lod", "--lods", "--variant",
    };


    [[nodiscard]] inline Result<Args> ParseArgs(
        int argc,
        char ** argv,
        int first
    )
    {
        Args a;

        for (int i = first; i < argc; ++i)
        {
            std::string s = argv[i];

            if (s == "-o")
                s = "--output";

            if (s.size() > 1 && s[0] == '-' && !(s.size() > 1 && std::isdigit( static_cast<unsigned char>(s[1]) )))
            {
                std::string key = s, val;

                if (auto eq = s.find( '=' ); eq != std::string::npos && s.starts_with( "--" ))
                {
                    key = s.substr( 0, eq );
                    val = s.substr( eq + 1 );

                    a.m_Opt[key].push_back( val );
                    continue;
                }
                if (VALUE_OPTS.count( key ))
                {
                    if (i + 1 >= argc)
                        return Fail( Errc::BAD_ARGUMENT, std::format( "{} requires a value", key ) );

                    a.m_Opt[key].push_back( argv[++i] );
                }
                else
                {
                    a.m_Flags.insert( key );
                }
            }
            else
            {
                a.m_Pos.push_back( s );
            }
        }

        return a;
    }

    // Input loading and detection.

    enum class Kind
    {
        UNKNOWN,
        WAD,
        TEXPACK,
        LODPACK,
        SHADERPACK,
        ANIMSET,
        AUDIOPACK,
        DDS,
        LZ4_OTHER
    };

    [[nodiscard]] inline std::string_view KindName(
        Kind k
    )
    {
        switch (k)
        {
            case Kind::WAD:
                return "wad (WTOC)";

            case Kind::TEXPACK:
                return "texpack";

            case Kind::LODPACK:
                return "lodpack";

            case Kind::SHADERPACK:
                return "shaderpack";

            case Kind::ANIMSET:
                return "anim file set (.as)";

            case Kind::AUDIOPACK:
                return "audiopack (Wwise streams)";

            case Kind::DDS:
                return "dds";

            case Kind::LZ4_OTHER:
                return "lz4 frame (unknown content)";

            default:
                return "unknown";
        }
    }

    struct Input
    {
        fs::path m_Path;
        bool m_WasLz4 = false;
        std::uint64_t m_DiskSize = 0;
        Bytes m_Owned;
        io::MappedFile m_Map;
        ByteSpan m_Data;
        Kind m_Kind = Kind::UNKNOWN;
    };

    [[nodiscard]] inline Kind Detect(
        ByteSpan d,
        const fs::path & p
    )
    {
        ByteReader r( d );

        if (wad::LooksLikeWad( d ))
            return Kind::WAD;

        if (auto m = r.At<std::uint64_t>( 0 ); m && *m == shaderpack::MAGIC)
            return Kind::SHADERPACK;

        if (auto m = r.At<std::uint32_t>( 0 ); m && *m == 0x20534444)
            return Kind::DDS;

        if (audiopack::LooksLikeToc( d ))
            return Kind::AUDIOPACK;

        if (auto h = r.At<texpack::Header>( 0 );
            h && h->m_Version == texpack::EXPECTED_VERSION && h->m_TocSizeBytes >= sizeof( texpack::Header ) &&
            h->m_TocSizeBytes <= d.size() && h->m_StreamBlocksOff <= h->m_TocSizeBytes)
        {
            return Kind::TEXPACK;
        }

        if (animset::LooksLikeAs( d ))
            return Kind::ANIMSET;

        if (lodpack::LooksLikeToc( d ))
            return Kind::LODPACK;

        const auto ext = p.extension().string();

        if (ext == ".lodpack" || p.string().ends_with( ".lodpack.toc" ))
            return Kind::LODPACK;

        return Kind::UNKNOWN;
    }


    // Load a file, unwrapping LZ4. Large raw files are memory mapped.
    [[nodiscard]] inline Result<Input> Load(
        const fs::path & p
    )
    {
        Input in;
        in.m_Path = p;

        std::error_code ec;
        in.m_DiskSize = fs::file_size( p, ec );
        if (ec) 
            return Fail( Errc::IO_FAILURE, std::format( "cannot open '{}': {}", p.string(), ec.message() ) );

        SMPACK_TRY( m, io::MappedFile::Open( p ) );

        if (lz4::LooksLikeFrame( m.Span() ))
        {
            in.m_WasLz4 = true;
            SMPACK_TRY( dec, lz4::DecompressFrame( m.Span() ) );
            in.m_Owned = std::move( dec );
            in.m_Data = in.m_Owned;
        }
        else
        {
            in.m_Map = std::move( m );
            in.m_Data = in.m_Map.Span();
        }

        in.m_Kind = Detect( in.m_Data, p );

        if (in.m_Kind == Kind::UNKNOWN && in.m_WasLz4)
            in.m_Kind = Kind::LZ4_OTHER;

        return in;
    }


    // Write bytes, LZ4 framing them unless disabled.
    [[nodiscard]] inline Result<void> WriteMaybeLz4(
        const fs::path & p,
        ByteSpan data,
        bool compress,
        int level
    )
    {
        if (!compress)
            return io::WriteFile( p, data );

        SMPACK_TRY( c, lz4::CompressFrame( data, level ) );
        return io::WriteFile( p, c );
    }

    // Suffix of the backups install writes.
    inline constexpr std::string_view BACKUP_SUFFIX = ".smpack-orig";

    // File name as the game knows it. A backup copy is named after its original.
    [[nodiscard]] inline std::string GameName( const fs::path & p )
    {
        std::string n = p.filename().string();

        if (n.size() > BACKUP_SUFFIX.size() && n.ends_with( BACKUP_SUFFIX ))
            n.resize( n.size() - BACKUP_SUFFIX.size() );

        return n;
    }


    // 'foo.texpack.toc' becomes 'foo', 'foo.lodpack' becomes 'foo', etc.
    [[nodiscard]] inline std::string PackBase(
        const fs::path & p
    )
    {
        std::string n = GameName( p );

        for (std::string_view suf : {".audiopack.toc", ".texpack.toc", ".lodpack.toc", ".texpack", ".lodpack", ".shaderpack", ".wad.bak", ".wad",
            ".wypdb", ".as", "_texpack.toc", "_texpack"})
        {
            if (n.size() > suf.size() && n.ends_with( suf ))
                return n.substr( 0, n.size() - suf.size() );
        }

        return p.stem().string();
    }


    [[nodiscard]] inline bool Icontains( 
        std::string_view hay,
        std::string_view needle
    )
    {
        if (needle.empty())
            return true;

        auto lower = [] ( char c )
        {
            return static_cast<char>(std::tolower( static_cast<unsigned char>(c) ));
        };

        for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i)
        {
            std::size_t k = 0;
            while (k < needle.size() && lower( hay[i + k] ) == lower( needle[k] ))
                ++k;

            if (k == needle.size())
                return true;
        }

        return false;
    }


    [[nodiscard]] inline std::optional<std::uint64_t> ParseU64(
        std::string_view s
    )
    {
        int base = 10;

        if (s.starts_with( "0x" ) || s.starts_with( "0X" ))
        {
            s.remove_prefix( 2 );
            base = 16;
        }
        else if (s.size() == 16)
        {
            base = 16;
        }

        std::uint64_t v = 0;
        auto r = std::from_chars( s.data(), s.data() + s.size(), v, base );

        if (r.ec != std::errc {} || r.ptr != s.data() + s.size())
            return std::nullopt;

        return v;
    }


    [[nodiscard]] inline Bytes FromHex(
        std::string_view s
    )
    {
        Bytes out;

        for (std::size_t i = 0; i + 1 < s.size(); i += 2)
        {
            unsigned v = 0;
            std::from_chars( s.data() + i, s.data() + i + 2, v, 16 );
            out.push_back( static_cast<std::byte>(v) );
        }

        return out;
    }


    // Game root heuristic. A directory containing 'exec/wad/pc_le'.
    [[nodiscard]] inline std::optional<fs::path> FindGameRoot( 
        fs::path p
    )
    {
        std::error_code ec;
        p = fs::absolute( p, ec );

        for (int i = 0; i < 8 && !p.empty(); ++i)
        {
            if (fs::exists( p / "exec" / "wad" / "pc_le", ec ) || fs::exists( p / "exec" / "boot-options.json", ec ))
                return p;

            if (p == p.parent_path())
                break;

            p = p.parent_path();
        }

        return std::nullopt;
    }
} 