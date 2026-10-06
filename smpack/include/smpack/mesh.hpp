// Model geometry.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "smpack/reader.hpp"

namespace smpack::mesh
{

    enum class VType : std::uint8_t
    {
        POSITION,
        NORMAL, 
        TANGENT,
        UV0, 
        UV1, 
        UV2, 
        UV3, 
        COLOR0, 
        COLOR1, 
        MATRIX_INDEX, 
        MATRIX_WEIGHT, 
        ALPHA,
        DISPLACEMENT, 
        TURBULENCE,
        WIND_PIVOT, 
        TANGENT_ROTATIONS, 
        SMOOTH_NORMAL, 
        DISPLACEMENT_MASK, 
        PREV_POSITION, 
        PREV_NORMAL,
    };

    enum class VFormat : std::uint8_t
    {
        F32,
        F16,
        U32, 
        R10G10B10A2,
        U16, 
        S16, 
        U16N, 
        S16N, 
        U8, 
        S8,
        U8N, 
        S8N
    };

    struct VertexElement
    {
        std::uint8_t m_Type;
        std::uint8_t m_Format;
        std::uint8_t m_Count;
        std::uint8_t m_Offset;
        std::uint8_t m_Slot;
        std::uint8_t m_Flags;
        std::uint16_t m_InstanceRate;
    };

    constexpr std::uint8_t STRIPPED_SLOT = 15;

    [[nodiscard]] inline std::uint32_t FormatSize(
        std::uint8_t f
    ) noexcept
    {
        switch (static_cast<VFormat>(f))
        {
            case VFormat::F32:
            case VFormat::U32:
            case VFormat::R10G10B10A2:
                return 4;

            case VFormat::F16:
            case VFormat::U16:
            case VFormat::S16:
            case VFormat::U16N:
            case VFormat::S16N:
                return 2;

            default:
                return 1;
        }
    }

    enum PrimFlags : std::uint32_t
    {
        PRIM_STREAMED = 2,
        PRIM_QUANTIZED_POSITIONS = 512,
        PRIM_RENDER_IN_MAIN_SCENE = 1024,
        PRIM_CAST_SHADOW = 2048,
    };

    struct Prim
    {
        std::uint32_t m_MaterialId = 0;
        std::uint32_t m_Flags = 0;
        std::uint32_t m_IndexOffset = 0;
        std::uint32_t m_VertexCount = 0;
        std::uint32_t m_PrimitiveCount = 0;
        std::uint16_t m_PrimId = 0;
        std::uint64_t m_StreamHash = 0;
        std::uint8_t m_IndexStride = 2;
        std::uint8_t m_Topology = 0;
        std::uint8_t m_Influences = 4; // 'SkinInfluenceFormat' k4, k7 or k10.
        std::uint16_t m_Meshlets = 0;
        std::uint64_t m_ParmOffset = 0; // 'RenPrimParm' position in the MESH chunk.
        std::uint64_t m_StreamOffsetsPos = 0; // Position of the stream offset table in the MESH chunk.
        std::vector<VertexElement> m_Elements; // Slot 15 removed.
        std::vector<std::uint32_t> m_StreamOffsets;

        [[nodiscard]] bool Streamed() const noexcept
        {
            return m_StreamHash != 0;
        }


        /// Shadow only proxies are not drawn in the main pass.
        [[nodiscard]] bool Visible() const noexcept
        {
            return (m_Flags & PRIM_RENDER_IN_MAIN_SCENE) != 0;
        }


        [[nodiscard]] bool Has( VType t ) const noexcept
        {
            for (const auto & e : m_Elements)
                if (e.m_Type == static_cast<std::uint8_t>(t)) return true;
            return false;
        }
    };


    namespace detail
    {
        [[nodiscard]] inline Result<std::uint64_t> Rel(
            const ByteReader & r,
            std::uint64_t field
        )
        {
            SMPACK_TRY( v, r.At<std::int32_t>( field ) );
            const std::int64_t t = static_cast<std::int64_t>(field) + v;

            if (t < 0 || static_cast<std::uint64_t>( t ) > r.Size())
                return Fail( Errc::BAD_OFFSET, std::format( "offset {:#x} out of range", t ) );

            return static_cast<std::uint64_t>(t);
        }


        // { OffT int [relative to the field], u32 count }.
        [[nodiscard]] inline Result<std::pair<std::uint64_t, std::uint32_t>> OffArray(
            const ByteReader & r,
            std::uint64_t field
        )
        {
            SMPACK_TRY( p, Rel( r, field ) );
            SMPACK_TRY( n, r.At<std::uint32_t>( field + 4 ) );
            return std::pair { p, n };
        }


        [[nodiscard]] inline float HalfToFloat(
            std::uint16_t h
        ) noexcept
        {
            const std::uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
            float v;

            if (e == 0)
                v = std::ldexp( static_cast<float>(m), -24 );
            else if (e == 31)
                v = m ? NAN : INFINITY;
            else
                v = std::ldexp( static_cast<float>(m | 1024), static_cast<int>(e) - 25 );

            return s ? -v : v;
        }
    }


    [[nodiscard]] inline Result<std::vector<Prim>> ParseMeshData(
        ByteSpan d
    )
    {
        ByteReader r( d );
        SMPACK_TRY( tab, detail::OffArray( r, 0x0C ) );

        if (tab.second > 65536)
            return Fail( Errc::INCONSISTENT, "implausible primitive count" );

        std::vector<Prim> out;
        out.reserve( tab.second );

        for (std::uint32_t i = 0; i < tab.second; ++i)
        {
            SMPACK_TRY( p, detail::Rel( r, tab.first + 4ull * i ) );
            SMPACK_TRY( raw, r.Bytes( p, 0x88 ) );

            auto u32 = [&] ( std::size_t o )
            {
                std::uint32_t v; std::memcpy( &v, raw.data() + o, 4 );
                return v;
            };

            auto u8 = [&] ( std::size_t o )
            {
                return static_cast<std::uint8_t>( raw[o] );
            };

            Prim pr;
            pr.m_MaterialId = u32( 0x28 );
            pr.m_IndexOffset = u32( 0x30 );
            pr.m_VertexCount = u32( 0x44 );
            pr.m_Flags = u32( 0x4C );
            pr.m_PrimitiveCount = u32( 0x48 );
            std::memcpy( &pr.m_PrimId, raw.data() + 0x50, 2 );
            std::memcpy( &pr.m_Meshlets, raw.data() + 0x52, 2 );
            pr.m_ParmOffset = p;

            const std::uint32_t elem_off = u32( 0x60 ), stream_off = u32( 0x64 );
            pr.m_StreamOffsetsPos = p + stream_off;
            pr.m_StreamHash = static_cast<std::uint64_t>(u32( 0x6C )) << 32 | u32( 0x68 );

            const std::uint8_t streams = u8( 0x80 );
            pr.m_IndexStride = u8( 0x81 );
            pr.m_Topology = u8( 0x83 );
            pr.m_Influences = std::array<std::uint8_t, 4> { 4, 7, 10, 4 }[u8( 0x87 ) & 3];

            const std::uint32_t nel = u8( 0x84 ) + std::uint32_t( u8( 0x85 ) );
            SMPACK_TRY( els, r.Array<VertexElement>( p + elem_off, nel ) );

            for (const auto & e : els)
                if (e.m_Slot != STRIPPED_SLOT) pr.m_Elements.push_back( e );

            SMPACK_TRY( so, r.Array<std::uint32_t>( p + stream_off, streams ) );

            pr.m_StreamOffsets = std::move( so );

            if (pr.m_IndexStride != 2 && pr.m_IndexStride != 4)
                pr.m_IndexStride = 2;

            out.push_back( std::move( pr ) );
        }

        return out;
    }

    struct Lod
    {
        float m_MaxDistance = 0;
        std::vector<std::uint16_t> m_PrimIds;
    };

    struct Part
    {
        std::uint32_t m_Parent = 0xFFFFFFFF; // Joint index [model groups].
        std::optional<std::array<float, 16>> m_Transform; // Row major, row vector convention [static meshes].
        std::vector<Lod> m_Lods; // Finest first.
    };

    [[nodiscard]] inline Result<std::vector<Part>> ParseModelGroup(
        ByteSpan d
    )
    {
        ByteReader r( d );

        SMPACK_TRY( np, r.At<std::uint16_t>( 0x30 ) );
        SMPACK_TRY( offs, r.Array<std::uint32_t>( 0x44, np ) );

        std::vector<Part> parts;
        for (const auto po : offs)
        {
            Part part;

            SMPACK_TRY( parent, r.At<std::uint16_t>( po ) );
            SMPACK_TRY( ndl, r.At<std::uint8_t>( po + 2 ) );

            part.m_Parent = parent;

            SMPACK_TRY( sets, r.Array<std::uint32_t>( po + 0x38ull, ndl ) );

            for (const auto so : sets)
            {
                const std::uint64_t s = std::uint64_t( po ) + so;

                SMPACK_TRY( cnt, r.At<std::uint32_t>( s ) );
                SMPACK_TRY( dist, r.At<float>( s + 4 ) );

                if (cnt > 4096) return
                    Fail( Errc::INCONSISTENT, "implausible detail set" );

                SMPACK_TRY( ids, r.Array<std::uint16_t>( s + 10, cnt ) );
                part.m_Lods.push_back( Lod { dist, std::move( ids ) } );
            }

            parts.push_back( std::move( part ) );
        }

        return parts;
    }


    [[nodiscard]] inline Result<std::vector<Part>> ParseStaticMesh(
        ByteSpan d
    )
    {
        ByteReader r( d );

        SMPACK_TRY( pa, detail::OffArray( r, 0x2C ) );
        SMPACK_TRY( ea, detail::OffArray( r, 0x44 ) );
        SMPACK_TRY( ia, detail::OffArray( r, 0x4C ) );
        SMPACK_TRY( elems, r.Array<std::uint16_t>( ea.first, ea.second ) );
        SMPACK_TRY( idx, r.Array<std::uint16_t>( ia.first, ia.second ) );

        auto prim_of = [&] ( std::uint32_t dp ) -> std::optional<std::uint16_t>
        {
            if (dp >= idx.size() || idx[dp] >= elems.size()) return std::nullopt;
            return elems[idx[dp]];
        };

        std::vector<Part> parts;
        for (std::uint32_t i = 0; i < pa.second; ++i)
        {
            SMPACK_TRY( p, detail::Rel( r, pa.first + 4ull * i ) );
            Part part;

            using Mat44 = std::array<float, 16>;

            SMPACK_TRY( m, r.At<Mat44>( p ) );
            part.m_Transform = m;

            SMPACK_TRY( jid, r.At<std::uint32_t>( p + 0x58 ) );
            part.m_Parent = jid;

            SMPACK_TRY( la, detail::OffArray( r, p + 0x68 ) );

            for (std::uint32_t k = 0; k < la.second; ++k)
            {
                SMPACK_TRY( maxz, r.At<std::uint16_t>( la.first + 10ull * k + 2 ) );
                SMPACK_TRY( b, r.At<std::uint16_t>( la.first + 10ull * k + 4 ) );
                SMPACK_TRY( e, r.At<std::uint16_t>( la.first + 10ull * k + 6 ) );

                Lod lod { detail::HalfToFloat( maxz ), {} };

                for (std::uint32_t dp = b; dp < e; ++dp)
                    if (auto id = prim_of( dp ))
                        lod.m_PrimIds.push_back( *id );

                part.m_Lods.push_back( std::move( lod ) );
            }

            parts.push_back( std::move( part ) );
        }

        return parts;
    }

    // Decoded vertex data of one primitive. Attributes absent from the
    // primitive are left empty.
    struct Geometry
    {
        std::vector<float> m_Positions; // xyz.
        std::vector<float> m_Normals; // xyz, unit length.
        std::array<std::vector<float>, 4> m_Uvs; // uv per set.
        std::vector<float> m_Colors; // RGBA.
        std::vector<std::uint16_t> m_Joints; // 4 skeleton joint indices per vertex [skinned prims].
        std::vector<float> m_Weights; // 4 weights per vertex, summing to 1.
        std::vector<std::uint32_t> m_Indices; // Triangle list, game winding.
    };

    namespace detail
    {
        inline void ReadElement(
            const std::byte * p,
            std::uint8_t fmt,
            std::uint32_t count,
            float * out
        ) noexcept
        {
            for (std::uint32_t c = 0; c < count && c < 4; ++c)
            {
                switch (static_cast<VFormat>( fmt ))
                {
                    case VFormat::F32:
                    {
                        float v; std::memcpy( &v, p + 4 * c, 4 );
                        out[c] = v; break;
                    }

                    case VFormat::F16:
                    {
                        std::uint16_t v; std::memcpy( &v, p + 2 * c, 2 );
                        out[c] = HalfToFloat( v );
                        break;
                    }

                    case VFormat::U32:
                    {
                        std::uint32_t v; std::memcpy( &v, p + 4 * c, 4 );
                        out[c] = static_cast<float>(v);
                        break;
                    }

                    case VFormat::U16:
                    {
                        std::uint16_t v; std::memcpy( &v, p + 2 * c, 2 );
                        out[c] = v;
                        break;
                    }

                    case VFormat::S16:
                    {
                        std::int16_t v;
                        std::memcpy( &v, p + 2 * c, 2 ); 
                        out[c] = v;
                        break;
                    }

                    case VFormat::U16N:
                    {
                        std::uint16_t v;
                        std::memcpy( &v, p + 2 * c, 2 );
                        out[c] = v / 65535.0f;
                        break;
                    }

                    case VFormat::S16N:
                    {
                        std::int16_t v;
                        std::memcpy( &v, p + 2 * c, 2 );
                        out[c] = std::max( -1.0f, v / 32767.0f );
                        break;
                    }

                    case VFormat::U8:
                        out[c] = static_cast<float>(static_cast<std::uint8_t>(p[c]));
                        break;

                    case VFormat::S8:
                        out[c] = static_cast<float>(static_cast<std::int8_t>(p[c]));
                        break;

                    case VFormat::U8N:
                        out[c] = static_cast<std::uint8_t>(p[c]) / 255.0f;
                        break;

                    case VFormat::S8N:
                        out[c] = std::max( -1.0f, static_cast<std::int8_t>(p[c]) / 127.0f );
                        break;

                    case VFormat::R10G10B10A2:
                    {
                        std::uint32_t v;
                        std::memcpy( &v, p, 4 );
                        out[0] = (v & 1023) / 1023.0f;
                        out[1] = ((v >> 10) & 1023) / 1023.0f;
                        out[2] = ((v >> 20) & 1023) / 1023.0f;
                        out[3] = (v >> 30) / 3.0f;
                        return;
                    }
                }
            }
        }


        [[nodiscard]] inline std::uint16_t FloatToHalf( 
            float f
        ) noexcept
        {
            std::uint32_t x;
            std::memcpy( &x, &f, 4 );

            const std::uint32_t sign = (x >> 16) & 0x8000u;
            const std::int32_t exp = static_cast<std::int32_t>((x >> 23) & 0xFF) - 127 + 15;

            std::uint32_t mant = x & 0x7FFFFFu;

            if (((x >> 23) & 0xFF) == 0xFF)
                return static_cast<std::uint16_t>(sign | 0x7C00u | (mant ? 0x200u : 0));

            if (exp >= 31)
                return static_cast<std::uint16_t>(sign | 0x7C00u);

            if (exp <= 0)
            {
                if (exp < -10)
                    return static_cast<std::uint16_t>( sign );

                mant |= 0x800000u;

                const std::uint32_t shift = static_cast<std::uint32_t>( 14 - exp );
                std::uint32_t h = mant >> shift;

                if ((mant >> (shift - 1)) & 1u)
                    ++h; // Round half up.
                return static_cast<std::uint16_t>( sign | h );
            }

            std::uint32_t h = sign | static_cast<std::uint32_t>( exp ) << 10 | (mant >> 13);

            if (mant & 0x1000u)
                ++h; // Round half up, carries into the exponent correctly.

            return static_cast<std::uint16_t>( h );
        }


        // Inverse of 'ReadElement' for plain values.
        inline void WriteElement(
            std::byte * p,
            std::uint8_t fmt,
            std::uint32_t count,
            const float * in
        ) noexcept
        {
            auto unorm = [] ( float v, float m )
            {
                return std::lround( std::clamp( v, 0.0f, 1.0f ) * m );
            };

            auto snorm = [] ( float v, float m )
            {
                return std::lround( std::clamp( v, -1.0f, 1.0f ) * m );
            };

            for (std::uint32_t c = 0; c < count && c < 4; ++c)
            {
                switch (static_cast<VFormat>( fmt ))
                {
                    case VFormat::F32:
                        std::memcpy( p + 4 * c, &in[c], 4 );
                        break;

                    case VFormat::F16:
                    {
                        const std::uint16_t v = FloatToHalf( in[c] ); std::memcpy( p + 2 * c, &v, 2 );
                        break;
                    }

                    case VFormat::U32:
                    {
                        const auto v = static_cast<std::uint32_t>(std::max( 0.0f, in[c] ) + 0.5f);
                        std::memcpy( p + 4 * c, &v, 4 );
                        break;
                    }

                    case VFormat::U16:
                    {
                        const auto v = static_cast<std::uint16_t>(std::clamp( in[c], 0.0f, 65535.0f ) + 0.5f);
                        std::memcpy( p + 2 * c, &v, 2 );
                        break;
                    }

                    case VFormat::S16:
                    {
                        const auto v = static_cast<std::int16_t>(std::lround( std::clamp( in[c], -32768.0f, 32767.0f ) ));
                        std::memcpy( p + 2 * c, &v, 2 );
                        break;
                    }

                    case VFormat::U16N:
                    {
                        const auto v = static_cast<std::uint16_t>(unorm( in[c], 65535.0f ));
                        std::memcpy( p + 2 * c, &v, 2 );
                        break;
                    }

                    case VFormat::S16N:
                    {
                        const auto v = static_cast<std::int16_t>(snorm( in[c], 32767.0f ));
                        std::memcpy( p + 2 * c, &v, 2 );
                        break;
                    }

                    case VFormat::U8:
                        p[c] = static_cast<std::byte>(static_cast<std::uint8_t>(std::clamp( in[c], 0.0f, 255.0f ) + 0.5f));
                        break;

                    case VFormat::S8:
                        p[c] = static_cast<std::byte>(static_cast<std::int8_t>(std::lround( std::clamp( in[c], -128.0f, 127.0f ) )));
                        break;

                    case VFormat::U8N:
                        p[c] = static_cast<std::byte>(static_cast<std::uint8_t>(unorm( in[c], 255.0f )));
                        break;

                    case VFormat::S8N:
                        p[c] = static_cast<std::byte>(static_cast<std::int8_t>(snorm( in[c], 127.0f )));
                        break;

                    case VFormat::R10G10B10A2:
                    {
                        const std::uint32_t v = static_cast<std::uint32_t>(unorm( in[0], 1023.0f )) | static_cast<std::uint32_t>(unorm( in[1], 1023.0f )) << 10 |
                            static_cast<std::uint32_t>(unorm( in[2], 1023.0f )) << 20 | static_cast<std::uint32_t>(unorm( in[3], 3.0f )) << 30;
                        std::memcpy( p, &v, 4 );
                        return;
                    }
                }
            }
        }
    }


    // Byte size of one element.
    [[nodiscard]] inline std::uint32_t ElementSize(
        const VertexElement & e
    ) noexcept
    {
        return e.m_Format == std::uint8_t( VFormat::R10G10B10A2 ) ? 4 : FormatSize( e.m_Format ) * e.m_Count;
    }


    // Stride of every vertex stream. The extent of the elements in that slot.
    [[nodiscard]] inline std::map<std::uint8_t, std::uint32_t> StreamStrides( const Prim & pr )
    {
        std::map<std::uint8_t, std::uint32_t> stride;

        for (const auto & e : pr.m_Elements)
            stride[e.m_Slot] = std::max( stride[e.m_Slot], e.m_Offset + ElementSize( e ) );

        return stride;
    }


    // Decode a primitive. '<data>' is the lodpack block for streamed prims or the
    // whole 'MG_<n>_gpu' GPU chunk otherwise.
    [[nodiscard]] inline Result<Geometry> Decode( const Prim & pr, ByteSpan data )
    {
        ByteReader r( data );
        Geometry g;

        const std::uint32_t n = pr.m_VertexCount;
        auto stride = StreamStrides( pr );

        for (const auto & e : pr.m_Elements)
        {
            if (e.m_Slot >= pr.m_StreamOffsets.size())
                continue;

            const auto type = static_cast<VType>(e.m_Type);
            std::vector<float> * dst = nullptr;
            std::uint32_t comps = 0;

            switch (type)
            {
                case VType::POSITION:
                    dst = &g.m_Positions;
                    comps = 3;
                    break;

                case VType::NORMAL:
                    dst = &g.m_Normals;
                    comps = 3;
                    break;

                case VType::UV0:
                case VType::UV1:
                case VType::UV2:
                case VType::UV3:
                    dst = &g.m_Uvs[static_cast<std::size_t>(type) - static_cast<std::size_t>(VType::UV0)];
                    comps = 2;
                    break;

                case VType::COLOR0:
                    dst = &g.m_Colors;
                    comps = 4;
                    break;

                default:
                    break;
            }

            if (!dst || !dst->empty())
                continue;

            const std::uint32_t st = stride[e.m_Slot];

            SMPACK_TRY( raw, r.Bytes( pr.m_StreamOffsets[e.m_Slot], std::uint64_t( st ) * n ) );
            dst->resize( std::size_t( n ) * comps );

            for (std::uint32_t v = 0; v < n; ++v)
            {
                float tmp[4] = { 0, 0, 0, 1 };
                detail::ReadElement( raw.data() + std::size_t( v ) * st + e.m_Offset, e.m_Format, e.m_Count, tmp );
                float * o = dst->data() + std::size_t( v ) * comps;

                if (type == VType::NORMAL)
                {
                    float x = tmp[0], y = tmp[1], z = tmp[2];

                    if (e.m_Format == std::uint8_t( VFormat::R10G10B10A2 ) || e.m_Format == std::uint8_t( VFormat::U8N ) ||
                        e.m_Format == std::uint8_t( VFormat::U16N ))
                    {
                        x = x * 2 - 1, y = y * 2 - 1, z = z * 2 - 1;
                    }

                    const float len = std::sqrt( x * x + y * y + z * z );

                    if (len > 1e-6f)
                        x /= len, y /= len, z /= len;
                    else
                        x = 0, y = 1, z = 0;

                    o[0] = x, o[1] = y, o[2] = z;
                }
                else
                {
                    for (std::uint32_t c = 0; c < comps; ++c)
                        o[c] = tmp[c];
                }
            }
        }

        // Skinning. The first matrix index and weight elements [4 influences].
        const VertexElement * ji = nullptr;
        const VertexElement * jw = nullptr;

        for (const auto & e : pr.m_Elements)
        {
            if (e.m_Slot >= pr.m_StreamOffsets.size())
                continue;

            if (!ji && e.m_Type == std::uint8_t( VType::MATRIX_INDEX ))
                ji = &e;

            if (!jw && e.m_Type == std::uint8_t( VType::MATRIX_WEIGHT ))
                jw = &e;
        }

        const bool packed = ji && ji->m_Format == std::uint8_t( VFormat::U32 );

        if (packed && (!jw || jw->m_Format != std::uint8_t( VFormat::U32 )))
            ji = nullptr;

        if (packed && ji)
        {
            const std::uint32_t ni = std::max<std::uint32_t>( pr.m_Influences, 4 );

            SMPACK_TRY( ri, r.Bytes( pr.m_StreamOffsets[ji->m_Slot], std::uint64_t( stride[ji->m_Slot] ) * n ) );
            SMPACK_TRY( rw, r.Bytes( pr.m_StreamOffsets[jw->m_Slot], std::uint64_t( stride[jw->m_Slot] ) * n ) );

            g.m_Joints.assign( std::size_t( n ) * 4, 0 );
            g.m_Weights.assign( std::size_t( n ) * 4, 0.0f );

            const std::uint32_t iw = ji->m_Count, ww = jw->m_Count;
            if (ni * 11 > iw * 32 || (ni - 1) > ww * 3)
                return Fail( Errc::UNSUPPORTED, std::format( "prim {}: unexpected skin packing", pr.m_PrimId ) );

            for (std::uint32_t v = 0; v < n; ++v)
            {
                std::uint32_t words[4] = {}, wts[4] = {};
                std::memcpy( words, ri.data() + std::size_t( v ) * stride[ji->m_Slot] + ji->m_Offset, 4 * std::min<std::uint32_t>( iw, 4 ) );
                std::memcpy( wts, rw.data() + std::size_t( v ) * stride[jw->m_Slot] + jw->m_Offset, 4 * std::min<std::uint32_t>( ww, 4 ) );

                auto bit = [&] ( std::uint32_t b )
                {
                    return (words[b / 32] >> (31 - b % 32)) & 1u;
                };

                std::array<std::pair<float, std::uint16_t>, 10> inf {};
                float sum = 0;

                for (std::uint32_t k = 0; k < ni && k < 10; ++k)
                {
                    std::uint32_t idx = 0;

                    if (ni == 7) 
                        idx = (words[k / 2] >> (16 * (k % 2))) & 0xFFFFu; // k7. u16 pairs, low half first.
                    else
                        for (std::uint32_t b = 0; b < 11; ++b)
                            idx = idx << 1 | bit( k * 11 + b );

                    float w = 0;
                    if (k + 1 < ni)
                    {
                        w = ((wts[k / 3] >> (10 * (k % 3))) & 1023u) / 1023.0f;
                        sum += w;
                    }
                    else
                    {
                        w = std::max( 0.0f, 1.0f - sum );
                    }

                    inf[k] = { w, static_cast<std::uint16_t>(idx) };
                }

                // Merge repeats, keep the four largest.
                for (std::uint32_t k = 0; k < ni; ++k)
                    for (std::uint32_t q = 0; q < k; ++q)
                        if (inf[q].first > 0 && inf[k].first > 0 && inf[q].second == inf[k].second)
                            inf[q].first += inf[k].first, inf[k].first = 0;

                std::sort( inf.begin(), inf.begin() + ni, [] ( auto & a, auto & b )
                {
                    return a.first > b.first;
                } );

                float top = 0;

                for (int c = 0; c < 4; ++c) 
                    top += inf[c].first;

                float rest = 0;

                for (int c = 1; c < 4; ++c)
                    rest += top > 0 ? inf[c].first / top : 0.0f;

                for (int c = 0; c < 4; ++c)
                {
                    const float w = c == 0 ? (top > 0 ? 1.0f - rest : 1.0f) : (top > 0 ? inf[c].first / top : 0.0f);
                    g.m_Joints[std::size_t( v ) * 4 + c] = w > 0 ? inf[c].second : 0;
                    g.m_Weights[std::size_t( v ) * 4 + c] = w;
                }
            }

            ji = nullptr;
        }

        if (ji)
        {
            SMPACK_TRY( ri, r.Bytes( pr.m_StreamOffsets[ji->m_Slot], std::uint64_t( stride[ji->m_Slot] ) * n ) );

            g.m_Joints.assign( std::size_t( n ) * 4, 0 );
            g.m_Weights.assign( std::size_t( n ) * 4, 0.0f );

            ByteSpan rw;
            if (jw)
            {
                SMPACK_TRY( x, r.Bytes( pr.m_StreamOffsets[jw->m_Slot], std::uint64_t( stride[jw->m_Slot] ) * n ) );
                rw = x;
            }

            const std::uint32_t nidx = std::min<std::uint32_t>( ji->m_Count, 4 );

            for (std::uint32_t v = 0; v < n; ++v)
            {
                float idx[4] = { 0, 0, 0, 0 };
                detail::ReadElement( ri.data() + std::size_t( v ) * stride[ji->m_Slot] + ji->m_Offset, ji->m_Format, nidx, idx );

                float w[4] = { 1, 0, 0, 0 };

                if (jw)
                {
                    float t[4] = { 0, 0, 0, 0 };
                    detail::ReadElement( rw.data() + std::size_t( v ) * stride[jw->m_Slot] + jw->m_Offset, jw->m_Format, jw->m_Count, t );

                    if (jw->m_Format == std::uint8_t( VFormat::R10G10B10A2 ) || jw->m_Count < 4)
                    {
                        // Three stored weights, the last one is implied.
                        w[0] = t[0], w[1] = t[1], w[2] = t[2];
                        w[3] = std::max( 0.0f, 1.0f - (t[0] + t[1] + t[2]) );
                    }
                    else
                    {
                        for (int c = 0; c < 4; ++c)
                            w[c] = t[c];
                    }
                }

                for (std::uint32_t c = 0; c < 4; ++c)
                    if (c >= nidx || w[c] < 0) w[c] = 0;

                // Merge repeated joints, then normalise so the weights sum to exactly 1.
                for (int c = 0; c < 4; ++c)
                    for (int k = 0; k < c; ++k)
                        if (w[k] > 0 && w[c] > 0 && idx[k] == idx[c])
                            w[k] += w[c], w[c] = 0;

                float sum = w[0] + w[1] + w[2] + w[3];
                int big = 0;
                for (int c = 0; c < 4; ++c)
                {
                    w[c] = sum > 0 ? w[c] / sum : (c == 0 ? 1.0f : 0.0f);

                    if (w[c] > w[big])
                        big = c;
                }

                float rest = 0;
                for (int c = 0; c < 4; ++c)
                    if (c != big)
                        rest += w[c];

                w[big] = 1.0f - rest;

                for (std::uint32_t c = 0; c < 4; ++c)
                {
                    g.m_Joints[std::size_t( v ) * 4 + c] = w[c] > 0 ? static_cast<std::uint16_t>( idx[c] ) : 0;
                    g.m_Weights[std::size_t( v ) * 4 + c] = w[c];
                }
            }
        }

        if (g.m_Positions.empty())
            return Fail( Errc::UNSUPPORTED, std::format( "prim {} has no position stream", pr.m_PrimId ) );

        const std::uint64_t ni = std::uint64_t( pr.m_PrimitiveCount ) * 3;

        SMPACK_TRY( ib, r.Bytes( pr.m_IndexOffset, ni * pr.m_IndexStride ) );

        g.m_Indices.resize( static_cast<std::size_t>(ni) );

        for (std::uint64_t i = 0; i < ni; ++i)
        {
            std::uint32_t v = 0;

            if (pr.m_IndexStride == 2)
            {
                std::uint16_t s;
                std::memcpy( &s, ib.data() + i * 2, 2 );
                v = s;
            }
            else
            {
                std::memcpy( &v, ib.data() + i * 4, 4 );
            }

            if (v >= n)
                return Fail( Errc::INCONSISTENT, std::format( "prim {}: index {} out of range ({} vertices)", pr.m_PrimId, v, n ) );

            g.m_Indices[static_cast<std::size_t>(i)] = v;
        }

        return g;
    }

    using Mat44 = std::array<float, 16>;

    [[nodiscard]] inline Mat44 Mul(
        const Mat44 & a,
        const Mat44 & b
    ) noexcept
    {
        Mat44 r {};

        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                float s = 0;
                for (int k = 0; k < 4; ++k)
                    s += a[i * 4 + k] * b[k * 4 + j];

                r[i * 4 + j] = s;
            }

        return r;
    }


    // Inverse of an affine row vector matrix.
    [[nodiscard]] inline Mat44 InverseAffine( 
        const Mat44 & m
    ) noexcept
    {
        const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
        const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
        const float k = std::abs( det ) > 1e-20f ? 1.0f / det : 0.0f;

        Mat44 r {};
        r[0] = (e * i - f * h) * k, r[1] = (c * h - b * i) * k, r[2] = (b * f - c * e) * k;
        r[4] = (f * g - d * i) * k, r[5] = (a * i - c * g) * k, r[6] = (c * d - a * f) * k;
        r[8] = (d * h - e * g) * k, r[9] = (b * g - a * h) * k, r[10] = (a * e - b * d) * k;

        const float tx = m[12], ty = m[13], tz = m[14];
        r[12] = -(tx * r[0] + ty * r[4] + tz * r[8]);
        r[13] = -(tx * r[1] + ty * r[5] + tz * r[9]);
        r[14] = -(tx * r[2] + ty * r[6] + tz * r[10]);
        r[15] = 1;
        return r;
    }

    enum JointFlags : std::uint16_t
    {
        JOINT_IK = 1,
        JOINT_FACE_CAMERA_UP = 2,
        JOINT_FACE_CAMERA = 4,
        JOINT_HIDDEN = 8,
        JOINT_DECAL_ROTATE = 16,
        JOINT_KEEP = 32,
        JOINT_VISIBILITY_ANIMATED = 64,
    };

    // A visual config of a game object ('Helwalker00', 'seidr01_moderate' etc.).
    struct SkeletonConfig
    {
        std::string m_Name;
        std::vector<bool> m_Visible;
    };

    struct Skeleton
    {
        std::vector<std::int16_t> m_Parents;
        std::vector<std::uint16_t> m_Flags; // Joint flags.
        std::vector<Mat44> m_Local; // Relative to the parent
        std::vector<Mat44> m_World; // Model space bind pose
        std::vector<Mat44> m_InvBind; // Skinning and rigid attachment. 'vertex * inv_bind * world' [identity for joints nothing is bound to].
        std::vector<std::uint16_t> m_ConfigJoints; // Joints whose visibility depends on the config.
        std::vector<SkeletonConfig> m_Configs;

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_Parents.size();
        }


        // Index of the config shown by default. The first one that is not a
        // decapitation, scratch or empty config.
        [[nodiscard]] int DefaultConfig() const noexcept
        {
            auto skip = [] ( const std::string & n )
            {
                std::string l;
                for (char c : n)
                    l.push_back( static_cast<char>(std::tolower( static_cast<unsigned char>(c) )) );

                return l.find( "decap" ) != std::string::npos || l.starts_with( "temp" ) || l.starts_with( "config" );
            };

            for (std::size_t i = 0; i < m_Configs.size(); ++i)
                if (!skip( m_Configs[i].m_Name ))
                    return static_cast<int>( i );

            return m_Configs.empty() ? -1 : 0;
        }


        // Visible state of every joint. '<config>' picks a variant ['-1', no config,
        // only the hidden flags apply]. '<all>' shows everything.
        [[nodiscard]] std::vector<bool> Visibility(
            int config,
            bool all = false
        ) const
        {
            std::vector<bool> vis( Size(), true );

            if (all)
                return vis;

            std::vector<int> slot( Size(), -1 );
            const SkeletonConfig * cfg = config >= 0 && static_cast<std::size_t>(config) < m_Configs.size() ? &m_Configs[static_cast<std::size_t>(config)] : nullptr;

            if (cfg)
                for (std::size_t i = 0; i < m_ConfigJoints.size(); ++i)
                    if (m_ConfigJoints[i] < Size()) slot[m_ConfigJoints[i]] = static_cast<int>(i);

            for (std::size_t j = 0; j < Size(); ++j)
            {
                bool v = j >= m_Flags.size() || !(m_Flags[j] & JOINT_HIDDEN);

                if (cfg && slot[j] >= 0)
                    v = cfg->m_Visible[static_cast<std::size_t>( slot[j] )];

                if (m_Parents[j] >= 0)
                    v = v && vis[static_cast<std::size_t>( m_Parents[j] )];

                vis[j] = v;
            }

            return vis;
        }
    };


    [[nodiscard]] inline Result<Skeleton> ParseSkeleton(
        ByteSpan d
    )
    {
        ByteReader r( d );
        SMPACK_TRY( n, r.At<std::uint16_t>( 0x10 ) );

        if (n == 0 || n > 4096)
            return Fail( Errc::INCONSISTENT, "implausible joint count" );

        Skeleton sk;
        for (std::uint32_t j = 0; j < n; ++j)
        {
            SMPACK_TRY( fl, r.At<std::uint16_t>( 0x18 + 8ull * j ) );
            SMPACK_TRY( par, r.At<std::int16_t>( 0x18 + 8ull * j + 6 ) );

            if (par >= static_cast<std::int32_t>( j ))
                return Fail( Errc::INCONSISTENT, "joint parent after its child" );

            sk.m_Parents.push_back( par );
            sk.m_Flags.push_back( fl );
        }

        const std::uint64_t v8 = 8ull * (4ull * n + 3);
        const std::uint64_t off = 15 + v8 - ((v8 - 1) & 15);

        SMPACK_TRY( nj, r.At<std::uint32_t>( off ) );
        SMPACK_TRY( bind_off, r.At<std::uint32_t>( off + 8 ) );

        if (nj != n || bind_off != 0x50 + 64ull * n)
            return Fail( Errc::INCONSISTENT, "joint matrix buffer not where expected" );

        SMPACK_TRY( nb, r.At<std::uint32_t>( off + 4 ) );
        SMPACK_TRY( loc, r.Array<Mat44>( off + 0x50, n ) );

        sk.m_Local = std::move( loc );

        if (nb == n)
        {
            SMPACK_TRY( ib, r.Array<Mat44>( off + bind_off, n ) );
            sk.m_InvBind = std::move( ib );
        }

        sk.m_World.resize( n );
        for (std::uint32_t j = 0; j < n; ++j)
            sk.m_World[j] = sk.m_Parents[j] < 0 ? sk.m_Local[j] : Mul( sk.m_Local[j], sk.m_World[static_cast<std::size_t>(sk.m_Parents[j])] );

        if (sk.m_InvBind.empty())
            for (const auto & w : sk.m_World)
                sk.m_InvBind.push_back( InverseAffine( w ) );

        // Visual configs follow the matrices. u16 joint count, u16 config count,
        // u16 variant count, u16 joints[count], one visibility bit mask per config,
        // then 56 byte config names. Absent when the counts are '0'.
        SMPACK_TRY( ncfg, r.At<std::uint16_t>( off + 12 ) );

        const std::uint64_t tail = off + bind_off + 64ull * nb;
        if (ncfg > 0 && tail + 6 <= d.size())
        {
            auto cj = r.At<std::uint16_t>( tail );
            auto cc = r.At<std::uint16_t>( tail + 2 );

            if (cj && cc && *cc == ncfg && *cj > 0 && *cj <= n)
            {
                const std::uint64_t mask_bytes = (*cj + 7u) / 8u;
                const std::uint64_t masks = tail + 6 + 2ull * *cj;
                const std::uint64_t names = masks + mask_bytes * ncfg;

                auto joints = r.Array<std::uint16_t>( tail + 6, *cj );

                if (joints && names <= d.size())
                {
                    sk.m_ConfigJoints = std::move( *joints );

                    for (std::uint32_t c = 0; c < ncfg; ++c)
                    {
                        SkeletonConfig cfg;
                        const std::uint64_t no = names + 56ull * c;

                        if (no < d.size())
                            cfg.m_Name = std::string( FixedStr( reinterpret_cast<const char *>( d.data() + no ), std::min<std::size_t>( 56, d.size() - no ) ) );

                        if (cfg.m_Name.empty()) cfg.m_Name = std::format( "config{}", c );

                        auto m = r.Bytes( masks + mask_bytes * c, mask_bytes );

                        if (!m)
                            break;

                        cfg.m_Visible.resize( *cj );

                        for (std::uint32_t b = 0; b < *cj; ++b)
                            cfg.m_Visible[b] = (static_cast<unsigned>( (*m)[b / 8] ) >> (b % 8)) & 1u;

                        sk.m_Configs.push_back( std::move( cfg ) );
                    }

                    if (sk.m_Configs.size() != ncfg)
                        sk.m_Configs.clear(), sk.m_ConfigJoints.clear();
                }
            }
        }

        return sk;
    }
}