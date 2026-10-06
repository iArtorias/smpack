// Minimal ordered JSON DOM. Parse and pretty print.

#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "smpack/reader.hpp"

namespace smpack::json
{
    class Value;
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    class Value
    {
    public:

        using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string,
            std::shared_ptr<Array>, std::shared_ptr<Object>>;

        Value() : m_V( nullptr )
        {}

        Value( std::nullptr_t ) : m_V( nullptr )
        {}

        Value( bool b ) : m_V( b )
        {}

        Value( int i ) : m_V( static_cast<std::int64_t>( i ) )
        {}

        Value( unsigned i ) : m_V( static_cast<std::int64_t>( i ) )
        {}

        Value( long i ) : m_V( static_cast<std::int64_t>(i) )
        {}

        Value( long long i ) : m_V( static_cast<std::int64_t>(i) )
        {}

        Value( unsigned long i ) : m_V( static_cast<std::int64_t>(i) )
        {}

        Value( unsigned long long i ) : m_V( static_cast<std::int64_t>(i) )
        {}

        Value( double d ) : m_V( d )
        {}

        Value( const char * s ) : m_V( std::string( s ) )
        {}

        Value( std::string s ) : m_V( std::move( s ) )
        {}

        Value( std::string_view s ) : m_V( std::string( s ) )
        {}

        Value( Array a ) : m_V( std::make_shared<Array>( std::move( a ) ) )
        {}

        Value( Object o ) : m_V( std::make_shared<Object>( std::move( o ) ) )
        {}

        static Value MakeObject()
        {
            return Value( Object {} );
        }


        static Value MakeArray()
        {
            return Value( Array {} );
        }


        [[nodiscard]] bool IsNull() const
        {
            return std::holds_alternative<std::nullptr_t>( m_V );
        }


        [[nodiscard]] bool IsBool() const
        {
            return std::holds_alternative<bool>( m_V );
        }


        [[nodiscard]] bool IsInt() const
        {
            return std::holds_alternative<std::int64_t>( m_V );
        }


        [[nodiscard]] bool IsNumber() const
        {
            return IsInt() || std::holds_alternative<double>( m_V );
        }


        [[nodiscard]] bool IsString() const
        {
            return std::holds_alternative<std::string>( m_V );
        }


        [[nodiscard]] bool IsArray() const
        {
            return std::holds_alternative<std::shared_ptr<Array>>( m_V );
        }


        [[nodiscard]] bool IsObject() const
        {
            return std::holds_alternative<std::shared_ptr<Object>>( m_V );
        }


        [[nodiscard]] bool AsBool(
            bool def = false
        ) const
        {
            return IsBool() ? std::get<bool>( m_V ) : def;
        }


        [[nodiscard]] std::int64_t AsInt(
            std::int64_t def = 0
        ) const
        {
            if (IsInt())
                return std::get<std::int64_t>( m_V );

            if (std::holds_alternative<double>( m_V ))
                return static_cast<std::int64_t>(std::get<double>( m_V ));

            return def;
        }


        [[nodiscard]] double AsDouble( 
            double def = 0
        ) const
        {
            if (IsInt())
                return static_cast<double>(std::get<std::int64_t>( m_V ));

            if (std::holds_alternative<double>( m_V ))
                return std::get<double>( m_V );

            return def;
        }


        [[nodiscard]] const std::string & AsString() const
        {
            static const std::string empty_value;
            return IsString() ? std::get<std::string>( m_V ) : empty_value;
        }


        // Parse '0x' hex strings or plain numbers into u64.
        [[nodiscard]] std::uint64_t AsU64(
            std::uint64_t def = 0
        ) const
        {
            if (IsInt())
                return static_cast<std::uint64_t>(std::get<std::int64_t>( m_V ));

            if (!IsString())
                return def;

            std::string_view s = AsString();
            int base = 10;

            if (s.starts_with( "0x" ) || s.starts_with( "0X" ))
            {
                s.remove_prefix( 2 );
                base = 16;
            }

            std::uint64_t out = 0;
            auto r = std::from_chars( s.data(), s.data() + s.size(), out, base );
            return r.ec == std::errc {} ? out : def;
        }


        Array & AsArray()
        {
            if (!IsArray()) m_V = std::make_shared<Array>();
            return *std::get<std::shared_ptr<Array>>( m_V );
        }


        [[nodiscard]] const Array & AsArray() const
        {
            static const Array empty_value;
            return IsArray() ? *std::get<std::shared_ptr<Array>>( m_V ) : empty_value;
        }


        Object & AsObject()
        {
            if (!IsObject())
                m_V = std::make_shared<Object>();

            return *std::get<std::shared_ptr<Object>>( m_V );
        }


        [[nodiscard]] const Object & AsObject() const
        {
            static const Object empty_value;
            return IsObject() ? *std::get<std::shared_ptr<Object>>( m_V ) : empty_value;
        }

        // Object member access [creates on write].
        Value & operator[](
            std::string_view key
            )
        {
            auto & o = AsObject();

            for (auto & [k, v] : o)
                if (k == key)
                    return v;

            o.emplace_back( std::string( key ), Value {} );
            return o.back().second;
        }


        [[nodiscard]] const Value & Get(
            std::string_view key
        ) const
        {
            static const Value null_value;

            for (const auto & [k, v] : AsObject())
                if (k == key) 
                    return v;

            return null_value;
        }


        [[nodiscard]] bool Has( 
            std::string_view key
        ) const
        {
            for (const auto & [k, v] : AsObject())
                if (k == key)
                    return true;

            return false;
        }


        void Push( Value v )
        {
            AsArray().push_back( std::move( v ) );
        }


        [[nodiscard]] const Storage & GetStorage() const
        {
            return m_V;
        }

    private:

        Storage m_V;
    };


    [[nodiscard]] inline std::string Hex64( std::uint64_t v )
    {
        return std::format( "{:#018x}", v );
    }


    // Dump.
    inline void EscapeInto(
        std::string & out,
        std::string_view s
    )
    {
        out.push_back( '"' );

        for (unsigned char c : s)
        {
            switch (c)
            {
                case '"': 
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    if (c < 0x20)
                        out += std::format( "\\u{:04x}", c );
                    else
                        out.push_back( static_cast<char>( c ) );
            }
        }

        out.push_back( '"' );
    }


    inline void DumpInto(
        std::string & out,
        const Value & v,
        int indent,
        int depth
    )
    {
        const auto nl = [&] ( int d )
        {
            if (indent < 0)
                return;

            out.push_back( '\n' );
            out.append( static_cast<std::size_t>( d * indent ), ' ' );
        };

        std::visit(
            [&] ( const auto & x )
        {
            using T = std::decay_t<decltype(x)>;

            if constexpr (std::is_same_v<T, std::nullptr_t>)
                out += "null";
            else if constexpr (std::is_same_v<T, bool>)
                out += x ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::int64_t>)
                out += std::to_string( x );
            else if constexpr (std::is_same_v<T, double>)
            {
                if (std::isfinite( x ))
                    out += std::format( "{}", x );
                else
                    out += "null";
            }
            else if constexpr (std::is_same_v<T, std::string>)
                EscapeInto( out, x );
            else if constexpr (std::is_same_v<T, std::shared_ptr<Array>>)
            {
                if (x->empty())
                {
                    out += "[]";
                    return;
                }

                // Short arrays of scalars stay on one line.
                bool scalar = x->size() <= 16;
                for (const auto & e : *x)
                    scalar = scalar && !e.IsArray() && !e.IsObject();

                out.push_back( '[' );

                for (std::size_t i = 0; i < x->size(); ++i)
                {
                    if (i)
                        out += scalar ? ", " : ",";

                    if (!scalar)
                        nl( depth + 1 );

                    DumpInto( out, (*x)[i], indent, depth + 1 );
                }

                if (!scalar)
                    nl( depth );

                out.push_back( ']' );
            }
            else
            {
                if (x->empty())
                {
                    out += "{}";
                    return;
                }

                out.push_back( '{' );

                for (std::size_t i = 0; i < x->size(); ++i)
                {
                    if (i)
                        out.push_back( ',' );

                    nl( depth + 1 );
                    EscapeInto( out, (*x)[i].first );

                    out += indent < 0 ? ":" : ": ";
                    DumpInto( out, (*x)[i].second, indent, depth + 1 );
                }

                nl( depth );
                out.push_back( '}' );
            }
        }, v.GetStorage() );
    }

    [[nodiscard]] inline std::string Dump( 
        const Value & v,
        int indent = 1
    )
    {
        std::string out;
        DumpInto( out, v, indent, 0 );
        out.push_back( '\n' );
        return out;
    }

    // Parse.
    class Parser
    {
    public:

        explicit Parser( std::string_view s ) : m_S( s )
        {}

        Result<Value> Parse()
        {
            auto v = ParseValue( 0 );

            if (!v)
                return v;

            Ws();

            if (m_P != m_S.size())
                return Err( "trailing characters" );

            return v;
        }

    private:

        std::string_view m_S;
        std::size_t m_P = 0;

        std::unexpected<Error> Err( std::string_view what ) const
        {
            std::size_t line = 1;
            for (std::size_t i = 0; i < m_P && i < m_S.size(); ++i)
                line += m_S[i] == '\n';

            return Fail( Errc::INCONSISTENT, std::format( "JSON parse error at line {}: {}", line, what ) );
        }

        void Ws()
        {
            while (m_P < m_S.size())
            {
                const char c = m_S[m_P];

                if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                    ++m_P;

                else if (c == '/' && m_P + 1 < m_S.size() && m_S[m_P + 1] == '/')
                { 
                    // Tolerate // comments.
                    while (m_P < m_S.size() && m_S[m_P] != '\n')
                        ++m_P;
                }
                else
                    break;
            }
        }


        Result<Value> ParseValue(
            int depth
        )
        {
            if (depth > 256)
                return Err( "nesting too deep" );

            Ws();

            if (m_P >= m_S.size())
                return Err( "unexpected end" );

            const char c = m_S[m_P];

            if (c == '{')
                return ParseObject( depth );

            if (c == '[')
                return ParseArray( depth );

            if (c == '"')
            {
                auto s = ParseString();

                if (!s)
                    return std::unexpected( s.error() );

                return Value( std::move( *s ) );
            }
            if (m_S.substr( m_P, 4 ) == "true")
            {
                m_P += 4;
                return Value( true );
            }
            if (m_S.substr( m_P, 5 ) == "false")
            {
                m_P += 5;
                return Value( false );
            }
            if (m_S.substr( m_P, 4 ) == "null")
            {
                m_P += 4;
                return Value( nullptr );
            }

            return ParseNumber();
        }


        Result<Value> ParseNumber()
        {
            const std::size_t b = m_P;
            bool flt = false;

            if (m_P < m_S.size() && (m_S[m_P] == '-' || m_S[m_P] == '+'))
                ++m_P;

            while (m_P < m_S.size())
            {
                const char c = m_S[m_P];

                if (c >= '0' && c <= '9')
                    ++m_P;
                else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+')
                {
                    flt = true;
                    ++m_P;
                }
                else
                    break;
            }

            if (b == m_P)
                return Err( "unexpected character" );

            const std::string tok( m_S.substr( b, m_P - b ) );

            if (!flt)
            {
                std::int64_t i = 0;
                auto r = std::from_chars( tok.data(), tok.data() + tok.size(), i );

                if (r.ec == std::errc {})
                    return Value( i );
            }
            try
            {
                return Value( std::stod( tok ) );
            }
            catch (...)
            {
                return Err( "bad number" );
            }
        }


        Result<std::string> ParseString()
        {
            ++m_P;
            std::string out;

            while (m_P < m_S.size())
            {
                char c = m_S[m_P++];

                if (c == '"') 
                    return out;

                if (c != '\\')
                {
                    out.push_back( c );
                    continue;
                }

                if (m_P >= m_S.size())
                    break;
                c = m_S[m_P++];

                switch (c)
                {
                    case 'n':
                        out.push_back( '\n' );
                        break;

                    case 't':
                        out.push_back( '\t' );
                        break;

                    case 'r':
                        out.push_back( '\r' );
                        break;

                    case 'b':
                        out.push_back( '\b' );
                        break;

                    case 'f':
                        out.push_back( '\f' );
                        break;

                    case 'u':
                    {
                        if (m_P + 4 > m_S.size())
                            return Err( "bad escape" );

                        unsigned cp = 0;
                        std::from_chars( m_S.data() + m_P, m_S.data() + m_P + 4, cp, 16 );
                        m_P += 4;

                        if (cp >= 0xD800 && cp < 0xDC00 && m_P + 6 <= m_S.size() && m_S[m_P] == '\\' && m_S[m_P + 1] == 'u')
                        {
                            unsigned lo = 0;
                            std::from_chars( m_S.data() + m_P + 2, m_S.data() + m_P + 6, lo, 16 );

                            if (lo >= 0xDC00 && lo < 0xE000)
                            {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                m_P += 6;
                            }
                        }

                        if (cp < 0x80)
                            out.push_back( static_cast<char>( cp ) );
                        else if (cp < 0x800)
                        {
                            out.push_back( static_cast<char>( 0xC0 | (cp >> 6) ) );
                            out.push_back( static_cast<char>( 0x80 | (cp & 0x3F) ) );
                        }
                        else if (cp < 0x10000)
                        {
                            out.push_back( static_cast<char>( 0xE0 | (cp >> 12) ) );
                            out.push_back( static_cast<char>( 0x80 | ((cp >> 6) & 0x3F) ) );
                            out.push_back( static_cast<char>( 0x80 | (cp & 0x3F) ) );
                        }
                        else
                        {
                            out.push_back( static_cast<char>( 0xF0 | (cp >> 18) ) );
                            out.push_back( static_cast<char>( 0x80 | ((cp >> 12) & 0x3F) ) );
                            out.push_back( static_cast<char>( 0x80 | ((cp >> 6) & 0x3F) ) );
                            out.push_back( static_cast<char>(0x80 | (cp & 0x3F)) );
                        }
                        break;
                    }

                    default:
                        out.push_back( c );
                }
            }

            return Err( "unterminated string" );
        }


        Result<Value> ParseArray( int depth )
        {
            ++m_P;
            Value v = Value::MakeArray();
            Ws();

            if (m_P < m_S.size() && m_S[m_P] == ']')
            {
                ++m_P;
                return v;
            }

            while (true)
            {
                auto e = ParseValue( depth + 1 );

                if (!e)
                    return e;

                v.Push( std::move( *e ) );

                Ws();

                if (m_P >= m_S.size())
                    return Err( "unterminated array" );

                if (m_S[m_P] == ',')
                {
                    ++m_P;
                    Ws();

                    if (m_P < m_S.size() && m_S[m_P] == ']')
                    {
                        ++m_P;
                        return v;
                    }
                    continue;
                }

                if (m_S[m_P] == ']')
                {
                    ++m_P;
                    return v;
                }

                return Err( "expected , or ]" );
            }
        }


        Result<Value> ParseObject( int depth )
        {
            ++m_P;
            Value v = Value::MakeObject();
            Ws();

            if (m_P < m_S.size() && m_S[m_P] == '}')
            {
                ++m_P;
                return v;
            }

            while (true)
            {
                Ws();

                if (m_P >= m_S.size() || m_S[m_P] != '"')
                    return Err( "expected key" );

                auto k = ParseString();

                if (!k)
                    return std::unexpected( k.error() );

                Ws();

                if (m_P >= m_S.size() || m_S[m_P] != ':')
                    return Err( "expected :" );

                ++m_P;

                auto e = ParseValue( depth + 1 );

                if (!e)
                    return e;

                v.AsObject().emplace_back( std::move( *k ), std::move( *e ) );
                Ws();

                if (m_P >= m_S.size())
                    return Err( "unterminated object" );

                if (m_S[m_P] == ',')
                {
                    ++m_P;
                    Ws();

                    if (m_P < m_S.size() && m_S[m_P] == '}')
                    {
                        ++m_P;
                        return v;
                    }
                    continue;
                }
                if (m_S[m_P] == '}')
                {
                    ++m_P;
                    return v;
                }

                return Err( "expected , or }" );
            }
        }
    };


    [[nodiscard]] inline Result<Value> Parse(
        std::string_view s
    )
    {
        if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF)
            s.remove_prefix( 3 ); // BOM

        return Parser( s ).Parse();
    }
}