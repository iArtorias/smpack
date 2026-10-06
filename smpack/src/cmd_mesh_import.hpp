// 'mesh import'. Put an edited glTF (.glb, .gltf) back into a game model.

#pragma once

#include <compare>
#include <limits>

#include "cmd_formats.hpp"
#include "cmd_mesh.hpp"
#include "smpack/gltf.hpp"

namespace smpack::cli
{
    namespace mesh_import
    {
        using mesh::Mat44;
        using mesh::VFormat;
        using mesh::VType;

        // Geometry of one game primitive to be, in the game's part space and winding.
        struct NewMesh
        {
            std::vector<float> m_Pos, m_Nrm;
            std::array<std::vector<float>, 4> m_Uv;
            std::vector<float> m_Col;
            std::vector<std::uint16_t> m_Joints; // Skeleton joint indices, 4 per vertex.
            std::vector<float> m_Weights;
            std::vector<std::uint32_t> m_Idx; // Clockwise, as the game draws.
            bool m_HasNrm = false;
            std::array<bool, 4> m_HasUv {};
            bool m_HasCol = false;
            bool m_HasSkin = false;
            bool m_EmptyFlags = true; // Attribute flags not set yet.

            [[nodiscard]] std::size_t Vcount() const noexcept
            {
                return m_Pos.size() / 3;
            }


            // Append another piece. An attribute stays only if every piece has it.
            void Append( const NewMesh & o )
            {
                const auto base = static_cast<std::uint32_t>(Vcount());

                if (m_EmptyFlags)
                {
                    m_HasNrm = o.m_HasNrm, m_HasUv = o.m_HasUv, m_HasCol = o.m_HasCol, m_HasSkin = o.m_HasSkin;
                    m_EmptyFlags = false;
                }
                else
                {
                    m_HasNrm = m_HasNrm && o.m_HasNrm, m_HasCol = m_HasCol && o.m_HasCol, m_HasSkin = m_HasSkin && o.m_HasSkin;

                    for (int k = 0; k < 4; ++k)
                        m_HasUv[k] = m_HasUv[k] && o.m_HasUv[k];
                }

                auto cat = [] ( auto & a, const auto & b, std::size_t want_old, std::size_t want_new )
                {
                    a.resize( want_old );

                    if (b.size() == want_new)
                        a.insert( a.end(), b.begin(), b.end() );
                    else
                        a.resize( want_old + want_new );
                };

                const std::size_t n0 = base, n1 = o.Vcount();
                cat( m_Nrm, o.m_Nrm, n0 * 3, n1 * 3 );

                for (int k = 0; k < 4; ++k)
                    cat( m_Uv[k], o.m_Uv[k], n0 * 2, n1 * 2 );

                cat( m_Col, o.m_Col, n0 * 4, n1 * 4 );
                cat( m_Joints, o.m_Joints, n0 * 4, n1 * 4 );
                cat( m_Weights, o.m_Weights, n0 * 4, n1 * 4 );

                m_Pos.insert( m_Pos.end(), o.m_Pos.begin(), o.m_Pos.end() );

                for (auto i : o.m_Idx)
                    m_Idx.push_back( base + i );
            }
        };

        // Nearest point lookup on a uniform grid.
        class NearestIndex
        {
        private:

            const std::vector<float> * m_Pos = nullptr;
            float m_Min[3] {}, m_Cell = 1;
            int m_Dim[3] { 1, 1, 1 };
            std::vector<std::uint32_t> m_Start, m_Items;

            [[nodiscard]] int Cell(
                float v,
                int axis
            ) const noexcept
            {
                return std::clamp( static_cast<int>((v - m_Min[axis]) / m_Cell), 0, m_Dim[axis] - 1 );
            }

        public:

            void Build(
                const std::vector<float> & pos
            )
            {
                m_Pos = &pos;
                const std::size_t n = pos.size() / 3;

                if (n == 0)
                    return;

                float mx[3] = { pos[0], pos[1], pos[2] };
                for (int a = 0; a < 3; ++a)
                    m_Min[a] = pos[static_cast<std::size_t>( a )];

                for (std::size_t i = 0; i < n; ++i)
                    for (int a = 0; a < 3; ++a)
                        m_Min[a] = std::min( m_Min[a], pos[i * 3 + a] ),
                        mx[a] = std::max( mx[a], pos[i * 3 + a] );

                const float ext = std::max( { mx[0] - m_Min[0], mx[1] - m_Min[1], mx[2] - m_Min[2], 1e-4f } );
                const double cells = std::max( 1.0, static_cast<double>( n ) / 4.0 );

                m_Cell = std::max( static_cast<float>( ext / std::cbrt( cells ) ), 1e-5f );

                for (int a = 0; a < 3; ++a)
                    m_Dim[a] = std::clamp( static_cast<int>( (mx[a] - m_Min[a]) / m_Cell ) + 1, 1, 512 );

                const std::size_t total = std::size_t( m_Dim[0] ) * m_Dim[1] * m_Dim[2];
                m_Start.assign( total + 1, 0 );

                std::vector<std::uint32_t> cell_of( n );
                for (std::size_t i = 0; i < n; ++i)
                {
                    const auto c = static_cast<std::uint32_t>( (std::size_t( Cell( pos[i * 3 + 2], 2 ) ) *
                        m_Dim[1] + Cell( pos[i * 3 + 1], 1 )) * m_Dim[0] + Cell( pos[i * 3], 0 ) );

                    cell_of[i] = c;
                    ++m_Start[c + 1];
                }

                for (std::size_t c = 0; c < total; ++c)
                    m_Start[c + 1] += m_Start[c];

                m_Items.resize( n );
                std::vector<std::uint32_t> fill( m_Start.begin(), m_Start.end() - 1 );

                for (std::size_t i = 0; i < n; ++i)
                    m_Items[fill[cell_of[i]]++] = static_cast<std::uint32_t>( i );
            }


            [[nodiscard]] bool Empty() const noexcept
            {
                return m_Items.empty();
            }


            [[nodiscard]] std::uint32_t Query( 
                const float * p
            ) const
            {
                const int c[3] = { Cell( p[0], 0 ), Cell( p[1], 1 ), Cell( p[2], 2 ) };
                std::uint32_t best = 0;
                float best_d = std::numeric_limits<float>::max();
                const int max_r = std::max( { m_Dim[0], m_Dim[1], m_Dim[2] } );

                for (int r = 0; r <= max_r; ++r)
                {
                    for (int z = c[2] - r; z <= c[2] + r; ++z)
                    {
                        if (z < 0 || z >= m_Dim[2])
                            continue;

                        for (int y = c[1] - r; y <= c[1] + r; ++y)
                        {
                            if (y < 0 || y >= m_Dim[1])
                                continue;

                            for (int x = c[0] - r; x <= c[0] + r; ++x)
                            {
                                if (x < 0 || x >= m_Dim[0])
                                    continue;

                                if (std::max( { std::abs( x - c[0] ), std::abs( y - c[1] ), std::abs( z - c[2] ) } ) != r)
                                    continue;

                                const std::size_t cell = (std::size_t( z ) * m_Dim[1] + y) * m_Dim[0] + x;

                                for (std::uint32_t k = m_Start[cell]; k < m_Start[cell + 1]; ++k)
                                {
                                    const std::uint32_t i = m_Items[k];

                                    const float * q = m_Pos->data() + std::size_t( i ) * 3;
                                    const float d = (q[0] - p[0]) * (q[0] - p[0]) + (q[1] - p[1]) * (q[1] - p[1]) + (q[2] - p[2]) * (q[2] - p[2]);

                                    if (d < best_d)
                                        best_d = d, best = i;
                                }
                            }
                        }
                    }

                    // Every point in a farther ring is at least r cells away.
                    if (best_d < std::numeric_limits<float>::max() && r * m_Cell * r * m_Cell >= best_d)
                        break;
                }

                return best;
            }
        };


        [[nodiscard]] inline std::array<float, 3> XformPoint(
            const Mat44 & m,
            const float * v
        ) noexcept
        {
            return { v[0] * m[0] + v[1] * m[4] + v[2] * m[8] + m[12], v[0] * m[1] + v[1] * m[5] + v[2] * m[9] + m[13],
                v[0] * m[2] + v[1] * m[6] + v[2] * m[10] + m[14] };
        }


        [[nodiscard]] inline std::array<float, 3> XformDir(
            const Mat44 & m,
            const float * v
        ) noexcept
        {
            std::array<float, 3> r = { v[0] * m[0] + v[1] * m[4] + v[2] * m[8], v[0] * m[1] + v[1] * m[5] + v[2] * m[9], v[0] * m[2] + v[1] * m[6] + v[2] * m[10] };
            const float l = std::sqrt( r[0] * r[0] + r[1] * r[1] + r[2] * r[2] );

            if (l > 1e-12f)
                r[0] /= l, r[1] /= l, r[2] /= l;

            return r;
        }

        // Inverse transpose of the upper 3x3, for normals under non uniform scale.
        [[nodiscard]] inline Mat44 NormalMatrix( const Mat44 & m ) noexcept
        {
            Mat44 inv = mesh::InverseAffine( m );
            Mat44 r {};

            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    r[i * 4 + j] = inv[j * 4 + i];

            r[15] = 1;
            return r;
        }


        // Area weighted vertex normals for a triangle list.
        inline void ComputeNormals( NewMesh & g )
        {
            const std::size_t n = g.Vcount();
            g.m_Nrm.assign( n * 3, 0.0f );

            for (std::size_t t = 0; t + 2 < g.m_Idx.size(); t += 3)
            {
                const float * a = &g.m_Pos[g.m_Idx[t] * 3ull];
                const float * b = &g.m_Pos[g.m_Idx[t + 1] * 3ull];
                const float * c = &g.m_Pos[g.m_Idx[t + 2] * 3ull];
                const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };

                // Clockwise front faces. e2xe1 points out.
                const float cr[3] = { e2[1] * e1[2] - e2[2] * e1[1], e2[2] * e1[0] - e2[0] * e1[2], e2[0] * e1[1] - e2[1] * e1[0] };

                for (int k = 0; k < 3; ++k)
                    for (auto v : { g.m_Idx[t], g.m_Idx[t + 1], g.m_Idx[t + 2] })
                        g.m_Nrm[v * 3ull + k] += cr[k];
            }

            for (std::size_t v = 0; v < n; ++v)
            {
                float * p = &g.m_Nrm[v * 3];
                const float l = std::sqrt( p[0] * p[0] + p[1] * p[1] + p[2] * p[2] );

                if (l > 1e-12f)
                    p[0] /= l, p[1] /= l, p[2] /= l;
                else
                    p[0] = 0, p[1] = 1, p[2] = 0;
            }

            g.m_HasNrm = true;
        }


        // Per vertex tangent [xyz] and handedness from UV set.
        inline void ComputeTangents(
            const NewMesh & g,
            int set,
            std::vector<float> & tan,
            std::vector<float> & sign
        )
        {
            const std::size_t n = g.Vcount();
            std::vector<double> tan_sum( n * 3, 0.0 ), bitan_sum( n * 3, 0.0 );
            const auto & u = g.m_Uv[static_cast<std::size_t>(set)];

            for (std::size_t t = 0; t + 2 < g.m_Idx.size(); t += 3)
            {
                const std::uint32_t a = g.m_Idx[t], b = g.m_Idx[t + 1], c = g.m_Idx[t + 2];
                double e1[3], e2[3];

                for (int k = 0; k < 3; ++k)
                    e1[k] = g.m_Pos[b * 3ull + k] - g.m_Pos[a * 3ull + k], e2[k] = g.m_Pos[c * 3ull + k] - g.m_Pos[a * 3ull + k];

                const double du1 = u[b * 2ull] - u[a * 2ull], dv1 = u[b * 2ull + 1] - u[a * 2ull + 1];
                const double du2 = u[c * 2ull] - u[a * 2ull], dv2 = u[c * 2ull + 1] - u[a * 2ull + 1];

                double r = du1 * dv2 - du2 * dv1;

                if (std::abs( r ) < 1e-20)
                    continue;

                r = 1.0 / r;

                for (int k = 0; k < 3; ++k)
                {
                    const double tt = (e1[k] * dv2 - e2[k] * dv1) * r, bb = (e2[k] * du1 - e1[k] * du2) * r;

                    for (auto v : { a, b, c })
                        tan_sum[v * 3ull + k] += tt, bitan_sum[v * 3ull + k] += bb;
                }
            }

            tan.assign( n * 3, 0.0f );
            sign.assign( n, 1.0f );

            for (std::size_t v = 0; v < n; ++v)
            {
                const double nx = g.m_Nrm[v * 3], ny = g.m_Nrm[v * 3 + 1], nz = g.m_Nrm[v * 3 + 2];
                double tx = tan_sum[v * 3], ty = tan_sum[v * 3 + 1], tz = tan_sum[v * 3 + 2];
                const double d = tx * nx + ty * ny + tz * nz;
                tx -= nx * d, ty -= ny * d, tz -= nz * d;
                double l = std::sqrt( tx * tx + ty * ty + tz * tz );

                if (l < 1e-12)
                {
                    // Any vector perpendicular to the normal.
                    if (std::abs( nx ) < 0.9)
                        tx = 0, ty = -nz, tz = ny;
                    else
                        tx = nz, ty = 0, tz = -nx;

                    l = std::sqrt( tx * tx + ty * ty + tz * tz );
                }

                tan[v * 3] = static_cast<float>(tx / l), tan[v * 3 + 1] = static_cast<float>(ty / l), tan[v * 3 + 2] = static_cast<float>(tz / l);

                const double cx = ny * tz - nz * ty, cy = nz * tx - nx * tz, cz = nx * ty - ny * tx;
                sign[v] = cx * bitan_sum[v * 3] + cy * bitan_sum[v * 3 + 1] + cz * bitan_sum[v * 3 + 2] < 0 ? -1.0f : 1.0f;
            }
        }

        struct Encoded
        {
            std::map<std::uint8_t, Bytes> m_Streams; // Per slot.
            Bytes m_Indices;
            Bytes m_MeshletInfo, m_MeshletCull; // Only for primitives that use meshlets.
            std::uint32_t m_MeshletCount = 0, m_MeshletMaxIndices = 0;
            std::uint8_t m_IndexStride = 2;
            std::uint32_t m_Vertices = 0, m_Triangles = 0;
            float m_Aabb[6] {}; // Half extent, center.
            std::array<float, 4> m_Mip {}; // Mipmap constants.
            bool m_MipValid = false;
            bool m_SameOrder = false; // Vertex v is still original vertex v [same count, UVs or nearest match].
        };

        // Sum of triangle areas in space and in UV set.
        inline void Areas( 
            const std::vector<float> & pos,
            const std::vector<float> & uv,
            const std::vector<std::uint32_t> & idx,
            double & world,
            double & tex
        )
        {
            world = tex = 0;

            if (uv.size() * 3 != pos.size() * 2)
                return;

            for (std::size_t t = 0; t + 2 < idx.size(); t += 3)
            {
                const std::uint32_t a = idx[t], b = idx[t + 1], c = idx[t + 2];
                double e1[3], e2[3];

                for (int k = 0; k < 3; ++k)
                    e1[k] = pos[b * 3ull + k] - pos[a * 3ull + k], e2[k] = pos[c * 3ull + k] - pos[a * 3ull + k];

                const double cx = e1[1] * e2[2] - e1[2] * e2[1], cy = e1[2] * e2[0] - e1[0] * e2[2], cz = e1[0] * e2[1] - e1[1] * e2[0];

                world += 0.5 * std::sqrt( cx * cx + cy * cy + cz * cz );
                tex += 0.5 * std::abs( (uv[b * 2ull] - uv[a * 2ull]) * (uv[c * 2ull + 1] - uv[a * 2ull + 1]) -
                    (uv[c * 2ull] - uv[a * 2ull]) * (uv[b * 2ull + 1] - uv[a * 2ull + 1]) );
            }
        }

        class Encoder
        {
        private:

            const mesh::Prim & m_Prim;
            ByteSpan m_Orig; // Original buffer [block or GPU chunk].
            const mesh::Geometry * m_OrigGeo; // Decoded original, may be null.
            const mesh::Skeleton * m_Skel;
            std::vector<std::string> & m_Warnings;
            std::string m_Label;
            std::map<std::uint8_t, std::uint32_t> m_Stride;

            // Joint usable by an 8 bit index. The joint itself or its nearest ancestor below 256.
            [[nodiscard]] std::uint16_t Fit8( std::uint16_t j, bool & clamped ) const
            {
                while (j >= 256 && m_Skel && j < m_Skel->Size() && m_Skel->m_Parents[j] >= 0)
                {
                    j = static_cast<std::uint16_t>( m_Skel->m_Parents[j] );
                    clamped = true;
                }
                if (j >= 256) clamped = true, j = 0;
                return j;
            }

            [[nodiscard]] const std::byte * OrigElement( const mesh::VertexElement & e, std::uint32_t v ) const
            {
                if (!m_OrigGeo || v >= m_Prim.m_VertexCount || e.m_Slot >= m_Prim.m_StreamOffsets.size()) return nullptr;
                const std::uint64_t off = m_Prim.m_StreamOffsets[e.m_Slot] + std::uint64_t( v ) * m_Stride.at( e.m_Slot ) + e.m_Offset;
                if (off + mesh::ElementSize( e ) > m_Orig.size()) return nullptr;
                return m_Orig.data() + off;
            }

        public:

            Encoder( const mesh::Prim & p, ByteSpan orig, const mesh::Geometry * geo, const mesh::Skeleton * skel, std::vector<std::string> & warnings,
                std::string label )
                : m_Prim( p ), m_Orig( orig ), m_OrigGeo( geo ), m_Skel( skel ), m_Warnings( warnings ), m_Label( std::move( label ) ), m_Stride( mesh::StreamStrides( p ) )
            {}

            [[nodiscard]] Result<Encoded> Run( NewMesh g )
            {
                const auto & pr = m_Prim;

                // 'Triangles', 'TriPatches' and 'TriAdjDomPatches' [triangle list and 12 index dominant data patches].
                if (pr.m_Topology != 0 && pr.m_Topology != 1 && pr.m_Topology != 5)
                    return Fail( Errc::UNSUPPORTED, std::format( "{}: tessellation topology {} cannot be replaced yet", m_Label, pr.m_Topology ) );

                if (pr.m_Flags & mesh::PRIM_QUANTIZED_POSITIONS)
                    return Fail( Errc::UNSUPPORTED, std::format( "{}: quantized positions are not supported", m_Label ) );

                if (g.Vcount() == 0 || g.m_Idx.empty())
                {
                    // Removed. One degenerate triangle keeps the primitive valid.
                    const float p0[3] = 
                    { 
                        m_OrigGeo && !m_OrigGeo->m_Positions.empty() ? m_OrigGeo->m_Positions[0] : 0.0f,
                        m_OrigGeo && !m_OrigGeo->m_Positions.empty() ? m_OrigGeo->m_Positions[1] : 0.0f,
                        m_OrigGeo && !m_OrigGeo->m_Positions.empty() ? m_OrigGeo->m_Positions[2] : 0.0f 
                    };

                    g = NewMesh {};
                    g.m_Pos.assign( p0, p0 + 3 );
                    g.m_Idx = { 0, 0, 0 };
                    g.m_EmptyFlags = false;
                }

                if (!g.m_HasNrm)
                    ComputeNormals( g );

                const std::uint32_t n = static_cast<std::uint32_t>(g.Vcount());

                NearestIndex lookup;
                std::vector<std::uint32_t> nearest;
                if (m_OrigGeo && !m_OrigGeo->m_Positions.empty())
                {
                    lookup.Build( m_OrigGeo->m_Positions );
                    nearest.resize( n );

                    for (std::uint32_t v = 0; v < n; ++v)
                        nearest[v] = lookup.Query( &g.m_Pos[v * 3ull] );
                }

                // Data keyed by original vertex index [pose space deformers] survives only an unchanged order.
                bool same_order = false;
                if (m_OrigGeo && n == m_Prim.m_VertexCount)
                {
                    std::uint32_t same = 0;
                    const auto & ouv = m_OrigGeo->m_Uvs[0];
                    const bool by_uv = g.m_HasUv[0] && ouv.size() == std::size_t( n ) * 2;

                    for (std::uint32_t v = 0; v < n; ++v)
                    {
                        if (by_uv)
                            same += std::abs( g.m_Uv[0][v * 2ull] - ouv[v * 2ull] ) < 1e-3f && std::abs( g.m_Uv[0][v * 2ull + 1] - ouv[v * 2ull + 1] ) < 1e-3f;
                        else
                            same += !nearest.empty() && nearest[v] == v;
                    }

                    same_order = same >= n - n / 100;
                }

                std::vector<float> tan, tsign;
                int tan_set = -1;

                for (int k = 0; k < 4 && tan_set < 0; ++k)
                    if (g.m_HasUv[k]) tan_set = k;

                if (tan_set >= 0)
                    ComputeTangents( g, tan_set, tan, tsign );

                Encoded out;
                out.m_SameOrder = same_order;

                for (const auto & [slot, stride] : m_Stride)
                    if (slot < pr.m_StreamOffsets.size())
                        out.m_Streams[slot].assign( std::size_t( n ) * stride, std::byte { 0 } );

                const mesh::VertexElement * ji = nullptr;
                const mesh::VertexElement * jw = nullptr;
                for (const auto & e : pr.m_Elements)
                {
                    if (e.m_Slot >= pr.m_StreamOffsets.size())
                        continue;

                    if (!ji && e.m_Type == std::uint8_t( VType::MATRIX_INDEX ))
                        ji = &e;

                    if (!jw && e.m_Type == std::uint8_t( VType::MATRIX_WEIGHT ))
                        jw = &e;
                }

                bool clamped = false, copied = false;
                for (const auto & e : pr.m_Elements)
                {
                    if (e.m_Slot >= pr.m_StreamOffsets.size())
                        continue;

                    const std::uint32_t st = m_Stride[e.m_Slot];
                    std::byte * base = out.m_Streams[e.m_Slot].data() + e.m_Offset;

                    const auto type = static_cast<VType>(e.m_Type);
                    const bool r10 = e.m_Format == std::uint8_t( VFormat::R10G10B10A2 );
                    const bool unorm_dir = r10 || e.m_Format == std::uint8_t( VFormat::U8N ) || e.m_Format == std::uint8_t( VFormat::U16N );

                    auto copy_nearest = [&] ( std::uint32_t v )
                    {
                        std::byte * dst = base + std::size_t( v ) * st;
                        if (nearest.empty()) return;
                        if (const std::byte * src = OrigElement( e, nearest[v] )) std::memcpy( dst, src, mesh::ElementSize( e ) );
                    };

                    for (std::uint32_t v = 0; v < n; ++v)
                    {
                        std::byte * dst = base + std::size_t( v ) * st;
                        float tmp[4] = { 0, 0, 0, 0 };

                        switch (type)
                        {
                            case VType::POSITION:
                            case VType::PREV_POSITION:
                                tmp[0] = g.m_Pos[v * 3ull], tmp[1] = g.m_Pos[v * 3ull + 1], tmp[2] = g.m_Pos[v * 3ull + 2], tmp[3] = 1;
                                mesh::detail::WriteElement( dst, e.m_Format, e.m_Count, tmp );
                                break;

                            case VType::NORMAL:
                            case VType::SMOOTH_NORMAL:
                            case VType::PREV_NORMAL:
                                for (int k = 0; k < 3; ++k)
                                    tmp[k] = unorm_dir ? g.m_Nrm[v * 3ull + k] * 0.5f + 0.5f : g.m_Nrm[v * 3ull + k];

                                mesh::detail::WriteElement( dst, e.m_Format, e.m_Count, tmp );
                                break;

                            case VType::TANGENT:
                                if (tan.empty())
                                {
                                    copy_nearest( v );
                                    copied = true;
                                    break;
                                }

                                for (int k = 0; k < 3; ++k)
                                    tmp[k] = unorm_dir ? tan[v * 3ull + k] * 0.5f + 0.5f : tan[v * 3ull + k];

                                // R10G10B10A2. Handedness in the 2 bit alpha, 3 for a mirrored frame.
                                tmp[3] = r10 ? (tsign[v] < 0 ? 1.0f : 0.0f) : tsign[v];
                                mesh::detail::WriteElement( dst, e.m_Format, e.m_Count, tmp );
                                break;

                            case VType::UV0:
                            case VType::UV1:
                            case VType::UV2:
                            case VType::UV3:
                            {
                                const auto k = static_cast<std::size_t>(type) - static_cast<std::size_t>(VType::UV0);

                                if (!g.m_HasUv[k])
                                {
                                    copy_nearest( v );
                                    copied = true;
                                    break;
                                }

                                tmp[0] = g.m_Uv[k][v * 2ull], tmp[1] = g.m_Uv[k][v * 2ull + 1];
                                mesh::detail::WriteElement( dst, e.m_Format, e.m_Count, tmp );
                                break;
                            }

                            case VType::COLOR0:
                                if (!g.m_HasCol)
                                {
                                    copy_nearest( v );
                                    break;
                                }

                                for (int k = 0; k < 4; ++k)
                                    tmp[k] = g.m_Col[v * 4ull + k];

                                mesh::detail::WriteElement( dst, e.m_Format, e.m_Count, tmp );
                                break;

                            case VType::MATRIX_INDEX:
                            case VType::MATRIX_WEIGHT:
                                if (!g.m_HasSkin) copy_nearest( v );
                                break; // Skinned input is written below.

                            default:
                                copy_nearest( v );
                                break;
                        }
                    }
                }

                if (g.m_HasSkin && ji)
                {
                    const std::uint32_t si = m_Stride[ji->m_Slot];
                    const std::uint32_t sw = jw ? m_Stride[jw->m_Slot] : 0;

                    for (std::uint32_t v = 0; v < n; ++v)
                    {
                        std::byte * di = out.m_Streams[ji->m_Slot].data() + std::size_t( v ) * si + ji->m_Offset;
                        std::byte * dw = jw ? out.m_Streams[jw->m_Slot].data() + std::size_t( v ) * sw + jw->m_Offset : nullptr;

                        WriteSkin( *ji, jw, di, dw, &g.m_Joints[v * 4ull], &g.m_Weights[v * 4ull], clamped );
                    }
                }
                else if (ji && !g.m_HasSkin && nearest.empty())
                {
                    return Fail( Errc::INCONSISTENT, std::format( "{}: the game primitive is skinned but the imported mesh has no skin weights", m_Label ) );
                }

                if (clamped)
                    m_Warnings.push_back( std::format( "{}: some vertices are bound to joints above 255, which this primitive cannot address. "
                        "They were moved to the nearest parent joint",
                        m_Label ) );

                (void)copied;

                // Indices.
                std::vector<std::uint32_t> all = g.m_Idx;
                if (pr.m_Topology == 5)
                {
                    const auto patches = DominantPatches( g );
                    all.insert( all.end(), patches.begin(), patches.end() );
                }

                out.m_IndexStride = pr.m_IndexStride;

                if (n > 0xFFFF)
                    out.m_IndexStride = 4;

                out.m_Indices.resize( all.size() * out.m_IndexStride );

                for (std::size_t i = 0; i < all.size(); ++i)
                {
                    if (out.m_IndexStride == 2)
                    {
                        const auto s = static_cast<std::uint16_t>( all[i] );
                        std::memcpy( out.m_Indices.data() + i * 2, &s, 2 );
                    }
                    else
                    {
                        std::memcpy( out.m_Indices.data() + i * 4, &all[i], 4 );
                    }
                }

                if (pr.m_Meshlets)
                    BuildMeshlets( g, out );

                out.m_Vertices = n;
                out.m_Triangles = static_cast<std::uint32_t>(g.m_Idx.size() / 3);

                float mn[3] = { g.m_Pos[0], g.m_Pos[1], g.m_Pos[2] }, mx[3] = { g.m_Pos[0], g.m_Pos[1], g.m_Pos[2] };

                for (std::uint32_t v = 0; v < n; ++v)
                    for (int k = 0; k < 3; ++k)
                        mn[k] = std::min( mn[k], g.m_Pos[v * 3ull + k] ), 
                        mx[k] = std::max( mx[k], g.m_Pos[v * 3ull + k] );

                for (int k = 0; k < 3; ++k)
                    out.m_Aabb[k] = (mx[k] - mn[k]) * 0.5f, out.m_Aabb[3 + k] = (mx[k] + mn[k]) * 0.5f;

                // Texture streaming constants. Scale the original by the change in texel density.
                if (m_OrigGeo)
                {
                    for (int k = 0; k < 4; ++k)
                    {
                        double wa0, ta0, wa1, ta1;

                        Areas( m_OrigGeo->m_Positions, m_OrigGeo->m_Uvs[static_cast<std::size_t>( k )], m_OrigGeo->m_Indices, wa0, ta0 );
                        Areas( g.m_Pos, g.m_HasUv[k] ? g.m_Uv[k] : std::vector<float> {}, g.m_Idx, wa1, ta1 );

                        out.m_Mip[static_cast<std::size_t>( k )] = wa0 > 0 && ta0 > 0 && wa1 > 0 && ta1 > 0 ? static_cast<float>(std::sqrt( (ta1 / wa1) / (ta0 / wa0) )) : 1.0f;
                    }

                    out.m_MipValid = true;
                }

                return out;
            }


            // Meshlets as the game stores them. Consecutive runs of the triangle list with at most 64
            // vertices and 126 triangles.
            static void BuildMeshlets( const NewMesh & g, Encoded & out )
            {
                constexpr std::size_t max_verts = 64, max_tris = 126;
                std::vector<std::uint32_t> mark( g.Vcount(), ~0u );
                std::size_t start = 0;

                auto emit = [&] ( std::size_t first, std::size_t end )
                {
                    const auto first32 = static_cast<std::uint32_t>(first), count = static_cast<std::uint32_t>(end - first);
                    const std::size_t o = out.m_MeshletInfo.size();
                    out.m_MeshletInfo.resize( o + 8 );
                    std::memcpy( out.m_MeshletInfo.data() + o, &first32, 4 );
                    std::memcpy( out.m_MeshletInfo.data() + o + 4, &count, 4 );

                    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
                    for (std::size_t i = first; i < end; ++i)
                        for (int k = 0; k < 3; ++k)
                            mn[k] = std::min( mn[k], g.m_Pos[g.m_Idx[i] * 3ull + k] ),
                            mx[k] = std::max( mx[k], g.m_Pos[g.m_Idx[i] * 3ull + k] );

                    const float he[3] = { (mx[0] - mn[0]) * 0.5f, (mx[1] - mn[1]) * 0.5f, (mx[2] - mn[2]) * 0.5f };
                    const float r = std::sqrt( he[0] * he[0] + he[1] * he[1] + he[2] * he[2] );

                    // Rounded up so the stored sphere still encloses the meshlet.
                    const std::uint16_t h[4] = { mesh::detail::FloatToHalf( (mx[1] + mn[1]) * 0.5f ), mesh::detail::FloatToHalf( (mx[0] + mn[0]) * 0.5f ),
                        mesh::detail::FloatToHalf( r * 1.002f + 1e-3f ), mesh::detail::FloatToHalf( (mx[2] + mn[2]) * 0.5f ) };
                    const std::size_t c = out.m_MeshletCull.size();

                    out.m_MeshletCull.resize( c + 8 );
                    std::memcpy( out.m_MeshletCull.data() + c, h, 8 );

                    out.m_MeshletMaxIndices = std::max( out.m_MeshletMaxIndices, count );
                    ++out.m_MeshletCount;
                };

                std::size_t verts = 0;
                const std::uint32_t id_base = 0;
                std::uint32_t current = id_base;

                for (std::size_t t = 0; t * 3 < g.m_Idx.size(); ++t)
                {
                    std::size_t add = 0;
                    for (int k = 0; k < 3; ++k)
                    {
                        const auto v = g.m_Idx[t * 3 + k];
                        bool dup = mark[v] == current;

                        for (int q = 0; q < k; ++q)
                            dup = dup || g.m_Idx[t * 3 + q] == v;

                        add += !dup;
                    }

                    if (verts + add > max_verts || t - start / 3 >= max_tris)
                    {
                        emit( start, t * 3 );

                        start = t * 3;
                        verts = 0;
                        ++current;
                    }

                    for (int k = 0; k < 3; ++k)
                    {
                        const auto v = g.m_Idx[t * 3 + k];

                        if (mark[v] != current)
                            mark[v] = current, ++verts;
                    }
                }

                if (start < g.m_Idx.size())
                    emit( start, g.m_Idx.size() );
            }


            // 'TriAdjDomPatches' data. Per triangle and corner [vertex, dominant vertex of the edge to the next
            // corner, dominant vertex of the edge from the previous corner, dominant corner]. Vertices at the same
            // position are the same corner, the dominant one is the first in index order, the dominant edge the
            // first triangle edge with that pair of positions. Seams then displace identically on both sides.
            [[nodiscard]] static std::vector<std::uint32_t> DominantPatches( const NewMesh & g )
            {
                const std::size_t n = g.Vcount(), tris = g.m_Idx.size() / 3;
                std::map<std::array<float, 3>, std::uint32_t> ids;
                std::vector<std::uint32_t> pos_id( n );

                for (std::size_t v = 0; v < n; ++v)
                    pos_id[v] = ids.try_emplace( { g.m_Pos[v * 3], g.m_Pos[v * 3 + 1], g.m_Pos[v * 3 + 2] },
                        static_cast<std::uint32_t>( ids.size() ) ).first->second;

                std::vector<std::uint32_t> dom_corner( ids.size(), ~0u );

                for (auto v : g.m_Idx)
                    if (dom_corner[pos_id[v]] == ~0u)
                        dom_corner[pos_id[v]] = v;

                std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, std::uint32_t>> dom_edge;
                for (std::size_t t = 0; t < tris; ++t)
                    for (int e = 0; e < 3; ++e)
                    {
                        const std::uint32_t a = g.m_Idx[t * 3 + e], c = g.m_Idx[t * 3 + (e + 1) % 3];
                        dom_edge.try_emplace( std::minmax( pos_id[a], pos_id[c] ), std::pair { a, c } );
                    }

                auto edge_vertex = [&] ( std::uint32_t a, std::uint32_t c, std::uint32_t at )
                {
                    const auto [x, y] = dom_edge.at( std::minmax( pos_id[a], pos_id[c] ) );
                    return pos_id[x] == pos_id[at] ? x : y;
                };

                std::vector<std::uint32_t> out;
                out.reserve( tris * 12 );

                for (std::size_t t = 0; t < tris; ++t)
                {
                    const std::uint32_t v[3] = { g.m_Idx[t * 3], g.m_Idx[t * 3 + 1], g.m_Idx[t * 3 + 2] };

                    for (int i = 0; i < 3; ++i)
                    {
                        const std::uint32_t vi = v[i], vn = v[(i + 1) % 3], vp = v[(i + 2) % 3];

                        out.push_back( vi );
                        out.push_back( edge_vertex( vi, vn, vi ) );
                        out.push_back( edge_vertex( vp, vi, vi ) );
                        out.push_back( dom_corner[pos_id[vi]] );
                    }
                }

                return out;
            }


            void WriteSkin( 
                const mesh::VertexElement & ji,
                const mesh::VertexElement * jw,
                std::byte * di,
                std::byte * dw,
                const std::uint16_t * joints,
                const float * weights,
                bool & clamped
            ) const
            {
                // Merge, sort by weight, normalise.
                std::array<std::pair<float, std::uint16_t>, 4> inf {};
                int cnt = 0;
                for (int k = 0; k < 4; ++k)
                {
                    if (!(weights[k] > 0))
                        continue;

                    bool merged = false;
                    for (int q = 0; q < cnt; ++q)
                        if (inf[q].second == joints[k])
                            inf[q].first += weights[k], merged = true;

                    if (!merged)
                        inf[cnt++] = { weights[k], joints[k] };
                }

                if (cnt == 0)
                    inf[cnt++] = { 1.0f, joints[0] };

                for (int a = 1; a < cnt; ++a) // Insertion sort, strongest first.
                    for (int b = a; b > 0 && inf[b].first > inf[b - 1].first; --b)
                        std::swap( inf[b], inf[b - 1] );

                const bool packed = ji.m_Format == std::uint8_t( VFormat::U32 );
                const bool idx8 = ji.m_Format == std::uint8_t( VFormat::U8 ) || ji.m_Format == std::uint8_t( VFormat::U8N );

                if (idx8)
                    for (int k = 0; k < cnt; ++k)
                        inf[k].second = Fit8( inf[k].second, clamped );

                if (packed)
                {
                    const std::uint32_t ni = std::max<std::uint32_t>( m_Prim.m_Influences, 4 );
                    cnt = std::min<int>( cnt, static_cast<int>( ni ) );

                    float sum = 0;
                    for (int k = 0; k < cnt; ++k)
                        sum += inf[k].first;

                    // Slot 'ni-1' holds the implied weight. The strongest influence goes there.
                    std::vector<std::uint16_t> idx( ni, inf[0].second );
                    std::vector<std::uint32_t> q( ni, 0 );

                    for (int k = 1; k < cnt; ++k)
                    {
                        idx[static_cast<std::size_t>( k - 1 )] = inf[k].second;
                        q[static_cast<std::size_t>( k - 1 )] = static_cast<std::uint32_t>( std::lround( inf[k].first / sum * 1023.0f ) );
                    }

                    std::uint32_t qs = 0;
                    for (std::uint32_t k = 0; k + 1 < ni; ++k)
                        qs += q[k];

                    while (qs > 1023)
                        for (std::uint32_t k = 0; k + 1 < ni && qs > 1023; ++k)
                            if (q[k])
                                --q[k], --qs;

                    std::uint32_t words[4] = {}, wts[4] = {};
                    for (std::uint32_t k = 0; k < ni; ++k)
                    {
                        if (ni == 7)
                        {
                            words[k / 2] |= std::uint32_t( idx[k] ) << (16 * (k % 2));
                        }
                        else
                        {
                            for (std::uint32_t b = 0; b < 11; ++b)
                            {
                                const std::uint32_t bit = k * 11 + b;

                                if ((idx[k] >> (10 - b)) & 1u)
                                    words[bit / 32] |= 1u << (31 - bit % 32);
                            }
                        }

                        if (k + 1 < ni)
                            wts[k / 3] |= (q[k] & 1023u) << (10 * (k % 3));
                    }

                    std::memcpy( di, words, 4 * std::min<std::uint32_t>( ji.m_Count, 4 ) );

                    if (dw && jw)
                        std::memcpy( dw, wts, 4 * std::min<std::uint32_t>( jw->m_Count, 4 ) );
                    return;
                }

                const std::uint32_t nidx = std::min<std::uint32_t>( ji.m_Count, 4 );
                cnt = std::min<int>( cnt, static_cast<int>(nidx) );

                float sum = 0;
                for (int k = 0; k < cnt; ++k)
                    sum += inf[k].first;

                float idxf[4] = { 0, 0, 0, 0 }, w[4] = { 0, 0, 0, 0 };
                const bool implied = jw && (jw->m_Format == std::uint8_t( VFormat::R10G10B10A2 ) || jw->m_Count < 4);

                if (implied && nidx == 4)
                {
                    // Three stored weights, the fourth [slot 3] implied. Strongest there.
                    idxf[3] = inf[0].second;
                    w[3] = inf[0].first / sum;

                    for (int k = 1; k < cnt; ++k)
                        idxf[k - 1] = inf[k].second, w[k - 1] = inf[k].first / sum;

                    for (int k = cnt - 1; k < 3; ++k)
                        idxf[k] = inf[0].second;
                }
                else
                {
                    for (int k = 0; k < cnt; ++k)
                        idxf[k] = inf[k].second, w[k] = inf[k].first / sum;

                    for (std::uint32_t k = static_cast<std::uint32_t>( cnt ); k < nidx; ++k)
                        idxf[k] = inf[0].second;
                }

                mesh::detail::WriteElement( di, ji.m_Format, nidx, idxf );

                if (dw && jw)
                {
                    if (!implied)
                    {
                        // All weights stored. Push the rounding error onto the strongest.
                        const float scale = jw->m_Format == std::uint8_t( VFormat::U8N ) ? 255.0f : jw->m_Format == std::uint8_t( VFormat::U16N ) ? 65535.0f : 0.0f;
                        if (scale > 0)
                        {
                            long total = 0;
                            long qv[4];
                            for (int k = 0; k < 4; ++k)
                                qv[k] = std::lround( w[k] * scale ), total += qv[k];

                            qv[0] += static_cast<long>( scale ) - total;

                            for (int k = 0; k < 4; ++k)
                                w[k] = static_cast<float>( qv[k] ) / scale;
                        }
                    }

                    float tmp[4] = { w[0], w[1], w[2], 0.0f };

                    if (!implied)
                        tmp[3] = w[3];

                    mesh::detail::WriteElement( dw, jw->m_Format, jw->m_Count, tmp );
                }
            }
        };


        // Strip Blender's '.001' style suffix.
        [[nodiscard]] inline std::string BaseName(
            std::string n
        )
        {
            if (auto d = n.rfind( '.' ); d != std::string::npos && d + 1 < n.size() &&
                std::all_of( n.begin() + static_cast<std::ptrdiff_t>(d) + 1, n.end(), [] ( char c )
            {
                return std::isdigit( static_cast<unsigned char>(c) );
            } )) n.resize( d );

            return n;
        }


        // The model name itself is part 0 of a single part model.
        [[nodiscard]] inline std::optional<std::pair<int, int>> PartFromName( 
            const std::string & node,
            const std::string & model,
            std::size_t parts
        )
        {
            const std::string n = BaseName( node );

            if (n == model && parts == 1)
                return std::pair { 0, 0 };

            const std::string pre = model + "_part";
            int lod = 0;
            std::string rest;

            if (n.starts_with( pre ))
                rest = n.substr( pre.size() );
            else if (parts == 1 && n.starts_with( model + "_lod" ))
                rest = "0" + n.substr( model.size() );
            else
                return std::nullopt;

            std::size_t i = 0;
            while (i < rest.size() && std::isdigit( static_cast<unsigned char>( rest[i] ) ))
                ++i;

            if (i == 0)
                return std::nullopt;

            const int part = std::atoi( rest.substr( 0, i ).c_str() );

            if (i < rest.size())
            {
                if (!rest.substr( i ).starts_with( "_lod" ))
                    return std::nullopt;

                lod = std::atoi( rest.substr( i + 4 ).c_str() );
            }

            return std::pair { part, lod };
        }


        [[nodiscard]] inline std::optional<std::uint16_t> JointFromName(
            const std::string & node
        )
        {
            const std::string n = BaseName( node );

            if (!n.starts_with( "joint_" ))
                return std::nullopt;

            std::uint32_t v = 0;
            const auto r = std::from_chars( n.data() + 6, n.data() + n.size(), v );

            if (r.ec != std::errc {} || r.ptr != n.data() + n.size() || v > 0xFFFF)
                return std::nullopt;

            return static_cast<std::uint16_t>(v);
        }


        // Returns 'true' when an imported piece is the original primitive. Same vertex count, triangles and positions.
        [[nodiscard]] inline bool SameGeometry(
            const NewMesh & g,
            const mesh::Geometry * o
        )
        {
            if (!o || g.Vcount() != o->m_Positions.size() / 3 || g.m_Idx != o->m_Indices)
                return false;

            float ext = 0;
            for (auto v : o->m_Positions)
                ext = std::max( ext, std::abs( v ) );

            const float tol = std::max( 1e-5f, ext * 1e-5f );

            for (std::size_t i = 0; i < g.m_Pos.size(); ++i)
                if (!(std::abs( g.m_Pos[i] - o->m_Positions[i] ) <= tol))
                    return false;

            return true;
        }


        // 'RenPSDParm' in 'RenModelGroup'.
        [[nodiscard]] inline Result<std::size_t> DropPsdPrims(
            Bytes & mg,
            const std::set<std::uint16_t> & prims
        )
        {
            constexpr std::size_t parm_size = 0x40, rec_size = 0x90;
            constexpr std::size_t rec_prim = 0x6A, rec_sample_ids = 0x78, cnt_prims = 0x2A;

            if (prims.empty() || mg.size() < 0x3C)
                return std::size_t( 0 );

            std::uint32_t base = 0;
            std::memcpy( &base, mg.data() + 0x38, 4 );

            if (base == 0)
                return std::size_t( 0 );

            if (std::uint64_t( base ) + parm_size > mg.size())
                return Fail( Errc::INCONSISTENT, "pose space deformer table out of range" );

            std::uint32_t recs_off = 0;
            std::uint16_t count = 0;

            std::memcpy( &recs_off, mg.data() + base + 0x14, 4 );
            std::memcpy( &count, mg.data() + base + cnt_prims, 2 );

            const std::uint64_t recs = std::uint64_t( base ) + recs_off;

            if (recs + std::uint64_t( count ) * rec_size > mg.size())
                return Fail( Errc::INCONSISTENT, "pose space deformer primitives out of range" );

            std::size_t kept = 0;

            for (std::size_t i = 0; i < count; ++i)
            {
                std::byte * r = mg.data() + recs + i * rec_size;
                std::uint16_t pid = 0;
                std::memcpy( &pid, r + rec_prim, 2 );

                if (prims.count( pid ))
                    continue;

                if (kept != i)
                {
                    // 'offset_sample_indices_' is relative to the field. Moving the record changes it.
                    std::byte * dst = mg.data() + recs + kept * rec_size;
                    std::memmove( dst, r, rec_size );

                    std::uint32_t rel = 0;
                    std::memcpy( &rel, dst + rec_sample_ids, 4 );
                    rel += static_cast<std::uint32_t>((i - kept) * rec_size);
                    std::memcpy( dst + rec_sample_ids, &rel, 4 );
                }

                ++kept;
            }

            const std::size_t dropped = count - kept;

            for (std::size_t i = kept; i < count; ++i)
                std::memset( mg.data() + recs + i * rec_size, 0, rec_size );

            const auto nc = static_cast<std::uint16_t>( kept );
            std::memcpy( mg.data() + base + cnt_prims, &nc, 2 );
            return dropped;
        }


        // Primitive IDs with pose space deformer records.
        [[nodiscard]] inline std::set<std::uint16_t> PsdPrims( 
            ByteSpan mg
        )
        {
            std::set<std::uint16_t> out;

            std::uint32_t base = 0, recs_off = 0;
            std::uint16_t count = 0;

            if (mg.size() < 0x3C)
                return out;

            std::memcpy( &base, mg.data() + 0x38, 4 );

            if (base == 0 || std::uint64_t( base ) + 0x40 > mg.size())
                return out;

            std::memcpy( &recs_off, mg.data() + base + 0x14, 4 );
            std::memcpy( &count, mg.data() + base + 0x2A, 2 );

            const std::uint64_t recs = std::uint64_t( base ) + recs_off;

            if (recs + std::uint64_t( count ) * 0x90 > mg.size())
                return out;

            for (std::size_t i = 0; i < count; ++i)
            {
                std::uint16_t pid = 0;
                std::memcpy( &pid, mg.data() + recs + i * 0x90 + 0x6A, 2 );
                out.insert( pid );
            }

            return out;
        }


        // New hash for an edited lodpack block. Never '0', never a hash the base packs or this edit already use.
        [[nodiscard]] inline std::uint64_t NewBlockHash(
            std::uint64_t old_hash,
            const Bytes & data,
            const LodLibrary & lib,
            const std::set<std::uint64_t> & taken
        )
        {
            std::uint64_t key[2] = { old_hash, Fnv1a64( data.data(), data.size() ) };
            std::uint64_t h = Fnv1a64( key, sizeof( key ) );

            while (h == 0 || h == old_hash || lib.Has( h ) || taken.count( h ))
            {
                ++key[1];
                h = Fnv1a64( key, sizeof( key ) );
            }

            return h;
        }


        // The WAD's own lod stream table.
        [[nodiscard]] inline Result<void> RehashWadLodToc(
            WadEdit & we,
            const std::map<std::uint64_t, std::uint64_t> & rehash,
            const std::map<std::uint64_t, Bytes> & data
        )
        {
            constexpr std::uint64_t toc_field = 0x24;

            for (std::size_t i = 0; i < we.m_W.m_Entries.size(); ++i)
            {
                const auto & e = we.m_W.m_Entries[i];

                if (e.m_E.m_Id != 1 || !e.Name().starts_with( "WAD_" ))
                    continue;

                Bytes & d = we.m_Payloads[i].m_Data;

                if (d.size() < toc_field + 4)
                    continue;

                std::uint32_t off = 0;
                std::memcpy( &off, d.data() + toc_field, 4 );

                if (off == 0 || std::uint64_t( off ) + sizeof( lodpack::Header ) > d.size())
                    continue;

                lodpack::Header h {};
                std::memcpy( &h, d.data() + off, sizeof( h ) );

                const std::uint64_t blocks_at = off + sizeof( h ) + std::uint64_t( h.m_GroupCount ) * 24;
                const std::uint64_t end = blocks_at + std::uint64_t( h.m_BlockCount ) * 24;

                if (end > d.size())
                    return Fail( Errc::INCONSISTENT, std::format( "{}: lod stream table overruns the chunk", e.Name() ) );

                std::vector<lodpack::BlockEntry> blocks( h.m_BlockCount );

                if (!blocks.empty())
                    std::memcpy( blocks.data(), d.data() + blocks_at, blocks.size() * 24 );

                std::size_t hits = 0;
                for (auto & b : blocks)
                    if (auto it = rehash.find( b.m_BlockDataHash ); it != rehash.end())
                    {
                        b.m_BlockDataHash = it->second;
                        b.m_BlockDataSize = static_cast<std::uint32_t>(data.at( it->first ).size());
                        ++hits;
                    }

                if (hits != rehash.size())
                    return Fail( Errc::INCONSISTENT, std::format( "{}: {} of {} edited lodpack blocks are not in the WAD's lod stream table",
                        e.Name(), rehash.size() - hits, rehash.size() ) );

                // Searched by hash.
                std::sort( blocks.begin(), blocks.end(), [] ( const lodpack::BlockEntry & a, const lodpack::BlockEntry & b )
                {
                    return a.m_BlockDataHash < b.m_BlockDataHash;
                } );

                std::memcpy( d.data() + blocks_at, blocks.data(), blocks.size() * 24 );
                return {};
            }

            return Fail( Errc::NOT_FOUND, "the WAD has no lod stream table, its streamed geometry cannot be replaced" );
        }
    }

    [[nodiscard]] inline Result<void> CmdMeshImport( const Args & args )
    {
        using namespace mesh_import;

        if (args.m_Pos.size() < 2)
            return Fail( Errc::BAD_ARGUMENT,
                "mesh import <file.wad> <model.glb|gltf> [--name EXACT | --id N] [--lods all|same] [--world] [--game DIR] "
                "[--patch NAME] [--append] [-o DIR] [--install GAME_DIR] [--json]" );

        const fs::path wp = args.m_Pos[0];
        const fs::path gp = args.m_Pos[1];
        const bool js = args.Has( "--json" );
        const std::string lods_mode = args.GetOr( "--lods", "all" );

        if (lods_mode != "all" && lods_mode != "same")
            return Fail( Errc::BAD_ARGUMENT, "--lods must be 'all' or 'same'" );

        std::vector<std::string> warnings;

        SMPACK_TRY( doc, gltf::Read( gp ) );
        SMPACK_TRY( we, OpenWadEdit( wp ) );

        auto models = FindModels( we.m_W, we.m_In.m_Data, warnings );

        // Which model. Explicit selection, the export extras, then the file name.
        const ModelRef * m = nullptr;

        auto pick = [&] ( auto pred )
        {
            for (const auto & x : models)
                if (pred( x )) return &x;
            return static_cast<const ModelRef *>(nullptr);
        };

        if (auto id = args.Get( "--id" )) m = pick( [&] ( const ModelRef & x )
        {
            return std::to_string( x.m_Mesh ) == *id;
        } );
        else if (auto nm = args.Get( "--name" )) m = pick( [&] ( const ModelRef & x )
        {
            return x.m_Name == *nm || "MESH_" + x.m_Name == *nm;
        } );
        else
        {
            if (!doc.m_SourceModel.empty())
            {
                m = pick( [&] ( const ModelRef & x )
                {
                    return x.m_Name == doc.m_SourceModel && (doc.m_SourceMesh < 0 || static_cast<long long>( x.m_Mesh ) == doc.m_SourceMesh);
                } );

                if (!m) m = pick( [&] ( const ModelRef & x )
                {
                    return x.m_Name == doc.m_SourceModel;
                } );
            }

            if (!m)
            {
                const std::string stem = BaseName( gp.stem().string() );
                m = pick( [&] ( const ModelRef & x )
                {
                    return x.m_Name == stem || std::format( "{}_{}", x.m_Name, x.m_Mesh ) == stem;
                } );
            }

            if (!m)
                for (const auto & n : doc.m_Nodes)
                    if (n.m_Mesh >= 0 && !m)
                        m = pick( [&] ( const ModelRef & x )
                    {
                        return PartFromName( n.m_Name, x.m_Name, x.m_Parts.size() ).has_value();
                    } );
        }

        if (!m)
            return Fail( Errc::NOT_FOUND, "could not tell which model this file replaces: pass '--name' or '--id'" );

        const bool world = args.Has( "--world" ) || doc.m_SourceWorld;

        // '--game' takes the game folder or its 'exec/wad/pc_le', the WAD's own folder is searched too.
        LodLibrary lib;
        if (auto g = args.Get( "--game" ))
        {
            std::error_code ec;
            const fs::path sub = fs::path( *g ) / "exec" / "wad" / "pc_le";

            lib.Open( fs::exists( sub, ec ) ? sub : fs::path( *g ) );
        }

        lib.Open( wp.parent_path() );

        // Gather the imported pieces.
        struct Key
        {
            int m_Part, m_Lod;
            std::uint32_t m_Material;
            auto operator<=>( const Key & ) const = default;
        };

        std::map<Key, NewMesh> pieces;
        std::map<int, std::set<int>> explicit_lods;
        std::map<std::string, std::uint32_t> mat_index;

        for (std::uint32_t i = 0; i < m->m_Materials.size(); ++i)
            mat_index.emplace( m->m_Materials[i], i );

        auto part_skinned = [&] ( std::size_t pi )
        {
            const auto & part = m->m_Parts[pi];

            if (part.m_Lods.empty())
                return false;

            bool any = false;

            for (auto id : part.m_Lods[0].m_PrimIds)
                if (const auto * p = FindPrim( *m, id ); p && p->Visible())
                {
                    if (!p->Has( VType::MATRIX_INDEX ))
                        return false;

                    any = true;
                }

            return any;
        };

        std::size_t used_nodes = 0;
        for (const auto & node : doc.m_Nodes)
        {
            if (node.m_Mesh < 0)
                continue;

            std::optional<std::pair<int, int>> pl;

            if (node.m_ExtraPart >= 0)
                pl = std::pair { node.m_ExtraPart, std::max( 0, node.m_ExtraLod ) };
            else
                pl = PartFromName( node.m_Name, m->m_Name, m->m_Parts.size() );
            if (!pl)
            {
                warnings.push_back( std::format( "node '{}' is not a part of {} (expected '{}_part<N>'), skipped", node.m_Name, m->m_Name, m->m_Name ) );
                continue;
            }

            const auto [part, lod] = *pl;

            if (part < 0 || static_cast<std::size_t>( part ) >= m->m_Parts.size())
                return Fail( Errc::BAD_ARGUMENT, std::format( "node '{}': {} has no part {}", node.m_Name, m->m_Name, part ) );

            if (lod < 0 || static_cast<std::size_t>( lod ) >= m->m_Parts[static_cast<std::size_t>( part )].m_Lods.size())
                return Fail( Errc::BAD_ARGUMENT, std::format( "node '{}': part {} has no LOD {}", node.m_Name, part, lod ) );

            ++used_nodes;

            explicit_lods[part].insert( lod );
            const Mat44 g_inv = mesh::InverseAffine( PartExportMatrix( *m, static_cast<std::size_t>( part ), part_skinned( static_cast<std::size_t>( part ) ), world ) );
            const gltf::Skin * skin = node.m_Skin >= 0 && static_cast<std::size_t>( node.m_Skin ) < doc.m_Skins.size() ? &doc.m_Skins[static_cast<std::size_t>(node.m_Skin)] : nullptr;

            std::vector<std::uint16_t> slot_joint;
            std::vector<Mat44> slot_xf;
            if (skin)
            {
                for (std::size_t s = 0; s < skin->m_Joints.size(); ++s)
                {
                    const auto & jn = doc.m_Nodes[static_cast<std::size_t>( skin->m_Joints[s] )];
                    auto j = JointFromName( jn.m_Name );

                    if (!j || !m->m_Skeleton || *j >= m->m_Skeleton->Size())
                        return Fail( Errc::INCONSISTENT, std::format( "skin joint '{}' does not match the game skeleton (expected 'joint_<index>')", jn.m_Name ) );

                    slot_joint.push_back( *j );
                    slot_xf.push_back( mesh::Mul( skin->m_InverseBind[s], jn.m_World ) );
                }
            }

            for (const auto & gpr : doc.m_Meshes[static_cast<std::size_t>( node.m_Mesh )])
            {
                std::string mname = gpr.m_Material >= 0 && static_cast<std::size_t>(gpr.m_Material) < doc.m_Materials.size()
                    ? BaseName( doc.m_Materials[static_cast<std::size_t>(gpr.m_Material)] ) : std::string();
                std::uint32_t mat = 0;
                if (auto it = mat_index.find( mname ); it != mat_index.end())
                {
                    mat = it->second;
                }
                else
                {
                    // A part drawn with one material accepts any material name.
                    std::set<std::uint32_t> in_part;
                    for (auto id : m->m_Parts[static_cast<std::size_t>(part)].m_Lods[static_cast<std::size_t>(lod)].m_PrimIds)
                        if (const auto * p = FindPrim( *m, id );
                            p && p->Visible() && !m->Helper( p->m_MaterialId )) in_part.insert( p->m_MaterialId );

                    if (in_part.size() != 1)
                        return Fail( Errc::BAD_ARGUMENT, std::format( "node '{}': material '{}' is not one of the model's materials. Keep the MAT_ names from the export",
                            node.m_Name, mname.empty() ? "(none)" : mname ) );

                    mat = *in_part.begin();
                    warnings.push_back( std::format( "node '{}': material '{}' mapped to {}", node.m_Name, mname, m->m_Materials.size() > mat ? m->m_Materials[mat] : "?" ) );
                }

                NewMesh nm;
                const std::size_t nv = gpr.m_Positions.size() / 3;
                const bool skinned_in = skin && gpr.m_Joints.size() == nv * 4;

                nm.m_Pos.resize( nv * 3 );
                nm.m_HasNrm = gpr.m_Normals.size() == nv * 3;

                if (nm.m_HasNrm)
                    nm.m_Nrm.resize( nv * 3 );

                const Mat44 node_n = NormalMatrix( node.m_World );
                const Mat44 gn = NormalMatrix( g_inv );

                for (std::size_t v = 0; v < nv; ++v)
                {
                    Mat44 xf = node.m_World;
                    Mat44 xn = node_n;

                    if (skinned_in)
                    {
                        // Bind pose position. Through the dominant joint's inverse bind and rest transform.
                        std::size_t best = 0;
                        for (std::size_t k = 1; k < 4; ++k)
                            if (gpr.m_Weights[v * 4 + k] > gpr.m_Weights[v * 4 + best])
                                best = k;

                        const std::size_t slot = gpr.m_Joints[v * 4 + best];

                        if (slot < slot_xf.size())
                            xf = slot_xf[slot], xn = NormalMatrix( xf );
                    }

                    auto w = XformPoint( xf, &gpr.m_Positions[v * 3] );
                    auto p = XformPoint( g_inv, w.data() );

                    std::memcpy( &nm.m_Pos[v * 3], p.data(), 12 );

                    if (nm.m_HasNrm)
                    {
                        auto n1 = XformDir( xn, &gpr.m_Normals[v * 3] );
                        auto n2 = XformDir( gn, n1.data() );

                        std::memcpy( &nm.m_Nrm[v * 3], n2.data(), 12 );
                    }
                }

                for (std::size_t k = 0; k < 4; ++k)
                {
                    nm.m_HasUv[k] = gpr.m_Uvs[k].size() == nv * 2;

                    if (nm.m_HasUv[k])
                        nm.m_Uv[k] = gpr.m_Uvs[k];
                }

                nm.m_HasCol = gpr.m_Colors.size() == nv * 4;

                if (nm.m_HasCol)
                    nm.m_Col = gpr.m_Colors;

                if (skinned_in)
                {
                    nm.m_HasSkin = true;
                    nm.m_Joints.resize( nv * 4 );
                    nm.m_Weights = gpr.m_Weights;

                    for (std::size_t i = 0; i < nv * 4; ++i)
                    {
                        const std::size_t slot = gpr.m_Joints[i];
                        nm.m_Joints[i] = slot < slot_joint.size() ? slot_joint[slot] : 0;

                        if (slot >= slot_joint.size())
                            nm.m_Weights[i] = 0;
                    }
                }

                // Counter clockwise glTF triangles to clockwise game triangles.
                nm.m_Idx.reserve( gpr.m_Indices.size() );

                for (std::size_t t = 0; t + 2 < gpr.m_Indices.size(); t += 3)
                {
                    nm.m_Idx.push_back( gpr.m_Indices[t] );
                    nm.m_Idx.push_back( gpr.m_Indices[t + 2] );
                    nm.m_Idx.push_back( gpr.m_Indices[t + 1] );
                }

                nm.m_EmptyFlags = false;
                pieces[Key { part, lod, mat }].Append( nm );
            }
        }

        if (used_nodes == 0)
            return Fail( Errc::NOT_FOUND, std::format( "no node in the file matches a part of {}", m->m_Name ) );

        // Original data.
        ByteSpan gpu;

        if (m->m_Gpu)
            gpu = we.m_Payloads[*m->m_Gpu].m_Data;

        std::map<std::uint64_t, Bytes> blocks;
        std::map<std::uint16_t, std::shared_ptr<mesh::Geometry>> orig_geo;

        auto source_of = [&] ( const mesh::Prim & p ) -> Result<ByteSpan>
        {
            if (!p.Streamed())
                return gpu;

            auto it = blocks.find( p.m_StreamHash );

            if (it == blocks.end())
            {
                auto b = lib.Read( p.m_StreamHash );

                if (!b)
                    return Fail( Errc::NOT_FOUND, std::format( "{} prim {}: its geometry block {:016x} is in a .lodpack that was not found "
                        "(pass '--game <game dir>')",
                        m->m_Name, p.m_PrimId, p.m_StreamHash ) );
                it = blocks.emplace( p.m_StreamHash, std::move( *b ) ).first;
            }

            return ByteSpan( it->second );
        };

        auto geo_of = [&] ( const mesh::Prim & p ) -> const mesh::Geometry *
        {
            if (auto it = orig_geo.find( p.m_PrimId ); it != orig_geo.end())
                return it->second.get();

            auto src = source_of( p );
            std::shared_ptr<mesh::Geometry> g;

            if (src)
                if (auto d = mesh::Decode( p, *src ))
                    g = std::make_shared<mesh::Geometry>( std::move( *d ) );

            orig_geo[p.m_PrimId] = g;
            return g.get();
        };

        // Encode.
        std::map<std::uint16_t, Encoded> encoded; // By prim ID.
        std::map<int, std::array<float, 6>> part_bounds; // New LOD 0 bounds per part [min, max].

        for (const auto & [part, levels] : explicit_lods)
        {
            const auto & part_info = m->m_Parts[static_cast<std::size_t>(part)];
            const int base_level = *levels.begin();

            for (std::size_t l = 0; l < part_info.m_Lods.size(); ++l)
            {
                const bool expl = levels.count( static_cast<int>( l ) ) != 0;

                if (!expl && lods_mode == "same")
                    continue;

                const int src_level = expl ? static_cast<int>( l ) : base_level;

                // Every imported piece of this level, for shadow proxies.
                NewMesh all;
                std::set<std::uint32_t> level_mats;
                for (const auto & [k, nm] : pieces)
                    if (k.m_Part == part && k.m_Lod == src_level)
                        all.Append( nm ), level_mats.insert( k.m_Material );

                std::set<std::uint32_t> filled;
                bool level_changed = false; // A visible primitive of this level gets new geometry.

                // Visible primitives first. A shadow proxy is only rebuilt when the level it shadows changed.
                for (const bool proxies : { false, true })
                    for (auto id : part_info.m_Lods[l].m_PrimIds)
                    {
                        const auto * p = FindPrim( *m, id );

                        if (!p || encoded.count( p->m_PrimId ) || m->Helper( p->m_MaterialId ) || p->Visible() == proxies)
                            continue;

                        NewMesh src;
                        if (!p->Visible())
                        {
                            if (!level_changed || !p->Has( VType::POSITION ))
                                continue;

                            src = all;
                        }
                        else if (!filled.count( p->m_MaterialId ))
                        {
                            if (auto it = pieces.find( Key { part, src_level, p->m_MaterialId } ); it != pieces.end())
                                src = it->second;
                            else if (l == static_cast<std::size_t>(src_level))
                                warnings.push_back( std::format( "part {}: nothing uses {} any more, its primitive is emptied", part,
                                    p->m_MaterialId < m->m_Materials.size() ? m->m_Materials[p->m_MaterialId] : std::to_string( p->m_MaterialId ) ) );

                            filled.insert( p->m_MaterialId );
                        }

                        auto srcr = source_of( *p );

                        if (!srcr)
                        {
                            // A coarser level the file does not describe can stay as it is.
                            if (!expl)
                            {
                                warnings.push_back( std::format( "part {} LOD {} kept: {}", part, l, srcr.error().m_Message ) );
                                continue;
                            }

                            return std::unexpected( srcr.error() );
                        }

                        // Geometry the file did not change keeps its original bytes.
                        if (p->Visible() && SameGeometry( src, geo_of( *p ) ))
                            continue;

                        if (p->Visible())
                            level_changed = true;

                        const ByteSpan srcb = *srcr;
                        Encoder enc( *p, srcb, geo_of( *p ), m->m_Skeleton ? &*m->m_Skeleton : nullptr, warnings, std::format( "{} part {} prim {}", m->m_Name, part, p->m_PrimId ) );

                        SMPACK_TRY( e, enc.Run( std::move( src ) ) );

                        if (l == 0 && p->Visible() && e.m_Triangles > 1)
                        {
                            auto & b = part_bounds.try_emplace( part, std::array<float, 6>{1e30f, 1e30f, 1e30f, -1e30f, -1e30f, -1e30f} ).first->second;

                            for (int k = 0; k < 3; ++k)
                                b[k] = std::min( b[k], e.m_Aabb[3 + k] - e.m_Aabb[k] ), b[3 + k] = std::max( b[3 + k], e.m_Aabb[3 + k] + e.m_Aabb[k] );
                        }

                        encoded.emplace( p->m_PrimId, std::move( e ) );
                    }

                for (auto mat : level_mats)
                {
                    bool found = false;

                    for (auto id : part_info.m_Lods[l].m_PrimIds)
                        if (const auto * p = FindPrim( *m, id ); p && p->Visible() && p->m_MaterialId == mat)
                            found = true;

                    if (!found && expl)
                        warnings.push_back( std::format( "part {} LOD {}: {} is not used by this part in the game. That geometry was not imported", part, l,
                            mat < m->m_Materials.size() ? m->m_Materials[mat] : std::to_string( mat ) ) );
                }
            }
        }

        // Pose space deformers of primitives that lost their vertex count or order.
        if (m->m_Group)
        {
            Bytes & mg = we.m_Payloads[*m->m_Group].m_Data;
            std::set<std::uint16_t> drop;

            for (auto id : PsdPrims( mg ))
                if (auto it = encoded.find( id ); it != encoded.end() && !it->second.m_SameOrder)
                {
                    drop.insert( id );
                    warnings.push_back( std::format( "{} prim {}: the new mesh has other vertices than the original, so its pose space corrective shapes "
                        "(facial and muscle correctives) are turned off", m->m_Name, id ) );
                }

            SMPACK_TRY( n_dropped, DropPsdPrims( mg, drop ) );
            (void)n_dropped;
        }

        // Write buffers and patch the prim table.
        Bytes & mesh_chunk = we.m_Payloads[m->m_Mesh].m_Data;
        Bytes new_gpu( gpu.begin(), gpu.end() );
        std::map<std::uint64_t, Bytes> new_blocks;

        auto put32 = [&] ( std::uint64_t off, std::uint32_t v )
        {
            std::memcpy( mesh_chunk.data() + off, &v, 4 );
        };

        for (auto & [id, e] : encoded)
        {
            const auto * p = FindPrim( *m, id );
            Bytes * buf = nullptr;

            if (p->Streamed())
            {
                auto it = new_blocks.find( p->m_StreamHash );

                if (it == new_blocks.end())
                    it = new_blocks.emplace( p->m_StreamHash, blocks.at( p->m_StreamHash ) ).first;

                buf = &it->second;
            }
            else
            {
                buf = &new_gpu;
            }

            const std::size_t align = p->Streamed() ? 16 : 256;

            auto append = [&] ( const Bytes & d )
            {
                while (buf->size() % align)
                    buf->push_back( std::byte { 0 } );

                const auto off = buf->size();
                buf->insert( buf->end(), d.begin(), d.end() );
                return static_cast<std::uint32_t>(off);
            };

            if (buf->size() > 0xFFFFFFFFull - 0x10000000ull)
                return Fail( Errc::UNSUPPORTED, "geometry buffer would exceed 4 GB" );

            for (const auto & [slot, data] : e.m_Streams)
            {
                const std::uint32_t off = append( data );
                put32( p->m_StreamOffsetsPos + 4ull * slot, off );

                if (slot == 0)
                    put32( p->m_ParmOffset + 0x3C, off );
            }

            const std::uint32_t ioff = append( e.m_Indices );
            put32( p->m_ParmOffset + 0x30, ioff );

            if (p->m_Meshlets)
            {
                // Info array, then the culling array on the next 16 byte boundary.
                Bytes blob = e.m_MeshletInfo;
                while (blob.size() % 16)
                    blob.push_back( std::byte { 0 } );

                blob.insert( blob.end(), e.m_MeshletCull.begin(), e.m_MeshletCull.end() );

                const std::uint32_t moff = append( blob );
                const auto cnt = static_cast<std::uint16_t>(std::min<std::uint32_t>( e.m_MeshletCount, 0xFFFF ));

                put32( p->m_ParmOffset + 0x34, moff );
                std::memcpy( mesh_chunk.data() + p->m_ParmOffset + 0x52, &cnt, 2 );
                put32( p->m_ParmOffset + 0x54, e.m_MeshletMaxIndices );
            }

            put32( p->m_ParmOffset + 0x44, e.m_Vertices );
            put32( p->m_ParmOffset + 0x48, e.m_Triangles );
            mesh_chunk[p->m_ParmOffset + 0x81] = static_cast<std::byte>(e.m_IndexStride);

            std::memcpy( mesh_chunk.data() + p->m_ParmOffset + 0x10, e.m_Aabb, 24 );

            if (e.m_MipValid)
            {
                float mip[4];
                std::memcpy( mip, mesh_chunk.data() + p->m_ParmOffset, 16 );

                for (int k = 0; k < 4; ++k)
                    if (std::isfinite( mip[k] ) && mip[k] < 1e30f)
                        mip[k] *= e.m_Mip[static_cast<std::size_t>(k)];

                std::memcpy( mesh_chunk.data() + p->m_ParmOffset, mip, 16 );
            }
        }

        if (m->m_Gpu && new_gpu.size() != gpu.size())
            we.m_Payloads[*m->m_Gpu].m_Data = std::move( new_gpu );

        // Grow the culling volumes of the parts and the model so new geometry is not culled.
        if (m->m_Group && !part_bounds.empty())
        {
            Bytes & mg = we.m_Payloads[*m->m_Group].m_Data;

            ByteReader r( mg );
            SMPACK_TRY( np, r.At<std::uint16_t>( 0x30 ) );
            SMPACK_TRY( offs, r.Array<std::uint32_t>( 0x44, np ) );

            float sphere[4];
            std::memcpy( sphere, mg.data() + 0x20, 16 );

            for (const auto & [part, b] : part_bounds)
            {
                if (static_cast<std::size_t>(part) >= offs.size())
                    continue;

                const std::uint64_t ob = offs[static_cast<std::size_t>(part)] + 4ull;

                if (ob + 48 > mg.size())
                    continue;

                float o[12];
                std::memcpy( o, mg.data() + ob, 48 );

                // Corners of the stored box [axes scaled by the half extents, then the center].
                float mn[3] = { b[0], b[1], b[2] }, mx[3] = { b[3], b[4], b[5] };
                for (int c = 0; c < 8; ++c)
                {
                    float p[3];
                    for (int k = 0; k < 3; ++k)
                        p[k] = o[9 + k] + (c & 1 ? 1.0f : -1.0f) *
                        o[k] + (c & 2 ? 1.0f : -1.0f) *
                        o[3 + k] + (c & 4 ? 1.0f : -1.0f) *
                        o[6 + k];

                    for (int k = 0; k < 3; ++k)
                        mn[k] = std::min( mn[k], p[k] ),
                        mx[k] = std::max( mx[k], p[k] );
                }

                float nb[12] = { (mx[0] - mn[0]) * 0.5f, 0, 0, 0, (mx[1] - mn[1]) * 0.5f, 0, 0, 0, (mx[2] - mn[2]) * 0.5f,
                    (mx[0] + mn[0]) * 0.5f, (mx[1] + mn[1]) * 0.5f, (mx[2] + mn[2]) * 0.5f };

                std::memcpy( mg.data() + ob, nb, 48 );

                // Model cull sphere in model space.
                const Mat44 g = PartExportMatrix( *m, static_cast<std::size_t>( part ), part_skinned( static_cast<std::size_t>( part ) ), true );

                for (int c = 0; c < 8; ++c)
                {
                    const float lp[3] = { c & 1 ? b[3] : b[0], c & 2 ? b[4] : b[1], c & 4 ? b[5] : b[2] };
                    const auto wp2 = XformPoint( g, lp );
                    const float d = std::sqrt( (wp2[0] - sphere[1]) * (wp2[0] - sphere[1]) + (wp2[1] - sphere[2]) * (wp2[1] - sphere[2]) +
                        (wp2[2] - sphere[3]) * (wp2[2] - sphere[3]) );
                    sphere[0] = std::max( sphere[0], d );
                }
            }

            std::memcpy( mg.data() + 0x20, sphere, 16 );
        }
        else if (m->m_StaticData && !part_bounds.empty())
        {
            warnings.push_back( "static props keep their original culling boxes. Keep the new mesh inside the original bounds" );
        }

        // Streamed geometry cannot override a base block by hash.
        // Edited blocks get new hashes instead. A hash missing from the map is looked up in every pack the WAD
        // is registered with, and the WAD's lod stream table registers it with the patch pack.
        std::map<std::uint64_t, std::uint64_t> rehash; // Old block hash becomes new.
        std::set<std::uint64_t> taken; // New block hashes.
        if (!new_blocks.empty())
        {
            for (const auto & [h, data] : new_blocks)
            {
                const std::uint64_t nh = NewBlockHash( h, data, lib, taken );

                taken.insert( nh );
                rehash.emplace( h, nh );
            }

            // Every primitive of the WAD that streams from an edited block, in any model. Untouched ones
            // keep their offsets, the new block starts with the original bytes.
            for (const auto & x : models)
            {
                Bytes & mc = we.m_Payloads[x.m_Mesh].m_Data;

                for (const auto & p : x.m_Prims)
                    if (auto it = rehash.find( p.m_StreamHash ); it != rehash.end())
                        std::memcpy( mc.data() + p.m_ParmOffset + 0x68, &it->second, 8 );
            }

            SMPACK_TRYV( RehashWadLodToc( we, rehash, new_blocks ) );
        }

        // Outputs.
        const fs::path out_dir = args.GetOr( "--output", "." );
        const fs::path wad_out = out_dir / GameName( wp );

        std::vector<fs::path> outputs;
        std::optional<std::string> patch;

        if (!new_blocks.empty())
        {
            patch = args.GetOr( "--patch", "smpack_meshpatch" );

            if (patch->size() > bootopts::MAX_NAME_LEN)
                return Fail( Errc::BAD_ARGUMENT, "--patch name must be at most 31 characters" );

            const fs::path pp = out_dir / (*patch + ".lodpack"), tp = out_dir / (*patch + ".lodpack.toc");

            // Groups of an earlier patch [other models of the same mod] are kept. A block replaced again
            // by this run is dropped from them.
            std::vector<lodpack::NewGroup> groups;
            auto superseded = [&] ( std::uint64_t h )
            {
                return rehash.count( h ) != 0 || taken.count( h ) != 0;
            };

            std::error_code ec;
            if (args.Has( "--append" ) && fs::exists( pp, ec ))
            {
                SMPACK_TRY( old, OpenLodpack( pp ) );

                for (std::size_t gi = 0; gi < old.m_Pack.m_Groups.size(); ++gi)
                {
                    lodpack::NewGroup ng;
                    ng.m_Entry = old.m_Pack.m_Groups[gi];

                    SMPACK_TRY( gb, lodpack::GroupBytes( old.m_In.m_Data, ng.m_Entry ) );

                    ng.m_Data.assign( gb.begin(), gb.end() );

                    for (const auto & b : old.m_Pack.m_Blocks)
                        if (b.m_GroupIdx == gi && !superseded( b.m_BlockDataHash )) ng.m_Blocks.push_back( b );

                    if (!ng.m_Blocks.empty())
                        groups.push_back( std::move( ng ) );
                }
            }

            // One patch group per source group, holding only the new blocks, so the game streams the
            // same units as before without a copy of the untouched blocks.
            std::map<std::uint64_t, lodpack::NewGroup> by_group; // Source group hash as new group.
            for (const auto & [h, data] : new_blocks)
            {
                const auto info = lib.Info( h );
                if (!info)
                    return Fail( Errc::NOT_FOUND, std::format( "lodpack block {:#018x} not found", h ) );

                auto [it, fresh] = by_group.try_emplace( info->m_Group.m_GroupDataHash );
                lodpack::NewGroup & ng = it->second;

                if (fresh)
                    ng.m_Entry = info->m_Group;

                while (ng.m_Data.size() % 16)
                    ng.m_Data.push_back( std::byte { 0 } );

                lodpack::BlockEntry b = info->m_Block;
                b.m_BlockDataHash = rehash.at( h );
                b.m_GroupOffset = static_cast<std::uint32_t>(ng.m_Data.size());
                b.m_BlockDataSize = static_cast<std::uint32_t>(data.size());
                ng.m_Data.insert( ng.m_Data.end(), data.begin(), data.end() );
                ng.m_Blocks.push_back( b );
            }

            for (auto & [src_hash, ng] : by_group)
            {
                while (ng.m_Data.size() % 16)
                    ng.m_Data.push_back( std::byte { 0 } );

                // A group hash of its own. The base group with this hash is a different set of bytes.
                std::vector<std::uint64_t> ids { src_hash };
                for (const auto & b : ng.m_Blocks)
                    ids.push_back( b.m_BlockDataHash );

                ng.m_Entry.m_GroupDataHash = Fnv1a64( ids.data(), ids.size() * 8 );
                groups.push_back( std::move( ng ) );
            }

            SMPACK_TRY( built, lodpack::Build( std::move( groups ), 1u, 1 ) );

            // The WAD and patch may sit in the same folder the inputs came from. Release the inputs first.
            SMPACK_TRYV( io::WriteFile( pp, built.m_Pack ) );
            SMPACK_TRYV( WriteMaybeLz4( tp, built.m_Toc, !args.Has( "--no-compress" ), 9 ) );

            outputs.push_back( pp );
            outputs.push_back( tp );
        }
        {
            SMPACK_TRY( bytes, wad::Build( we.m_W.m_Header, we.m_W.m_Entries, we.m_Payloads ) );
            we.m_In = Input {}; // Unmap. The output may overwrite the input WAD.

            SMPACK_TRYV( WriteMaybeLz4( wad_out, bytes, !args.Has( "--no-compress" ), args.GetInt( "--level", 0 ) ) );
            outputs.push_back( wad_out );
        }

        if (auto game = args.Get( "--install" ))
            SMPACK_TRYV( InstallFiles( *game, outputs, std::nullopt, patch ) );

        std::uint64_t verts = 0, tris = 0;
        for (const auto & [id, e] : encoded)
            verts += e.m_Vertices, tris += e.m_Triangles;

        if (js)
        {
            json::Value d = json::Value::MakeObject();

            d["smpack"] = "meshimport";
            d["model"] = m->m_Name;
            d["id"] = m->m_Mesh;
            d["prims"] = encoded.size();
            d["vertices"] = verts;
            d["triangles"] = tris;
            d["lodpack"] = patch ? json::Value( *patch ) : json::Value( nullptr );

            json::Value fl = json::Value::MakeArray();

            for (const auto & f : outputs)
                fl.Push( f.string() );

            d["files"] = fl;

            json::Value ws = json::Value::MakeArray();

            for (const auto & s : warnings)
                ws.Push( s );

            d["warnings"] = ws;

            print( "{}", json::Dump( d ) );
        }
        else
        {
            for (const auto & s : warnings)
                println( stderr, "  ! {}", s );

            println( "{}: {} primitive(s) replaced ({} vertices, {} triangles)", m->m_Name, encoded.size(), verts, tris );

            for (const auto & f : outputs)
                println( "wrote {}", f.string() );

            if (patch && !args.Get( "--install" ))
                println( "\nTo activate, copy the files into 'exec/wad/pc_le' and add \"{}\" to \"patch-lodpacks\" in exec/boot-options.json "
                    "(or run again with '--install <game dir>').",
                    *patch );
        }

        return {};
    }
}