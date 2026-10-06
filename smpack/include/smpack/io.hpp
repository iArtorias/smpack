// File helpers. Whole file load and store, memory mapping for multi GB payloads, and a streaming writer.

#pragma once

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "smpack/reader.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace smpack::io
{
    namespace fs = std::filesystem;

    [[nodiscard]] inline Result<Bytes> ReadFile( const fs::path & p )
    {
        std::error_code ec;
        const auto sz = fs::file_size( p, ec );

        if (ec)
        {
            return Fail( Errc::IO_FAILURE, std::format( "cannot stat '{}': {}", p.string(), ec.message() ) );
        }

        std::ifstream f( p, std::ios::binary );

        if (!f)
            return Fail( Errc::IO_FAILURE, std::format( "cannot open '{}'", p.string() ) );

        Bytes buf( static_cast<std::size_t>(sz) );
        if (sz != 0 && !f.read( reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(sz) ))
        {
            return Fail( Errc::IO_FAILURE, std::format( "short read on '{}'", p.string() ) );
        }

        return buf;
    }


    [[nodiscard]] inline Result<void> EnsureParent( const fs::path & p )
    {
        std::error_code ec;

        if (p.has_parent_path())
        {
            fs::create_directories( p.parent_path(), ec );

            if (ec)
            {
                return Fail( Errc::IO_FAILURE, std::format( "cannot create '{}': {}",
                    p.parent_path().string(), ec.message() ) );
            }
        }

        return {};
    }


    [[nodiscard]] inline Result<void> WriteFile( const fs::path & p, ByteSpan data )
    {
        SMPACK_TRYV( EnsureParent( p ) );

        std::ofstream f( p, std::ios::binary | std::ios::trunc );

        if (!f)
            return Fail( Errc::IO_FAILURE, std::format( "cannot write '{}'", p.string() ) );

        if (!data.empty() &&
            !f.write( reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()) ))
        {
            return Fail( Errc::IO_FAILURE, std::format( "write failed on '{}'", p.string() ) );
        }

        return {};
    }


    [[nodiscard]] inline Result<void> WriteText( const fs::path & p, std::string_view s )
    {
        return WriteFile( p, AsBytes( s ) );
    }

    // Read only memory mapping. Falls back to a heap copy for empty files.
    class MappedFile
    {
    public:

        MappedFile() = default;
        MappedFile( const MappedFile & ) = delete;
        MappedFile & operator=( const MappedFile & ) = delete;
        MappedFile( MappedFile && o ) noexcept
        {
            *this = std::move( o );
        }

        MappedFile & operator=( MappedFile && o ) noexcept
        {
            if (this != &o)
            {
                Close();

                m_Data = o.m_Data;
                m_Size = o.m_Size;
                #if defined(_WIN32)
                m_File = o.m_File;
                m_Map = o.m_Map;
                o.m_File = INVALID_HANDLE_VALUE;
                o.m_Map = nullptr;
                #endif
                o.m_Data = nullptr;
                o.m_Size = 0;
            }
            return *this;
        }

        ~MappedFile()
        {
            Close();
        }

        [[nodiscard]] static Result<MappedFile> Open(
            const fs::path & p
        )
        {
            MappedFile m;

            #if defined(_WIN32)
            m.m_File = CreateFileW( p.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );

            if (m.m_File == INVALID_HANDLE_VALUE)
            {
                return Fail( Errc::IO_FAILURE, std::format( "cannot open '{}'", p.string() ) );
            }

            LARGE_INTEGER li {};
            GetFileSizeEx( m.m_File, &li );

            m.m_Size = static_cast<std::uint64_t>(li.QuadPart);

            if (m.m_Size == 0)
                return m;

            m.m_Map = CreateFileMappingW( m.m_File, nullptr, PAGE_READONLY, 0, 0, nullptr );

            if (!m.m_Map)
                return Fail( Errc::IO_FAILURE, std::format( "cannot map '{}'", p.string() ) );

            m.m_Data = static_cast<const std::byte *>(MapViewOfFile( m.m_Map, FILE_MAP_READ, 0, 0, 0 ));

            if (!m.m_Data)
                return Fail( Errc::IO_FAILURE, std::format( "cannot map view of '{}'", p.string() ) );
            #else
            const int fd = ::open( p.c_str(), O_RDONLY );

            if (fd < 0)
                return Fail( Errc::IO_FAILURE, std::format( "cannot open '{}'", p.string() ) );

            struct stat st {};
            if (fstat( fd, &st ) != 0)
            {
                ::close( fd );
                return Fail( Errc::IO_FAILURE, std::format( "cannot stat '{}'", p.string() ) );
            }

            m.m_Size = static_cast<std::uint64_t>( st.st_size );

            if (m.m_Size != 0)
            {
                void * v = mmap( nullptr, m.m_Size, PROT_READ, MAP_PRIVATE, fd, 0 );

                if (v == MAP_FAILED)
                {
                    ::close( fd );
                    return Fail( Errc::IO_FAILURE, std::format( "cannot mmap '{}'", p.string() ) );
                }

                m.m_Data = static_cast<const std::byte *>(v);
            }

            ::close( fd );
            #endif

            return m;
        }


        [[nodiscard]] ByteSpan Span() const noexcept
        {
            return { m_Data, static_cast<std::size_t>(m_Size) };
        }


        [[nodiscard]] std::uint64_t Size() const noexcept
        {
            return m_Size;
        }


        void Close() noexcept
        {
            #if defined(_WIN32)
            if (m_Data)
                UnmapViewOfFile( m_Data );

            if (m_Map)
                CloseHandle( m_Map );

            if (m_File != INVALID_HANDLE_VALUE)
                CloseHandle( m_File );

            m_Map = nullptr;
            m_File = INVALID_HANDLE_VALUE;
            #else
            if (m_Data)
                munmap( const_cast<std::byte *>(m_Data), m_Size );
            #endif

            m_Data = nullptr;
            m_Size = 0;
        }

    private:

        const std::byte * m_Data = nullptr;
        std::uint64_t m_Size = 0;

        #if defined(_WIN32)
        HANDLE m_File = INVALID_HANDLE_VALUE;
        HANDLE m_Map = nullptr;
        #endif
    };


    // Streaming binary writer that tracks its position. Used for multi GB
    // payload output where assembling in memory is not an option.
    class OutFile
    {
    public:
        [[nodiscard]] static Result<OutFile> Create( const fs::path & p )
        {
            SMPACK_TRYV( EnsureParent( p ) );

            OutFile o;
            o.m_Path = p;
            o.m_F = std::make_unique<std::ofstream>( p, std::ios::binary | std::ios::trunc );

            if (!*o.m_F)
                return Fail( Errc::IO_FAILURE, std::format( "cannot write '{}'", p.string() ) );

            return o;
        }


        Result<void> Write(
            ByteSpan s
        )
        {
            if (!s.empty() && !m_F->write( reinterpret_cast<const char *>(s.data()),
                static_cast<std::streamsize>(s.size()) ))
            {
                return Fail( Errc::IO_FAILURE, std::format( "write failed on '{}'", m_Path.string() ) );
            }

            m_Pos += s.size();
            return {};
        }


        Result<void> Zeros(
            std::uint64_t n
        )
        {
            static const std::byte zero_block[4096] {};

            while (n)
            {
                const auto k = std::min<std::uint64_t>( n, sizeof( zero_block ) );
                SMPACK_TRYV( Write( { zero_block, static_cast<std::size_t>(k) } ) );
                n -= k;
            }

            return {};
        }


        Result<void> Align(
            std::uint64_t a
        )
        {
            if (a > 1 && m_Pos % a)
                return Zeros( a - m_Pos % a );

            return {};
        }


        Result<void> Pwrite(
            std::uint64_t off,
            ByteSpan s
        )
        {
            const auto cur = m_F->tellp();

            m_F->seekp( static_cast<std::streamoff>(off) );

            if (!m_F->write( reinterpret_cast<const char *>(s.data()), static_cast<std::streamsize>(s.size()) ))
            {
                return Fail( Errc::IO_FAILURE, std::format( "write failed on '{}'", m_Path.string() ) );
            }

            m_F->seekp( cur );
            return {};
        }


        [[nodiscard]] std::uint64_t Pos() const noexcept
        {
            return m_Pos;
        }


        Result<void> Close()
        {
            if (m_F)
            {
                m_F->close();

                if (!*m_F)
                    return Fail( Errc::IO_FAILURE, std::format( "close failed on '{}'", m_Path.string() ) );

                m_F.reset();
            }
            return {};
        }

    private:

        fs::path m_Path;
        std::unique_ptr<std::ofstream> m_F;
        std::uint64_t m_Pos = 0;
    };

    /// Human readable byte count.
    [[nodiscard]] inline std::string Human(
        std::uint64_t n
    )
    {
        constexpr const char * units[] = { "B", "KB", "MB", "GB", "TB" };
        double v = static_cast<double>(n);
        int u = 0;

        while (v >= 1024.0 && u < 4)
        {
            v /= 1024.0;
            ++u;
        }

        return u == 0 ? std::format( "{} {}", n, units[0] ) : std::format( "{:.1f} {}", v, units[u] );
    }


    // Make a string safe for use as a file name component.
    [[nodiscard]] inline std::string SafeName(
        std::string_view s
    )
    {
        std::string o;
        o.reserve( s.size() );

        for (char c : s)
        {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                c == '_' || c == '-' || c == '.' || c == '#' || c == '+';
            o.push_back( ok ? c : '_' );
        }

        if (o.empty())
            o = "_";

        return o;
    }
}