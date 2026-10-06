// Bounds checked reading and writing over in-memory blobs.
//
// Every offset in these formats comes from the file itself and is therefore
// untrusted. All access goes through 'ByteReader' so a malformed or hostile
// file yields a clean error instead of a read past the end of the buffer.

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace smpack
{

    enum class Errc
    {
        IO_FAILURE,
        TRUNCATED,
        BAD_MAGIC,
        BAD_VERSION,
        BAD_OFFSET,
        INCONSISTENT,
        UNSUPPORTED,
        NOT_FOUND,
        BAD_ARGUMENT,
    };

    struct Error
    {
        Errc m_Code {};
        std::string m_Message;
    };

    template <class T>
    using Result = std::expected<T, Error>;

    [[nodiscard]] inline std::unexpected<Error> Fail(
        Errc c,
        std::string msg
    )
    {
        return std::unexpected( Error { c, std::move( msg ) } );
    }

    // Propagate an error from a' Result' expression.
    #define SMPACK_TRY(var, expr)                                 \
    auto var##_res_ = (expr);                                     \
    if (!var##_res_)                                              \
       return std::unexpected(var##_res_.error());                \
    auto& var = *var##_res_

    #define SMPACK_TRYV(expr)                                     \
    do                                                            \
    {                                                             \
        auto r_ = (expr);                                         \
        if (!r_)                                                  \
           return std::unexpected(r_.error());                    \
    } while (0)
    
    // Trivially copyable POD that we are willing to memcpy out of a file.
    template <class T>
    concept RawPod = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

    using Bytes = std::vector<std::byte>;
    using ByteSpan = std::span<const std::byte>;

    class ByteReader
    {
    public:

        ByteReader() = default;
        explicit ByteReader( ByteSpan data ) noexcept : m_Data( data )
        {}

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_Data.size();
        }


        [[nodiscard]] ByteSpan Span() const noexcept
        {
            return m_Data;
        }


        // Read a POD at an absolute offset.
        template <RawPod T>
        [[nodiscard]] Result<T> At(
            std::uint64_t off
        ) const
        {
            if (off > m_Data.size() || m_Data.size() - off < sizeof( T ))
            {
                return Fail( Errc::TRUNCATED,
                    std::format( "read of {} bytes at offset {:#x} exceeds size {:#x}",
                    sizeof( T ), off, m_Data.size() ) );
            }

            T out {};
            std::memcpy( &out, m_Data.data() + off, sizeof( T ) );
            return out;
        }


        // Borrow a raw byte range without copying.
        [[nodiscard]] Result<ByteSpan> Bytes(
            std::uint64_t off,
            std::uint64_t len
        ) const
        {
            if (off > m_Data.size() || m_Data.size() - off < len)
            {
                return Fail( Errc::TRUNCATED,
                    std::format( "span [{:#x}, {:#x}) exceeds size {:#x}", off, off + len,
                    m_Data.size() ) );
            }

            return m_Data.subspan( static_cast<std::size_t>( off ), static_cast<std::size_t>( len ) );
        }

        // Read a fixed count array of PODs.
        template <RawPod T>
        [[nodiscard]] Result<std::vector<T>> Array(
            std::uint64_t off,
            std::uint64_t count
        ) const
        {
            if (count != 0 && count > (UINT64_MAX / static_cast<std::uint64_t>(sizeof( T ))))
            {
                return Fail( Errc::INCONSISTENT, std::format( "element count {} overflows", count ) );
            }

            const std::uint64_t extent = count * sizeof( T );
            auto raw = Bytes( off, extent );

            if (!raw)
                return std::unexpected( raw.error() );

            std::vector<T> out( static_cast<std::size_t>(count) );

            if (count != 0)
                std::memcpy( out.data(), raw->data(), static_cast<std::size_t>(extent) );

            return out;
        }

    private:

        ByteSpan m_Data;
    };

    // Append only little-endian writer.
    class ByteWriter
    {
    public:

        Bytes & Buf() noexcept
        {
            return m_Buf;
        }


        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_Buf.size();
        }


        template <RawPod T>
        void Put( const T & v )
        {
            const auto * p = reinterpret_cast<const std::byte *>(&v);
            m_Buf.insert( m_Buf.end(), p, p + sizeof( T ) );
        }


        template <RawPod T>
        void PutAt( std::size_t off, const T & v )
        {
            if (m_Buf.size() < off + sizeof( T )) m_Buf.resize( off + sizeof( T ) );
            std::memcpy( m_Buf.data() + off, &v, sizeof( T ) );
        }


        void Put( ByteSpan s )
        {
            m_Buf.insert( m_Buf.end(), s.begin(), s.end() );
        }


        void Zeros( std::size_t n )
        {
            m_Buf.insert( m_Buf.end(), n, std::byte { 0 } );
        }


        void Align( std::size_t a )
        {
            if (a > 1)
            {
                const std::size_t r = m_Buf.size() % a;
                if (r) Zeros( a - r );
            }
        }


        Bytes Take()
        {
            return std::move( m_Buf );
        }

    private:

        Bytes m_Buf;
    };

    // Fixed size, possibly unterminated char array.
    template <std::size_t N>
    [[nodiscard]] inline std::string_view FixedStr( 
        const std::array<char, N> & buf
    ) noexcept
    {
        const auto * end = static_cast<const char *>(std::memchr( buf.data(), '\0', N ));
        return end ? std::string_view( buf.data(), static_cast<std::size_t>(end - buf.data()) )
            : std::string_view( buf.data(), N );
    }


    [[nodiscard]] inline std::string_view FixedStr(
        const char * p,
        std::size_t n
    ) noexcept
    {
        const auto * end = static_cast<const char *>(std::memchr( p, '\0', n ));
        return end ? std::string_view( p, static_cast<std::size_t>(end - p) ) : std::string_view( p, n );
    }


    template <std::size_t N>
    inline void SetFixedStr( char( &dst )[N], std::string_view s )
    {
        std::memset( dst, 0, N );
        std::memcpy( dst, s.data(), std::min( s.size(), N - 1 ) );
    }


    [[nodiscard]] constexpr std::uint64_t AlignUp(
        std::uint64_t v,
        std::uint64_t a
    ) noexcept
    {
        return a <= 1 ? v : (v + a - 1) / a * a;
    }


    [[nodiscard]] inline ByteSpan AsBytes( std::string_view s ) noexcept
    {
        return { reinterpret_cast<const std::byte *>(s.data()), s.size() };
    }


    [[nodiscard]] inline std::string ToHex(
        ByteSpan s
    )
    {
        static constexpr char hex_digits[] = "0123456789abcdef";

        std::string out;
        out.reserve( s.size() * 2 );

        for (auto b : s)
        {
            out.push_back( hex_digits[static_cast<unsigned>(b) >> 4] );
            out.push_back( hex_digits[static_cast<unsigned>(b) & 15] );
        }

        return out;
    }
}