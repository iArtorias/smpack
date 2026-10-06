// glTF 2.0 reader [.glb and .gltf with external or embedded buffers] for model import.
// 
// Reads triangle primitives with positions, normals, UVs,
// colors and 4 skin influences, the node tree with names and extras, and
// skins.

#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "smpack/io.hpp"
#include "smpack/json.hpp"
#include "smpack/mesh.hpp"

namespace smpack::gltf
{
    namespace fs = std::filesystem;

    struct Primitive
    {
        std::vector<float> m_Positions; // xyz.
        std::vector<float> m_Normals; // xyz, empty when absent.
        std::array<std::vector<float>, 4> m_Uvs; // uv per set.
        std::vector<float> m_Colors; // RGBA.
        std::vector<std::uint16_t> m_Joints; // 4 per vertex, skin joint slots.
        std::vector<float> m_Weights; // 4 per vertex.
        std::vector<std::uint32_t> m_Indices; // Counter clockwise triangle list.
        int m_Material = -1;
        int m_PrimExtra = -1; // 'extras.prim' written by smpack export.
    };

    struct Node
    {
        std::string m_Name;
        int m_Parent = -1;
        mesh::Mat44 m_Local {};
        mesh::Mat44 m_World {};
        int m_Mesh = -1;
        int m_Skin = -1;
        int m_ExtraPart = -1; // 'extras.part' written by smpack export.
        int m_ExtraLod = -1;
    };

    struct Skin
    {
        std::vector<int> m_Joints; // Node indices.
        std::vector<mesh::Mat44> m_InverseBind; // Per joint, identity when absent.
    };

    struct Document
    {
        std::vector<Node> m_Nodes;
        std::vector<std::vector<Primitive>> m_Meshes;
        std::vector<std::string> m_Materials; // Names.
        std::vector<Skin> m_Skins;
        // 'scenes[0].extras.smpack', when present
        std::string m_SourceWad, m_SourceModel;
        long long m_SourceMesh = -1;
        bool m_SourceWorld = false;
    };

    namespace detail
    {
        [[nodiscard]] inline mesh::Mat44 Identity() noexcept
        {
            mesh::Mat44 m {};
            m[0] = m[5] = m[10] = m[15] = 1;
            return m;
        }

        // glTF TRS to row vector matrix [scale, then rotate, then translate].
        [[nodiscard]] inline mesh::Mat44 Trs(
            const double t[3],
            const double q[4],
            const double s[3]
        )
        {
            const double x = q[0], y = q[1], z = q[2], w = q[3];

            // Column vector rotation R, the row vector matrix holds R transposed in its upper 3x3.
            const double r[3][3] =
            {
                { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) },
            };

            mesh::Mat44 m = Identity();

            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    m[row * 4 + col] = static_cast<float>( r[col][row] * s[row] );

            m[12] = static_cast<float>( t[0] );
            m[13] = static_cast<float>( t[1] );
            m[14] = static_cast<float>( t[2] );

            return m;
        }


        [[nodiscard]] inline Result<Bytes> DecodeBase64( 
            std::string_view s
        )
        {
            auto val = [] ( char c ) -> int
            {
                if (c >= 'A' && c <= 'Z')
                    return c - 'A';

                if (c >= 'a' && c <= 'z')
                    return c - 'a' + 26;

                if (c >= '0' && c <= '9')
                    return c - '0' + 52;

                if (c == '+' || c == '-')
                    return 62;

                if (c == '/' || c == '_')
                    return 63;

                return -1;
            };

            Bytes out;
            out.reserve( s.size() * 3 / 4 );
            std::uint32_t acc = 0;
            int bits = 0;

            for (char c : s)
            {
                if (c == '=' || c == '\r' || c == '\n')
                    continue;

                const int v = val( c );

                if (v < 0)
                    return Fail( Errc::INCONSISTENT, "bad base64 in a data URI" );

                acc = acc << 6 | static_cast<std::uint32_t>( v );
                bits += 6;

                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back( static_cast<std::byte>( (acc >> bits) & 0xFF ) );
                }
            }

            return out;
        }


        [[nodiscard]] inline std::string UriDecode(
            std::string_view s
        )
        {
            std::string o;

            for (std::size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '%' && i + 2 < s.size())
                {
                    unsigned v = 0;
                    std::from_chars( s.data() + i + 1, s.data() + i + 3, v, 16 );
                    o.push_back( static_cast<char>(v) );
                    i += 2;
                }
                else
                {
                    o.push_back( s[i] );
                }
            }

            return o;
        }

        struct Reader
        {
            const json::Value & m_Doc;
            std::vector<Bytes> m_Buffers;

            // Read an accessor as floats [normalized integers are mapped to [0,1] or [-1,1]].
            [[nodiscard]] Result<std::vector<float>> Floats(
                std::int64_t index,
                std::uint32_t & comps
            ) const
            {
                const auto & accs = m_Doc.Get( "accessors" ).AsArray();

                if (index < 0 || static_cast<std::size_t>( index ) >= accs.size())
                    return Fail( Errc::BAD_OFFSET, "accessor index out of range" );

                const auto & a = accs[static_cast<std::size_t>( index )];

                if (a.Has( "sparse" ))
                    return Fail( Errc::UNSUPPORTED, "sparse accessors are not supported" );

                const std::string & type = a.Get( "type" ).AsString();
                comps = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;

                if (!comps)
                    return Fail( Errc::UNSUPPORTED, std::format( "accessor type '{}' is not supported", type ) );

                const auto ct = a.Get( "componentType" ).AsInt();
                const std::uint32_t csize = ct == 5126 || ct == 5125 ? 4 : ct == 5122 || ct == 5123 ? 2 : ct == 5120 || ct == 5121 ? 1 : 0;

                if (!csize)
                    return Fail( Errc::UNSUPPORTED, std::format( "component type {} is not supported", ct ) );

                const bool norm = a.Get( "normalized" ).AsBool();
                const auto count = static_cast<std::uint64_t>( a.Get( "count" ).AsInt() );
                std::vector<float> out( static_cast<std::size_t>(count * comps), 0.0f );

                if (!a.Has( "bufferView" ))
                    return out; // All zeros by spec.

                const auto & views = m_Doc.Get( "bufferViews" ).AsArray();
                const auto vi = a.Get( "bufferView" ).AsInt();

                if (vi < 0 || static_cast<std::size_t>( vi ) >= views.size())
                    return Fail( Errc::BAD_OFFSET, "bufferView index out of range" );

                const auto & v = views[static_cast<std::size_t>( vi )];
                const auto bi = v.Get( "buffer" ).AsInt();

                if (bi < 0 || static_cast<std::size_t>( bi ) >= m_Buffers.size())
                    return Fail( Errc::BAD_OFFSET, "buffer index out of range" );

                const Bytes & buf = m_Buffers[static_cast<std::size_t>( bi )];
                const std::uint64_t base = static_cast<std::uint64_t>( v.Get( "byteOffset" ).AsInt() ) + static_cast<std::uint64_t>( a.Get( "byteOffset" ).AsInt() );
                const std::uint64_t elem = std::uint64_t( csize ) * comps;
                const std::uint64_t stride = v.Get( "byteStride" ).AsInt() > 0 ? static_cast<std::uint64_t>( v.Get( "byteStride" ).AsInt() ) : elem;

                if (count && base + stride * (count - 1) + elem > buf.size())
                    return Fail( Errc::TRUNCATED, "accessor reads past the end of its buffer" );

                const std::byte * p = buf.data();

                for (std::uint64_t i = 0; i < count; ++i)
                {
                    const std::byte * e = p + base + i * stride;

                    for (std::uint32_t c = 0; c < comps; ++c)
                    {
                        const std::byte * q = e + c * csize;
                        float f = 0;

                        switch (ct)
                        {
                            case 5126: 
                                std::memcpy( &f, q, 4 );
                                break;
                            case 5125:
                            {
                                std::uint32_t u; std::memcpy( &u, q, 4 );
                                f = static_cast<float>( u );
                                break;
                            }
                            case 5123:
                            {
                                std::uint16_t u; std::memcpy( &u, q, 2 );
                                f = norm ? u / 65535.0f : u;
                                break;
                            }
                            case 5122:
                            {
                                std::int16_t s; std::memcpy( &s, q, 2 );
                                f = norm ? std::max( -1.0f, s / 32767.0f ) : s;
                                break;
                            }
                            case 5121:
                            {
                                const auto u = static_cast<std::uint8_t>(*q);
                                f = norm ? u / 255.0f : u;
                                break;
                            }
                            case 5120:
                            {
                                const auto s = static_cast<std::int8_t>(*q);
                                f = norm ? std::max( -1.0f, s / 127.0f ) : s;
                                break;
                            }
                        }

                        out[static_cast<std::size_t>(i * comps + c)] = f;
                    }
                }

                return out;
            }


            // Integer accessor [indices, joints] read exactly.
            [[nodiscard]] Result<std::vector<std::uint32_t>> Uints(
                std::int64_t index,
                std::uint32_t & comps
            ) const
            {
                const auto & accs = m_Doc.Get( "accessors" ).AsArray();

                if (index < 0 || static_cast<std::size_t>( index ) >= accs.size())
                    return Fail( Errc::BAD_OFFSET, "accessor index out of range" );

                const auto ct = accs[static_cast<std::size_t>( index )].Get( "componentType" ).AsInt();

                if (ct != 5121 && ct != 5123 && ct != 5125)
                    return Fail( Errc::UNSUPPORTED, "index and joint accessors must be unsigned integers" );

                SMPACK_TRY( f, Floats( index, comps ) );
                std::vector<std::uint32_t> out( f.size() );

                if (ct == 5125)
                {
                    // Reread 32 bit values without the float round trip.
                    const auto & a = accs[static_cast<std::size_t>( index )];
                    const auto & v = m_Doc.Get( "bufferViews" ).AsArray()[static_cast<std::size_t>( a.Get( "bufferView" ).AsInt() )];
                    const Bytes & buf = m_Buffers[static_cast<std::size_t>( v.Get( "buffer" ).AsInt() )];
                    const std::uint64_t base = static_cast<std::uint64_t>(v.Get( "byteOffset" ).AsInt()) + static_cast<std::uint64_t>(a.Get( "byteOffset" ).AsInt());
                    const std::uint64_t stride = v.Get( "byteStride" ).AsInt() > 0 ? static_cast<std::uint64_t>(v.Get( "byteStride" ).AsInt()) : 4ull * comps;
                    const std::size_t count = f.size() / comps;

                    for (std::size_t i = 0; i < count; ++i)
                        for (std::uint32_t c = 0; c < comps; ++c)
                            std::memcpy( &out[i * comps + c], buf.data() + base + i * stride + 4ull * c, 4 );
                }
                else
                {
                    for (std::size_t i = 0; i < f.size(); ++i)
                        out[i] = static_cast<std::uint32_t>( f[i] );
                }

                return out;
            }
        };
    }

    [[nodiscard]] inline Result<Document> Read(
        const fs::path & path
    )
    {
        SMPACK_TRY( raw, io::ReadFile( path ) );

        std::string text;
        Bytes glb_bin;
        bool has_glb_bin = false;
        std::uint32_t magic = 0;

        if (raw.size() >= 4)
            std::memcpy( &magic, raw.data(), 4 );

        if (magic == 0x46546C67)
        {
            // 'glTF'.
            ByteReader r( raw );
            SMPACK_TRY( ver, r.At<std::uint32_t>( 4 ) );

            if (ver != 2)
                return Fail( Errc::BAD_VERSION, std::format( "GLB version {} is not supported", ver ) );

            std::uint64_t off = 12;

            while (off + 8 <= raw.size())
            {
                SMPACK_TRY( len, r.At<std::uint32_t>( off ) );
                SMPACK_TRY( type, r.At<std::uint32_t>( off + 4 ) );
                SMPACK_TRY( chunk, r.Bytes( off + 8, len ) );

                if (type == 0x4E4F534A)
                    text.assign( reinterpret_cast<const char *>(chunk.data()), chunk.size() );

                else if (type == 0x004E4942 && !has_glb_bin)
                    glb_bin.assign( chunk.begin(), chunk.end() ), has_glb_bin = true;

                off += 8 + AlignUp( len, 4 );
            }

            if (text.empty())
                return Fail( Errc::INCONSISTENT, "GLB has no JSON chunk" );
        }
        else
        {
            text.assign( reinterpret_cast<const char *>(raw.data()), raw.size() );
        }

        SMPACK_TRY( doc, json::Parse( text ) );
        detail::Reader rd { doc, {} };

        for (const auto & b : doc.Get( "buffers" ).AsArray())
        {
            const std::string & uri = b.Get( "uri" ).AsString();

            if (uri.empty())
            {
                if (!has_glb_bin) 
                    return Fail( Errc::INCONSISTENT, "buffer without a URI outside a GLB" );

                rd.m_Buffers.push_back( glb_bin );
            }
            else if (uri.starts_with( "data:" ))
            {
                const auto comma = uri.find( ',' );

                if (comma == std::string::npos || uri.substr( 0, comma ).find( ";base64" ) == std::string::npos)
                    return Fail( Errc::UNSUPPORTED, "only base64 data URIs are supported" );

                SMPACK_TRY( d, detail::DecodeBase64( std::string_view( uri ).substr( comma + 1 ) ) );
                rd.m_Buffers.push_back( std::move( d ) );
            }
            else
            {
                const std::string rel = detail::UriDecode( uri );

                SMPACK_TRY( d, io::ReadFile( path.parent_path() / fs::path( std::u8string( rel.begin(), rel.end() ) ) ) );
                rd.m_Buffers.push_back( std::move( d ) );
            }
        }

        Document out;
        for (const auto & m : doc.Get( "materials" ).AsArray())
            out.m_Materials.push_back( m.Get( "name" ).AsString() );

        for (const auto & m : doc.Get( "meshes" ).AsArray())
        {
            std::vector<Primitive> prims;
            for (const auto & p : m.Get( "primitives" ).AsArray())
            {
                const auto mode = p.Has( "mode" ) ? p.Get( "mode" ).AsInt() : 4;

                if (mode != 4)
                    return Fail( Errc::UNSUPPORTED, std::format( "mesh '{}': only triangle lists can be imported (mode {})", m.Get( "name" ).AsString(), mode ) );

                const auto & at = p.Get( "attributes" );

                if (!at.Has( "POSITION" ))
                    return Fail( Errc::INCONSISTENT, "primitive without POSITION" );

                Primitive pr;
                std::uint32_t c = 0;

                SMPACK_TRY( pos, rd.Floats( at.Get( "POSITION" ).AsInt(), c ) );

                if (c != 3)
                    return Fail( Errc::INCONSISTENT, "POSITION must be VEC3" );

                pr.m_Positions = std::move( pos );
                const std::size_t nv = pr.m_Positions.size() / 3;

                if (at.Has( "NORMAL" ))
                {
                    SMPACK_TRY( n, rd.Floats( at.Get( "NORMAL" ).AsInt(), c ) );

                    if (c == 3 && n.size() == nv * 3)
                        pr.m_Normals = std::move( n );
                }

                for (int k = 0; k < 4; ++k)
                {
                    const auto key = std::format( "TEXCOORD_{}", k );

                    if (!at.Has( key ))
                        continue;

                    SMPACK_TRY( uv, rd.Floats( at.Get( key ).AsInt(), c ) );

                    if (c == 2 && uv.size() == nv * 2)
                        pr.m_Uvs[static_cast<std::size_t>( k )] = std::move( uv );
                }

                if (at.Has( "COLOR_0" ))
                {
                    SMPACK_TRY( col, rd.Floats( at.Get( "COLOR_0" ).AsInt(), c ) );

                    if (c == 4 && col.size() == nv * 4)
                        pr.m_Colors = std::move( col );

                    else if (c == 3 && col.size() == nv * 3)
                    {
                        pr.m_Colors.resize( nv * 4 );

                        for (std::size_t v = 0; v < nv; ++v)
                            pr.m_Colors[v * 4] = col[v * 3], pr.m_Colors[v * 4 + 1] = col[v * 3 + 1], pr.m_Colors[v * 4 + 2] = col[v * 3 + 2], pr.m_Colors[v * 4 + 3] = 1;
                    }
                }

                if (at.Has( "JOINTS_0" ) && at.Has( "WEIGHTS_0" ))
                {
                    SMPACK_TRY( j, rd.Uints( at.Get( "JOINTS_0" ).AsInt(), c ) );

                    if (c != 4 || j.size() != nv * 4)
                        return Fail( Errc::INCONSISTENT, "JOINTS_0 must be VEC4 per vertex" );

                    std::uint32_t cw = 0;
                    SMPACK_TRY( w, rd.Floats( at.Get( "WEIGHTS_0" ).AsInt(), cw ) );

                    if (cw != 4 || w.size() != nv * 4)
                        return Fail( Errc::INCONSISTENT, "WEIGHTS_0 must be VEC4 per vertex" );

                    pr.m_Joints.resize( j.size() );

                    for (std::size_t i = 0; i < j.size(); ++i)
                        pr.m_Joints[i] = static_cast<std::uint16_t>( std::min<std::uint32_t>( j[i], 0xFFFF ) );

                    pr.m_Weights = std::move( w );
                }

                if (p.Has( "indices" ))
                {
                    SMPACK_TRY( ix, rd.Uints( p.Get( "indices" ).AsInt(), c ) );
                    pr.m_Indices = std::move( ix );
                }
                else
                {
                    pr.m_Indices.resize( nv );

                    for (std::size_t i = 0; i < nv; ++i)
                        pr.m_Indices[i] = static_cast<std::uint32_t>( i );
                }

                if (pr.m_Indices.size() % 3)
                    return Fail( Errc::INCONSISTENT, "index count is not a multiple of 3" );

                for (auto i : pr.m_Indices)
                    if (i >= nv)
                        return Fail( Errc::INCONSISTENT, "index out of range" );

                pr.m_Material = p.Has( "material" ) ? static_cast<int>( p.Get( "material" ).AsInt() ) : -1;

                if (p.Get( "extras" ).Has( "prim" ))
                    pr.m_PrimExtra = static_cast<int>( p.Get( "extras" ).Get( "prim" ).AsInt( -1 ) );

                prims.push_back( std::move( pr ) );
            }

            out.m_Meshes.push_back( std::move( prims ) );
        }

        const auto & nodes = doc.Get( "nodes" ).AsArray();
        out.m_Nodes.resize( nodes.size() );

        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const auto & n = nodes[i];
            auto & nd = out.m_Nodes[i];

            nd.m_Name = n.Get( "name" ).AsString();
            nd.m_Mesh = n.Has( "mesh" ) ? static_cast<int>( n.Get( "mesh" ).AsInt() ) : -1;
            nd.m_Skin = n.Has( "skin" ) ? static_cast<int>( n.Get( "skin" ).AsInt() ) : -1;

            if (n.Get( "extras" ).Has( "part" ))
            {
                nd.m_ExtraPart = static_cast<int>( n.Get( "extras" ).Get( "part" ).AsInt( -1 ) );
                nd.m_ExtraLod = static_cast<int>( n.Get( "extras" ).Get( "lod" ).AsInt( -1 ) );
            }

            if (n.Has( "matrix" ) && n.Get( "matrix" ).AsArray().size() == 16)
            {
                for (int k = 0; k < 16; ++k)
                    nd.m_Local[static_cast<std::size_t>( k )] = static_cast<float>( n.Get( "matrix" ).AsArray()[static_cast<std::size_t>( k )].AsDouble() );
            }
            else
            {
                double t[3] = { 0, 0, 0 }, q[4] = { 0, 0, 0, 1 }, s[3] = { 1, 1, 1 };

                const auto & t_arr = n.Get( "translation" ).AsArray();
                const auto & r_arr = n.Get( "rotation" ).AsArray();
                const auto & s_arr = n.Get( "scale" ).AsArray();

                if (t_arr.size() == 3)
                    for (int k = 0; k < 3; ++k)
                        t[k] = t_arr[static_cast<std::size_t>( k )].AsDouble();

                if (r_arr.size() == 4)
                    for (int k = 0; k < 4; ++k)
                        q[k] = r_arr[static_cast<std::size_t>( k )].AsDouble();

                if (s_arr.size() == 3)
                    for (int k = 0; k < 3; ++k)
                        s[k] = s_arr[static_cast<std::size_t>( k )].AsDouble( 1 );

                nd.m_Local = detail::Trs( t, q, s );
            }

            for (const auto & c : n.Get( "children" ).AsArray())
            {
                const auto ci = c.AsInt( -1 );

                if (ci >= 0 && static_cast<std::size_t>( ci ) < nodes.size())
                    out.m_Nodes[static_cast<std::size_t>(ci)].m_Parent = static_cast<int>(i);
            }

            if (nd.m_Mesh >= static_cast<int>(out.m_Meshes.size()))
                return Fail( Errc::BAD_OFFSET, "node references a missing mesh" );
        }

        // World matrices, parents first [guard against cycles].
        std::vector<int> state( out.m_Nodes.size(), 0 );
        auto world = [&] ( auto && self, std::size_t i ) -> Result<mesh::Mat44>
        {
            if (state[i] == 2)
                return out.m_Nodes[i].m_World;

            if (state[i] == 1)
                return Fail( Errc::INCONSISTENT, "node hierarchy has a cycle" );

            state[i] = 1;
            mesh::Mat44 w = out.m_Nodes[i].m_Local;

            if (out.m_Nodes[i].m_Parent >= 0)
            {
                SMPACK_TRY( pw, self( self, static_cast<std::size_t>(out.m_Nodes[i].m_Parent) ) );
                w = mesh::Mul( w, pw );
            }

            out.m_Nodes[i].m_World = w;
            state[i] = 2;
            return w;
        };

        for (std::size_t i = 0; i < out.m_Nodes.size(); ++i)
            SMPACK_TRYV( world( world, i ) );

        for (const auto & s : doc.Get( "skins" ).AsArray())
        {
            Skin sk;
            for (const auto & j : s.Get( "joints" ).AsArray())
            {
                const auto ji = j.AsInt( -1 );

                if (ji < 0 || static_cast<std::size_t>( ji ) >= out.m_Nodes.size())
                    return Fail( Errc::BAD_OFFSET, "skin joint out of range" );

                sk.m_Joints.push_back( static_cast<int>( ji ) );
            }

            sk.m_InverseBind.assign( sk.m_Joints.size(), detail::Identity() );

            if (s.Has( "inverseBindMatrices" ))
            {
                std::uint32_t c = 0;
                SMPACK_TRY( ib, rd.Floats( s.Get( "inverseBindMatrices" ).AsInt(), c ) );

                if (c != 16 || ib.size() != sk.m_Joints.size() * 16)
                    return Fail( Errc::INCONSISTENT, "'inverseBindMatrices' do not match the joints" );

                for (std::size_t j = 0; j < sk.m_Joints.size(); ++j)
                    for (int k = 0; k < 16; ++k)
                        sk.m_InverseBind[j][static_cast<std::size_t>( k )] = ib[j * 16 + static_cast<std::size_t>( k )];
            }

            out.m_Skins.push_back( std::move( sk ) );
        }

        const auto & scenes = doc.Get( "scenes" ).AsArray();

        if (!scenes.empty())
        {
            const auto & src = scenes[0].Get( "extras" ).Get( "smpack" );
            out.m_SourceWad = src.Get( "wad" ).AsString();
            out.m_SourceModel = src.Get( "model" ).AsString();
            out.m_SourceMesh = src.Has( "mesh" ) ? src.Get( "mesh" ).AsInt( -1 ) : -1;
            out.m_SourceWorld = src.Get( "world" ).AsBool();
        }

        return out;
    }
}