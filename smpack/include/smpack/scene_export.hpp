// Neutral scene description plus glTF 2.0 (.gltf and .bin or .glb) and
// binary FBX 7.4 writers. Y up, metres, right handed, triangle winding is
// converted from the game's clockwise front faces to counter clockwise.

#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "smpack/io.hpp"
#include "smpack/json.hpp"
#include "smpack/mesh.hpp"

namespace smpack::scene
{
    namespace fs = std::filesystem;

    struct Submesh
    {
        std::shared_ptr<const mesh::Geometry> m_Geo;
        std::uint32_t m_Material = 0;
        int m_Prim = -1; // Flat prim ID in the game model.
    };

    struct Node
    {
        std::string m_Name;
        std::optional<std::array<float, 16>> m_Matrix; // Row major, row vector convention.
        std::vector<Submesh> m_Subs;
        int m_Joint = -1; // Rigid part attached to this skeleton joint [geometry in joint space].
        int m_Part = -1; // Model part index.
        int m_Lod = 0;

        [[nodiscard]] bool Skinned() const noexcept
        {
            if (m_Subs.empty())
                return false;

            for (const auto & s : m_Subs)
                if (s.m_Geo->m_Joints.empty())
                    return false;

            return true;
        }
    };

    enum class AlphaMode
    {
        ALPHA_OPAQUE,
        ALPHA_MASK
    };

    struct Material
    {
        std::string m_Name;
        std::string m_BaseColor; // Image path relative to the output file [empty means none].
        int m_BaseUv = 0; // UV set the base color map uses.
        bool m_Overlay = false; // No color of its own [eye wetness, shadow cards, effect shells].
        AlphaMode m_Alpha = AlphaMode::ALPHA_OPAQUE; // Mask. Cutout in the base color alpha [hair, lashes, decals].
        std::string m_Normal;
    };

    // Where a scene came from, written as glTF extras so an edited file can be matched back.
    struct Source
    {
        std::string m_Wad;
        std::size_t m_Mesh = 0; // WAD entry index of 'MESH_<name>'.
        bool m_World = false;
    };

    struct Scene
    {
        std::string m_Name;
        std::vector<Node> m_Nodes;
        std::vector<Material> m_Materials;
        std::optional<mesh::Skeleton> m_Skeleton;
        Source m_Source;

        [[nodiscard]] static std::string JointName(
            std::size_t i
        )
        {
            return std::format( "joint_{:03}", i );
        }
    };

    namespace detail
    {
        [[nodiscard]] inline bool IsIdentity(
            const mesh::Mat44 & m
        ) noexcept
        {
            for (int i = 0; i < 16; ++i)
                if (std::abs( m[i] - ((i % 5 == 0) ? 1.0f : 0.0f) ) > 1e-6f)
                    return false;

            return true;
        }
    }

    // glTF.

    namespace detail
    {
        inline void PutFloats( 
            ByteWriter & w, 
            const std::vector<float> & v
        )
        {
            w.Put( ByteSpan( reinterpret_cast<const std::byte *>(v.data()), v.size() * 4 ) );
        }
    }

    /// Write '<path>' as .gltf [with a sibling .bin] or as .glb, by extension.
    [[nodiscard]] inline Result<void> WriteGltf( const Scene & sc, const fs::path & path )
    {
        const bool glb = path.extension() == ".glb";
        ByteWriter bin;

        json::Value views = json::Value::MakeArray(), 
            accs = json::Value::MakeArray(),
            meshes = json::Value::MakeArray(),
            nodes = json::Value::MakeArray();

        auto add_view = [&] ( std::size_t off, std::size_t len, std::optional<int> target )
        {
            json::Value v = json::Value::MakeObject();

            v["buffer"] = 0;
            v["byteOffset"] = off;
            v["byteLength"] = len;

            if (target)
                v["target"] = *target;

            views.Push( std::move( v ) );

            return views.AsArray().size() - 1;
        };

        auto add_acc = [&] ( std::size_t view, int ctype, std::size_t count, std::string_view type )
        {
            json::Value a = json::Value::MakeObject();

            a["bufferView"] = view;
            a["componentType"] = ctype;
            a["count"] = count;
            a["type"] = type;
            accs.Push( std::move( a ) );

            return accs.AsArray().size() - 1;
        };

        auto float_attr = [&] ( const std::vector<float> & v, std::uint32_t comps, std::string_view type, bool minmax )
        {
            bin.Align( 4 );

            const std::size_t off = bin.Size();
            detail::PutFloats( bin, v );
            const auto acc = add_acc( add_view( off, v.size() * 4, 34962 ), 5126, v.size() / comps, type );

            if (minmax && !v.empty())
            {
                json::Value mn = json::Value::MakeArray(), mx = json::Value::MakeArray();

                for (std::uint32_t c = 0; c < comps; ++c)
                {
                    float lo = v[c], hi = v[c];

                    for (std::size_t i = c; i < v.size(); i += comps)
                        lo = std::min( lo, v[i] ), hi = std::max( hi, v[i] );

                    mn.Push( static_cast<double>( lo ) );
                    mx.Push( static_cast<double>( hi ) );
                }

                accs.AsArray().back()["min"] = mn;
                accs.AsArray().back()["max"] = mx;
            }

            return acc;
        };

        json::Value scene_nodes = json::Value::MakeArray();

        const std::size_t nj = sc.m_Skeleton ? sc.m_Skeleton->Size() : 0;
        std::vector<json::Value> joint_children( nj ); // Values share storage when copied. Build each one.

        for (auto & c : joint_children)
            c = json::Value::MakeArray();

        for (std::size_t j = 0; j < nj; ++j)
        {
            json::Value nd = json::Value::MakeObject();
            nd["name"] = Scene::JointName( j );

            if (!detail::IsIdentity( sc.m_Skeleton->m_Local[j] ))
            {
                json::Value mm = json::Value::MakeArray();

                for (float f : sc.m_Skeleton->m_Local[j])
                    mm.Push( static_cast<double>( f ) );

                nd["matrix"] = mm;
            }

            nodes.Push( std::move( nd ) );
            const int par = sc.m_Skeleton->m_Parents[j];

            if (par < 0)
                scene_nodes.Push( j );
            else
                joint_children[static_cast<std::size_t>( par )].Push( j );
        }

        bool any_skin = false;
        for (const auto & n : sc.m_Nodes)
        {
            json::Value prims = json::Value::MakeArray();

            for (const auto & s : n.m_Subs)
            {
                const auto & g = *s.m_Geo;
                json::Value attrs = json::Value::MakeObject();

                attrs["POSITION"] = float_attr( g.m_Positions, 3, "VEC3", true );

                if (!g.m_Normals.empty())
                    attrs["NORMAL"] = float_attr( g.m_Normals, 3, "VEC3", false );

                for (std::size_t k = 0; k < g.m_Uvs.size(); ++k)
                    if (!g.m_Uvs[k].empty())
                        attrs[std::format( "TEXCOORD_{}", k )] = float_attr( g.m_Uvs[k], 2, "VEC2", false );

                if (!g.m_Colors.empty())
                    attrs["COLOR_0"] = float_attr( g.m_Colors, 4, "VEC4", false );

                if (nj > 0 && n.Skinned())
                {
                    bin.Align( 4 );

                    const std::size_t joff = bin.Size();
                    for (auto v : g.m_Joints)
                        bin.Put( static_cast<std::uint16_t>(v < nj ? v : 0) );

                    attrs["JOINTS_0"] = add_acc( add_view( joff, g.m_Joints.size() * 2, 34962 ), 5123, g.m_Joints.size() / 4, "VEC4" );
                    attrs["WEIGHTS_0"] = float_attr( g.m_Weights, 4, "VEC4", false );
                }

                bin.Align( 4 );

                const std::size_t off = bin.Size();
                for (std::size_t t = 0; t + 2 < g.m_Indices.size(); t += 3)
                {
                    bin.Put( g.m_Indices[t] );
                    bin.Put( g.m_Indices[t + 2] );
                    bin.Put( g.m_Indices[t + 1] );
                }

                json::Value p = json::Value::MakeObject();

                p["attributes"] = attrs;
                p["indices"] = add_acc( add_view( off, g.m_Indices.size() * 4, 34963 ), 5125, g.m_Indices.size(), "SCALAR" );
                p["material"] = s.m_Material;

                if (s.m_Prim >= 0)
                {
                    json::Value ex = json::Value::MakeObject();

                    ex["prim"] = s.m_Prim;
                    p["extras"] = ex;
                }

                prims.Push( std::move( p ) );
            }

            json::Value nd = json::Value::MakeObject();

            nd["name"] = n.m_Name;

            if (n.m_Part >= 0)
            {
                json::Value ex = json::Value::MakeObject();

                ex["part"] = n.m_Part;
                ex["lod"] = n.m_Lod;
                nd["extras"] = ex;
            }

            if (!n.m_Subs.empty())
            {
                json::Value m = json::Value::MakeObject();

                m["name"] = n.m_Name;
                m["primitives"] = prims;

                meshes.Push( std::move( m ) );
                nd["mesh"] = meshes.AsArray().size() - 1;
            }

            if (n.m_Matrix)
            {
                // Row vector row major storage equals the column major storage of the column vector matrix.
                json::Value mm = json::Value::MakeArray();

                for (float f : *n.m_Matrix)
                    mm.Push( static_cast<double>(f) );

                nd["matrix"] = mm;
            }

            if (nj > 0 && n.Skinned())
            {
                nd["skin"] = 0;
                any_skin = true;
            }

            nodes.Push( std::move( nd ) );

            if (n.m_Joint >= 0 && static_cast<std::size_t>(n.m_Joint) < nj)
                joint_children[static_cast<std::size_t>(n.m_Joint)].Push( nodes.AsArray().size() - 1 );
            else
                scene_nodes.Push( nodes.AsArray().size() - 1 );
        }

        for (std::size_t j = 0; j < nj; ++j)
            if (!joint_children[j].AsArray().empty()) nodes.AsArray()[j]["children"] = joint_children[j];

        json::Value skins = json::Value::MakeArray();

        if (any_skin)
        {
            bin.Align( 4 );

            const std::size_t off = bin.Size();

            for (std::size_t j = 0; j < nj; ++j)
                for (float f : sc.m_Skeleton->m_InvBind[j])
                    bin.Put( f );

            json::Value sk = json::Value::MakeObject();

            sk["inverseBindMatrices"] = add_acc( add_view( off, nj * 64, std::nullopt ), 5126, nj, "MAT4" );

            json::Value js = json::Value::MakeArray();

            for (std::size_t j = 0; j < nj; ++j)
                js.Push( j );

            sk["joints"] = js;
            sk["name"] = "skeleton";

            skins.Push( std::move( sk ) );
        }

        json::Value mats = json::Value::MakeArray(),
            textures = json::Value::MakeArray(), 
            images = json::Value::MakeArray();

        auto image_index = [&] ( const std::string & uri )
        {
            for (std::size_t i = 0; i < images.AsArray().size(); ++i)
                if (images.AsArray()[i].Get( "uri" ).AsString() == uri)
                    return i;

            json::Value im = json::Value::MakeObject();

            im["uri"] = uri;
            images.Push( std::move( im ) );

            json::Value t = json::Value::MakeObject();

            t["source"] = images.AsArray().size() - 1;
            textures.Push( std::move( t ) );

            return images.AsArray().size() - 1;
        };

        for (const auto & m : sc.m_Materials)
        {
            json::Value j = json::Value::MakeObject();
            j["name"] = m.m_Name;
            json::Value pbr = json::Value::MakeObject();

            if (!m.m_BaseColor.empty())
            {
                json::Value t = json::Value::MakeObject();
                t["index"] = image_index( m.m_BaseColor );

                if (m.m_BaseUv > 0)
                    t["texCoord"] = m.m_BaseUv;

                pbr["baseColorTexture"] = t;
            }

            if (m.m_Overlay)
            {
                json::Value f = json::Value::MakeArray();

                for (double v : {1.0, 1.0, 1.0, 0.15})
                    f.Push( v );

                pbr["baseColorFactor"] = f;
                j["alphaMode"] = "BLEND";
            }
            else if (m.m_Alpha == AlphaMode::ALPHA_MASK)
            {
                j["alphaMode"] = "MASK";
                j["alphaCutoff"] = 0.5;
                j["doubleSided"] = true;
            }

            pbr["metallicFactor"] = 0.0;
            pbr["roughnessFactor"] = 0.8;
            j["pbrMetallicRoughness"] = pbr;

            if (!m.m_Normal.empty())
            {
                json::Value t = json::Value::MakeObject();

                t["index"] = image_index( m.m_Normal );
                j["normalTexture"] = t;
            }

            mats.Push( std::move( j ) );
        }

        json::Value doc = json::Value::MakeObject();
        json::Value asset = json::Value::MakeObject();

        asset["version"] = "2.0";
        asset["generator"] = "smpack";
        doc["asset"] = asset;
        doc["scene"] = 0;

        json::Value scn = json::Value::MakeObject();

        scn["name"] = sc.m_Name;
        scn["nodes"] = scene_nodes;

        if (!sc.m_Source.m_Wad.empty())
        {
            json::Value ex = json::Value::MakeObject(), src = json::Value::MakeObject();

            src["wad"] = sc.m_Source.m_Wad;
            src["mesh"] = sc.m_Source.m_Mesh;
            src["model"] = sc.m_Name;
            src["world"] = sc.m_Source.m_World;
            ex["smpack"] = src;
            scn["extras"] = ex;
        }

        json::Value scenes = json::Value::MakeArray();

        scenes.Push( scn );
        doc["scenes"] = scenes;
        doc["nodes"] = nodes;
        doc["meshes"] = meshes;

        if (!skins.AsArray().empty())
            doc["skins"] = skins;

        if (!mats.AsArray().empty())
            doc["materials"] = mats;

        if (!textures.AsArray().empty())
        {
            doc["textures"] = textures;
            doc["images"] = images;
        }

        doc["accessors"] = accs;
        doc["bufferViews"] = views;

        bin.Align( 4 );

        json::Value buf = json::Value::MakeObject();

        buf["byteLength"] = bin.Size();

        if (!glb)
            buf["uri"] = path.stem().string() + ".bin";

        json::Value bufs = json::Value::MakeArray();

        bufs.Push( buf );
        doc["buffers"] = bufs;

        if (!glb)
        {
            SMPACK_TRYV( io::WriteText( path, json::Dump( doc ) ) );
            return io::WriteFile( fs::path( path ).replace_extension( ".bin" ), bin.Buf() );
        }

        std::string js = json::Dump( doc, -1 );

        while (js.size() % 4)
            js.push_back( ' ' );

        const Bytes body = bin.Take();

        ByteWriter out;
        out.Put( std::uint32_t { 0x46546C67 } );
        out.Put( std::uint32_t { 2 } );
        out.Put( static_cast<std::uint32_t>(12 + 8 + js.size() + 8 + body.size()) );
        out.Put( static_cast<std::uint32_t>(js.size()) );
        out.Put( std::uint32_t { 0x4E4F534A } );
        out.Put( AsBytes( js ) );
        out.Put( static_cast<std::uint32_t>(body.size()) );
        out.Put( std::uint32_t { 0x004E4942 } );
        out.Put( ByteSpan( body ) );

        return io::WriteFile( path, out.Buf() );
    }

    // FBX [binary 7.4].

    namespace fbx
    {
        struct Prop
        {
            char m_Type;
            Bytes m_Data;
        };

        struct NodeRec
        {
            std::string m_Name;
            std::vector<Prop> m_Props;
            std::vector<NodeRec> m_Children;

            NodeRec & Add( std::string n )
            {
                m_Children.push_back( NodeRec { std::move( n ), {}, {} } );
                return m_Children.back();
            }


            template <class T>
            NodeRec & Raw(
                char t,
                const T & v
            )
            {
                Prop p { t, {} };
                p.m_Data.resize( sizeof( T ) );
                std::memcpy( p.m_Data.data(), &v, sizeof( T ) );
                m_Props.push_back( std::move( p ) );
                return *this;
            }


            NodeRec & I32(
                std::int32_t v
            )
            {
                return Raw( 'I', v );
            }


            NodeRec & I64(
                std::int64_t v
            )
            {
                return Raw( 'L', v );
            }


            NodeRec & F64(
                double v
            )
            {
                return Raw( 'D', v );
            }


            NodeRec & Chr(
                char v
            )
            {
                return Raw( 'C', v );
            }


            NodeRec & Str(
                std::string_view s,
                char t = 'S'
            )
            {
                Prop p { t, {} };
                const auto n = static_cast<std::uint32_t>(s.size());
                p.m_Data.resize( 4 + s.size() );
                std::memcpy( p.m_Data.data(), &n, 4 );
                std::memcpy( p.m_Data.data() + 4, s.data(), s.size() );
                m_Props.push_back( std::move( p ) );
                return *this;
            }


            NodeRec & Bin(
                ByteSpan b
            )
            {
                return Str( std::string_view( reinterpret_cast<const char *>(b.data()), b.size() ), 'R' );
            }


            template <class T>
            NodeRec & Arr(
                char t,
                const std::vector<T> & v
            )
            {
                Prop p { t, {} };
                const std::uint32_t count = static_cast<std::uint32_t>(v.size()), enc = 0, len = count * sizeof( T );

                p.m_Data.resize( 12 + std::size_t( len ) );
                std::memcpy( p.m_Data.data(), &count, 4 );
                std::memcpy( p.m_Data.data() + 4, &enc, 4 );
                std::memcpy( p.m_Data.data() + 8, &len, 4 );

                if (len)
                    std::memcpy( p.m_Data.data() + 12, v.data(), len );

                m_Props.push_back( std::move( p ) );
                return *this;
            }
        };


        inline void WriteNode(
            ByteWriter & w,
            const NodeRec & n
        )
        {
            const std::size_t start = w.Size();

            w.Put( std::uint32_t { 0 } ); // End offset, patched below.
            w.Put( static_cast<std::uint32_t>(n.m_Props.size()) );

            std::uint32_t plen = 0;
            for (const auto & p : n.m_Props)
                plen += 1 + static_cast<std::uint32_t>(p.m_Data.size());

            w.Put( plen );
            w.Put( static_cast<std::uint8_t>(n.m_Name.size()) );
            w.Put( AsBytes( n.m_Name ) );

            for (const auto & p : n.m_Props)
            {
                w.Put( static_cast<std::uint8_t>(p.m_Type) );
                w.Put( ByteSpan( p.m_Data ) );
            }

            if (!n.m_Children.empty())
            {
                for (const auto & c : n.m_Children)
                    WriteNode( w, c );

                w.Zeros( 13 );
            }

            w.PutAt( start, static_cast<std::uint32_t>(w.Size()) );
        }


        // 'Properties70' 'P' entry.
        inline NodeRec & Prop70(
            NodeRec & p70,
            std::string_view name,
            std::string_view type,
            std::string_view label,
            std::string_view flags
        )
        {
            return p70.Add( "P" ).Str( name ).Str( type ).Str( label ).Str( flags );
        }


        inline std::string Cls(
            std::string_view name,
            std::string_view klass
        )
        {
            std::string s( name );

            s.push_back( '\0' );
            s.push_back( '\x01' );
            s += klass;
            return s;
        }


        // Row vector matrix translation, Euler XYZ degrees, scale.
        inline void Decompose( 
            const std::array<float, 16> & m,
            double t[3], 
            double r[3],
            double s[3] 
        )
        {
            double rows[3][3];

            for (int i = 0; i < 3; ++i)
            {
                const double x = m[i * 4], y = m[i * 4 + 1], z = m[i * 4 + 2];
                s[i] = std::sqrt( x * x + y * y + z * z );
                const double k = s[i] > 1e-12 ? 1 / s[i] : 0;
                rows[i][0] = x * k, rows[i][1] = y * k, rows[i][2] = z * k;
            }

            t[0] = m[12], t[1] = m[13], t[2] = m[14];

            // Column vector rotation R = transpose[rows], R = Rz * Ry * Rx.
            auto rot = [&] ( int a, int b )
            {
                return rows[b][a];
            };

            const double rad = 180.0 / std::numbers::pi;
            const double sy = std::clamp( -rot( 2, 0 ), -1.0, 1.0 );
            r[1] = std::asin( sy ) * rad;

            if (std::abs( sy ) < 0.99999)
            {
                r[0] = std::atan2( rot( 2, 1 ), rot( 2, 2 ) ) * rad;
                r[2] = std::atan2( rot( 1, 0 ), rot( 0, 0 ) ) * rad;
            }
            else
            {
                r[0] = std::atan2( -rot( 1, 2 ), rot( 1, 1 ) ) * rad;
                r[2] = 0;
            }
        }
    }


    [[nodiscard]] inline Result<void> WriteFbx(
        const Scene & sc,
        const fs::path & path
    )
    {
        using fbx::NodeRec;
        std::int64_t next_id = 1000000;

        auto id = [&]
        {
            return next_id++;
        };

        std::vector<NodeRec> top;
        {
            NodeRec h { "FBXHeaderExtension", {}, {} };
            h.Add( "FBXHeaderVersion" ).I32( 1003 );
            h.Add( "FBXVersion" ).I32( 7400 );

            auto & ts = h.Add( "CreationTimeStamp" );
            ts.Add( "Version" ).I32( 1000 );

            for (auto [k, v] : std::initializer_list<std::pair<const char *, int>> {
                { "Year", 2026 }, { "Month", 1 }, { "Day", 1 }, { "Hour", 0 }, { "Minute", 0 }, { "Second", 0 }, { "Millisecond", 0 } })
                ts.Add( k ).I32( v );
            h.Add( "Creator" ).Str( "smpack" );

            top.push_back( std::move( h ) );

            static constexpr unsigned char file_id[16] = { 0x28, 0xb3, 0x2a, 0xeb, 0xb6, 0x24, 0xcc, 0xc2,
                0xbf, 0xc8, 0xb0, 0x2a, 0xa9, 0x2b, 0xfc, 0xf1 };

            NodeRec fid { "FileId", {}, {} };
            fid.Bin( ByteSpan( reinterpret_cast<const std::byte *>(file_id), 16 ) );
            top.push_back( std::move( fid ) );
            NodeRec ct { "CreationTime", {}, {} };
            ct.Str( "1970-01-01 10:00:00:000" );
            top.push_back( std::move( ct ) );

            NodeRec cr { "Creator", {}, {} };
            cr.Str( "smpack" );

            top.push_back( std::move( cr ) );
        }
        {
            NodeRec gs { "GlobalSettings", {}, {} };
            gs.Add( "Version" ).I32( 1000 );

            auto & p = gs.Add( "Properties70" );
            fbx::Prop70( p, "UpAxis", "int", "Integer", "" ).I32( 1 );
            fbx::Prop70( p, "UpAxisSign", "int", "Integer", "" ).I32( 1 );
            fbx::Prop70( p, "FrontAxis", "int", "Integer", "" ).I32( 2 );
            fbx::Prop70( p, "FrontAxisSign", "int", "Integer", "" ).I32( 1 );
            fbx::Prop70( p, "CoordAxis", "int", "Integer", "" ).I32( 0 );
            fbx::Prop70( p, "CoordAxisSign", "int", "Integer", "" ).I32( 1 );
            fbx::Prop70( p, "OriginalUpAxis", "int", "Integer", "" ).I32( 1 );
            fbx::Prop70( p, "OriginalUpAxisSign", "int", "Integer", "" ).I32( 1 );
            fbx::Prop70( p, "UnitScaleFactor", "double", "Number", "" ).F64( 100.0 );
            fbx::Prop70( p, "OriginalUnitScaleFactor", "double", "Number", "" ).F64( 100.0 );

            top.push_back( std::move( gs ) );
        }
        {
            NodeRec docs { "Documents", {}, {} };
            docs.Add( "Count" ).I32( 1 );

            auto & d = docs.Add( "Document" ).I64( id() ).Str( "" ).Str( "Scene" );
            d.Add( "RootNode" ).I64( 0 );

            top.push_back( std::move( docs ) );
            top.push_back( NodeRec { "References", {}, {} } );
        }

        NodeRec objects { "Objects", {}, {} };
        NodeRec conns { "Connections", {}, {} };

        auto oo = [&] ( std::int64_t child, std::int64_t parent )
        {
            conns.Add( "C" ).Str( "OO" ).I64( child ).I64( parent );
        };

        auto op = [&] ( std::int64_t child, std::int64_t parent, std::string_view p )
        {
            conns.Add( "C" ).Str( "OP" ).I64( child ).I64( parent ).Str( p );
        };

        std::size_t n_models = 0, n_geoms = 0, n_tex = 0;
        std::vector<std::int64_t> mat_ids;

        for (const auto & m : sc.m_Materials)
        {
            const auto mid = id();
            mat_ids.push_back( mid );

            auto & mo = objects.Add( "Material" ).I64( mid ).Str( fbx::Cls( m.m_Name, "Material" ) ).Str( "" );
            mo.Add( "Version" ).I32( 102 );
            mo.Add( "ShadingModel" ).Str( "phong" );
            mo.Add( "MultiLayer" ).I32( 0 );

            auto & p = mo.Add( "Properties70" );
            fbx::Prop70( p, "DiffuseColor", "Color", "", "A" ).F64( 0.8 ).F64( 0.8 ).F64( 0.8 );

            if (m.m_Overlay)
            {
                fbx::Prop70( p, "TransparencyFactor", "Number", "", "A" ).F64( 0.85 );
                fbx::Prop70( p, "Opacity", "double", "Number", "" ).F64( 0.15 );
            }

            auto tex = [&] ( const std::string & file, std::string_view slot )
            {
                if (file.empty())
                    return;

                const auto tid = id(), vid = id();
                const std::string nm = fs::path( file ).stem().string();

                auto & t = objects.Add( "Texture" ).I64( tid ).Str( fbx::Cls( nm, "Texture" ) ).Str( "" );
                t.Add( "Type" ).Str( "TextureVideoClip" );
                t.Add( "Version" ).I32( 202 );
                t.Add( "TextureName" ).Str( fbx::Cls( nm, "Texture" ) );
                t.Add( "Media" ).Str( fbx::Cls( nm, "Video" ) );
                t.Add( "FileName" ).Str( file );
                t.Add( "RelativeFilename" ).Str( file );

                auto & v = objects.Add( "Video" ).I64( vid ).Str( fbx::Cls( nm, "Video" ) ).Str( "Clip" );
                v.Add( "Type" ).Str( "Clip" );
                v.Add( "FileName" ).Str( file );
                v.Add( "RelativeFilename" ).Str( file );

                oo( vid, tid );
                op( tid, mid, slot );
                ++n_tex;
            };

            tex( m.m_BaseColor, "DiffuseColor" );

            // Cutout in the color map alpha. Importers read it from the transparency slot.
            if (m.m_Alpha == AlphaMode::ALPHA_MASK)
                tex( m.m_BaseColor, "TransparentColor" );

            tex( m.m_Normal, "NormalMap" );
        }

        // Skeleton. One limb node model per joint.
        const std::size_t nj = sc.m_Skeleton ? sc.m_Skeleton->Size() : 0;
        std::vector<std::int64_t> joint_ids( nj );
        std::size_t n_attrs = 0, n_deformers = 0;

        for (std::size_t j = 0; j < nj; ++j)
        {
            joint_ids[j] = id();
            const auto name = Scene::JointName( j );

            auto & jm = objects.Add( "Model" ).I64( joint_ids[j] ).Str( fbx::Cls( name, "Model" ) ).Str( "LimbNode" );
            jm.Add( "Version" ).I32( 232 );
            auto & p = jm.Add( "Properties70" );

            double t[3], r[3], sc3[3];
            fbx::Decompose( sc.m_Skeleton->m_Local[j], t, r, sc3 );
            fbx::Prop70( p, "Lcl Translation", "Lcl Translation", "", "A" ).F64( t[0] ).F64( t[1] ).F64( t[2] );
            fbx::Prop70( p, "Lcl Rotation", "Lcl Rotation", "", "A" ).F64( r[0] ).F64( r[1] ).F64( r[2] );
            fbx::Prop70( p, "Lcl Scaling", "Lcl Scaling", "", "A" ).F64( sc3[0] ).F64( sc3[1] ).F64( sc3[2] );

            jm.Add( "Shading" ).Chr( 'Y' );
            jm.Add( "Culling" ).Str( "CullingOff" );

            const auto aid = id();

            auto & at = objects.Add( "NodeAttribute" ).I64( aid ).Str( fbx::Cls( name, "NodeAttribute" ) ).Str( "LimbNode" );
            at.Add( "TypeFlags" ).Str( "Skeleton" );

            auto & ap = at.Add( "Properties70" );
            fbx::Prop70( ap, "Size", "double", "Number", "" ).F64( 3.0 );

            oo( aid, joint_ids[j] );

            const int par = sc.m_Skeleton->m_Parents[j];
            oo( joint_ids[j], par < 0 ? 0 : joint_ids[static_cast<std::size_t>( par )] );

            ++n_models;
            ++n_attrs;
        }

        auto mat16 = [] ( const mesh::Mat44 & m )
        {
            std::vector<double> v( 16 );

            for (int i = 0; i < 16; ++i)
                v[i] = m[i];

            return v;
        };

        std::vector<std::int64_t> skinned_models;

        for (const auto & n : sc.m_Nodes)
        {
            const auto model_id = id();
            auto & mo = objects.Add( "Model" ).I64( model_id ).Str( fbx::Cls( n.m_Name, "Model" ) ).Str( n.m_Subs.empty() ? "Null" : "Mesh" );
            mo.Add( "Version" ).I32( 232 );

            auto & p = mo.Add( "Properties70" );

            if (n.m_Matrix)
            {
                double t[3], r[3], s[3];
                fbx::Decompose( *n.m_Matrix, t, r, s );
                fbx::Prop70( p, "Lcl Translation", "Lcl Translation", "", "A" ).F64( t[0] ).F64( t[1] ).F64( t[2] );
                fbx::Prop70( p, "Lcl Rotation", "Lcl Rotation", "", "A" ).F64( r[0] ).F64( r[1] ).F64( r[2] );
                fbx::Prop70( p, "Lcl Scaling", "Lcl Scaling", "", "A" ).F64( s[0] ).F64( s[1] ).F64( s[2] );
            }

            mo.Add( "Shading" ).Chr( 'T' );
            mo.Add( "Culling" ).Str( "CullingOff" );

            oo( model_id, n.m_Joint >= 0 && static_cast<std::size_t>(n.m_Joint) < nj ? joint_ids[static_cast<std::size_t>(n.m_Joint)] : 0 );
            ++n_models;

            if (n.m_Subs.empty())
                continue;

            const bool skin = nj > 0 && n.Skinned();

            std::vector<std::vector<std::int32_t>> cl_idx( skin ? nj : 0 );
            std::vector<std::vector<double>> cl_w( skin ? nj : 0 );

            // Merge the submeshes into one geometry with a per polygon material layer.
            std::vector<double> verts, normals;
            std::array<std::vector<double>, 4> uvs;
            std::vector<std::int32_t> poly, mats;
            std::array<bool, 4> has_uv {};
            bool has_n = true;

            for (const auto & s : n.m_Subs)
            {
                has_n = has_n && !s.m_Geo->m_Normals.empty();

                for (std::size_t k = 0; k < 4; ++k)
                    has_uv[k] = has_uv[k] || !s.m_Geo->m_Uvs[k].empty();
            }

            std::vector<std::uint32_t> local_mat;
            for (const auto & s : n.m_Subs)
            {
                std::uint32_t slot = 0;
                for (; slot < local_mat.size() && local_mat[slot] != s.m_Material; ++slot)
                {}

                if (slot == local_mat.size())
                    local_mat.push_back( s.m_Material );

                const auto base = static_cast<std::int32_t>( verts.size() / 3 );
                const auto & g = *s.m_Geo;
                const std::size_t nv = g.m_Positions.size() / 3;

                for (float f : g.m_Positions)
                    verts.push_back( f );

                if (skin)
                    for (std::size_t v = 0; v < nv; ++v)
                        for (int c = 0; c < 4; ++c)
                        {
                            const float wt = g.m_Weights[v * 4 + c];
                            const auto jt = g.m_Joints[v * 4 + c];

                            if (wt <= 0 || jt >= nj)
                                continue;

                            cl_idx[jt].push_back( base + static_cast<std::int32_t>( v ) );
                            cl_w[jt].push_back( wt );
                        }

                for (std::size_t k = 0; k < 4; ++k)
                {
                    if (!has_uv[k])
                        continue;

                    for (std::size_t v = 0; v < nv; ++v)
                    {
                        const bool ok = g.m_Uvs[k].size() >= v * 2 + 2;

                        uvs[k].push_back( ok ? g.m_Uvs[k][v * 2] : 0.0 );
                        uvs[k].push_back( ok ? 1.0 - g.m_Uvs[k][v * 2 + 1] : 0.0 );
                    }
                }

                for (std::size_t t = 0; t + 2 < g.m_Indices.size(); t += 3)
                {
                    const std::uint32_t tri[3] = { g.m_Indices[t], g.m_Indices[t + 2], g.m_Indices[t + 1] };

                    for (int c = 0; c < 3; ++c)
                    {
                        const auto vi = base + static_cast<std::int32_t>( tri[c] );
                        poly.push_back( c == 2 ? ~vi : vi );

                        if (has_n)
                            for (int a = 0; a < 3; ++a)
                                normals.push_back( g.m_Normals[std::size_t( tri[c] ) * 3 + a] );
                    }

                    mats.push_back( static_cast<std::int32_t>( slot ) );
                }
            }

            const auto gid = id();
            auto & geo = objects.Add( "Geometry" ).I64( gid ).Str( fbx::Cls( n.m_Name, "Geometry" ) ).Str( "Mesh" );

            geo.Add( "Vertices" ).Arr( 'd', verts );
            geo.Add( "PolygonVertexIndex" ).Arr( 'i', poly );
            geo.Add( "GeometryVersion" ).I32( 124 );

            if (has_n)
            {
                auto & le = geo.Add( "LayerElementNormal" ).I32( 0 );

                le.Add( "Version" ).I32( 101 );
                le.Add( "Name" ).Str( "" );
                le.Add( "MappingInformationType" ).Str( "ByPolygonVertex" );
                le.Add( "ReferenceInformationType" ).Str( "Direct" );
                le.Add( "Normals" ).Arr( 'd', normals );
            }

            std::vector<std::int32_t> uv_index;
            int uv_layers = 0;
            for (std::size_t k = 0; k < 4; ++k)
            {
                if (!has_uv[k])
                    continue;

                if (uv_index.empty())
                {
                    uv_index.reserve( poly.size() );

                    for (auto v : poly)
                        uv_index.push_back( v < 0 ? ~v : v );
                }

                auto & le = geo.Add( "LayerElementUV" ).I32( uv_layers++ );

                le.Add( "Version" ).I32( 101 );
                le.Add( "Name" ).Str( std::format( "UVMap{}", k == 0 ? std::string() : std::to_string( k ) ) );
                le.Add( "MappingInformationType" ).Str( "ByPolygonVertex" );
                le.Add( "ReferenceInformationType" ).Str( "IndexToDirect" );
                le.Add( "UV" ).Arr( 'd', uvs[k] );
                le.Add( "UVIndex" ).Arr( 'i', uv_index );
            }
            {
                auto & le = geo.Add( "LayerElementMaterial" ).I32( 0 );

                le.Add( "Version" ).I32( 101 );
                le.Add( "Name" ).Str( "" );
                le.Add( "MappingInformationType" ).Str( "ByPolygon" );
                le.Add( "ReferenceInformationType" ).Str( "IndexToDirect" );
                le.Add( "Materials" ).Arr( 'i', mats );
            }

            for (int layer = 0; layer < std::max( 1, uv_layers ); ++layer)
            {
                auto & l = geo.Add( "Layer" ).I32( layer );
                l.Add( "Version" ).I32( 100 );

                auto elem = [&] ( std::string_view t )
                {
                    auto & e = l.Add( "LayerElement" );

                    e.Add( "Type" ).Str( t );
                    e.Add( "TypedIndex" ).I32( layer );
                };

                if (layer == 0)
                {
                    if (has_n)
                        elem( "LayerElementNormal" );

                    elem( "LayerElementMaterial" );
                }

                if (layer < uv_layers)
                    elem( "LayerElementUV" );
            }

            oo( gid, model_id );

            for (auto m : local_mat)
                if (m < mat_ids.size()) oo( mat_ids[m], model_id );

            ++n_geoms;

            if (skin)
            {
                const auto skin_id = id();
                auto & sk = objects.Add( "Deformer" ).I64( skin_id ).Str( fbx::Cls( n.m_Name, "Deformer" ) ).Str( "Skin" );

                sk.Add( "Version" ).I32( 101 );
                sk.Add( "Link_DeformAcuracy" ).F64( 50.0 );

                oo( skin_id, gid );
                ++n_deformers;

                mesh::Mat44 ident {};
                ident[0] = ident[5] = ident[10] = ident[15] = 1;

                for (std::size_t j = 0; j < nj; ++j)
                {
                    if (cl_idx[j].empty())
                        continue;

                    const auto cid = id();
                    auto & cl = objects.Add( "Deformer" ).I64( cid ).Str( fbx::Cls( Scene::JointName( j ), "SubDeformer" ) ).Str( "Cluster" );

                    cl.Add( "Version" ).I32( 100 );
                    cl.Add( "UserData" ).Str( "" ).Str( "" );
                    cl.Add( "Indexes" ).Arr( 'i', cl_idx[j] );
                    cl.Add( "Weights" ).Arr( 'd', cl_w[j] );

                    // FBX SDK convention. 'vertex * Transform * inverse(TransformLink) * link', which must equal 'vertex * inv_bind * link'.
                    cl.Add( "Transform" ).Arr( 'd', mat16( mesh::Mul( sc.m_Skeleton->m_InvBind[j], sc.m_Skeleton->m_World[j] ) ) );
                    cl.Add( "TransformLink" ).Arr( 'd', mat16( sc.m_Skeleton->m_World[j] ) );

                    oo( cid, skin_id );
                    oo( joint_ids[j], cid );
                    ++n_deformers;
                }
                skinned_models.push_back( model_id );
            }
        }

        if (!skinned_models.empty())
        {
            mesh::Mat44 ident {};
            ident[0] = ident[5] = ident[10] = ident[15] = 1;
            auto & pose = objects.Add( "Pose" ).I64( id() ).Str( fbx::Cls( "BindPose", "Pose" ) ).Str( "BindPose" );
            pose.Add( "Type" ).Str( "BindPose" );
            pose.Add( "Version" ).I32( 100 );
            pose.Add( "NbPoseNodes" ).I32( static_cast<std::int32_t>(skinned_models.size() + nj) );
            for (auto m : skinned_models)
            {
                auto & pn = pose.Add( "PoseNode" );
                pn.Add( "Node" ).I64( m );
                pn.Add( "Matrix" ).Arr( 'd', mat16( ident ) );
            }
            for (std::size_t j = 0; j < nj; ++j)
            {
                auto & pn = pose.Add( "PoseNode" );
                pn.Add( "Node" ).I64( joint_ids[j] );
                pn.Add( "Matrix" ).Arr( 'd', mat16( sc.m_Skeleton->m_World[j] ) );
            }
        }
        {
            NodeRec defs { "Definitions", {}, {} };

            defs.Add( "Version" ).I32( 100 );
            const std::size_t n_pose = skinned_models.empty() ? 0 : 1;
            defs.Add( "Count" ).I32( static_cast<std::int32_t>(1 + n_models + n_geoms + sc.m_Materials.size() + 2 * n_tex + n_attrs + n_deformers + n_pose) );

            auto ot = [&] ( std::string_view t, std::size_t c )
            {
                if (c)
                    defs.Add( "ObjectType" ).Str( t ).Add( "Count" ).I32( static_cast<std::int32_t>(c) );
            };

            ot( "GlobalSettings", 1 );
            ot( "Model", n_models );
            ot( "Geometry", n_geoms );
            ot( "Material", sc.m_Materials.size() );
            ot( "Texture", n_tex );
            ot( "Video", n_tex );
            ot( "NodeAttribute", n_attrs );
            ot( "Deformer", n_deformers );
            ot( "Pose", n_pose );

            top.push_back( std::move( defs ) );
        }

        top.push_back( std::move( objects ) );
        top.push_back( std::move( conns ) );

        ByteWriter w;
        w.Put( AsBytes( std::string_view( "Kaydara FBX Binary  \0\x1a\0", 23 ) ) );
        w.Put( std::uint32_t { 7400 } );

        for (const auto & n : top)
            fbx::WriteNode( w, n );

        w.Zeros( 13 );

        static constexpr unsigned char foot_id[16] = { 0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66,
            0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26, 0x7e };

        static constexpr unsigned char foot_magic[16] = { 0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e,
            0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b };

        w.Put( ByteSpan( reinterpret_cast<const std::byte *>(foot_id), 16 ) );
        w.Zeros( 4 );
        std::size_t pad = 16 - w.Size() % 16;
        w.Zeros( pad );
        w.Put( std::uint32_t { 7400 } );
        w.Zeros( 120 );
        w.Put( ByteSpan( reinterpret_cast<const std::byte *>(foot_magic), 16 ) );

        return io::WriteFile( path, w.Buf() );
    }
}