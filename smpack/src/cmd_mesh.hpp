// 'mesh list', 'mesh export'. Models in a WAD to glTF 2.0 (.gltf, .glb) or FBX.

#pragma once

#include <fstream>
#include <charconv>
#include <unordered_map>

#include "cli.hpp"
#include "smpack/mesh.hpp"
#include "smpack/scene_export.hpp"

namespace smpack::cli
{
    // Streamed geometry lookup across every .lodpack next to the WAD.

    class LodLibrary
    {
    private:

        struct Loc
        {
            std::uint32_t m_Pack;
            std::uint32_t m_Group;
            std::uint64_t m_Offset;
            std::uint32_t m_Size;
        };

        struct PackFile
        {
            fs::path m_Path;
            lodpack::Pack m_Toc;
            std::unique_ptr<std::ifstream> m_Stream;
        };

        std::vector<PackFile> m_Packs;
        std::unordered_map<std::uint64_t, Loc> m_Blocks;

        void Add( 
            const fs::path & toc
        )
        {
            const fs::path payload = toc.string().substr( 0, toc.string().size() - 4 );

            std::error_code ec;
            if (!fs::exists( payload, ec ))
                return;

            auto raw = io::ReadFile( toc );

            if (!raw)
                return;

            Bytes dec;
            ByteSpan d = *raw;

            if (lz4::LooksLikeFrame( d ))
            {
                auto x = lz4::DecompressFrame( d );

                if (!x)
                    return;

                dec = std::move( *x );
                d = dec;
            }

            auto pk = lodpack::Parse( d );

            if (!pk)
                return;

            const auto idx = static_cast<std::uint32_t>(m_Packs.size());

            for (const auto & b : pk->m_Blocks)
                m_Blocks[b.m_BlockDataHash] = Loc { idx, b.m_GroupIdx, pk->m_Groups[b.m_GroupIdx].m_FileOffset + b.m_GroupOffset, b.m_BlockDataSize };

            m_Packs.push_back( PackFile { payload, std::move( *pk ), nullptr } );
        }


        Result<Bytes> ReadRange(
            std::uint32_t pack,
            std::uint64_t offset,
            std::uint64_t size
        )
        {
            auto & pk = m_Packs[pack];

            if (!pk.m_Stream)
            {
                pk.m_Stream = std::make_unique<std::ifstream>( pk.m_Path, std::ios::binary );

                if (!*pk.m_Stream)
                    return Fail( Errc::IO_FAILURE, std::format( "cannot open '{}'", pk.m_Path.string() ) );
            }

            Bytes b( static_cast<std::size_t>(size) );
            pk.m_Stream->clear();
            pk.m_Stream->seekg( static_cast<std::streamoff>(offset) );

            if (!pk.m_Stream->read( reinterpret_cast<char *>(b.data()), static_cast<std::streamsize>(b.size()) ))
                return Fail( Errc::IO_FAILURE, std::format( "short read in '{}'", pk.m_Path.string() ) );

            return b;
        }

    public:

        // Index every '*.lodpack.toc' in <dir>. Later packs win, as in the game.
        void Open(
            const fs::path & dir
        )
        {
            std::error_code ec;
            std::vector<fs::path> tocs;

            for (const auto & e : fs::directory_iterator( dir.empty() ? fs::path( "." ) : dir, ec ))
                if (e.path().string().ends_with( ".lodpack.toc" ))
                    tocs.push_back( e.path() );

            std::sort( tocs.begin(), tocs.end() );

            for (const auto & t : tocs)
                Add( t );
        }


        [[nodiscard]] bool Has(
            std::uint64_t hash
        ) const
        {
            return m_Blocks.count( hash ) != 0;
        }


        [[nodiscard]] std::size_t PackCount() const
        {
            return m_Packs.size();
        }


        [[nodiscard]] Result<Bytes> Read(
            std::uint64_t hash
        )
        {
            auto it = m_Blocks.find( hash );

            if (it == m_Blocks.end())
                return Fail( Errc::NOT_FOUND, std::format( "lodpack block {:#018x} not found", hash ) );

            return ReadRange( it->second.m_Pack, it->second.m_Offset, it->second.m_Size );
        }

        // Table entries of a block and of the group holding it.
        struct BlockInfo
        {
            lodpack::GroupEntry m_Group {};
            lodpack::BlockEntry m_Block {};
        };

        [[nodiscard]] std::optional<BlockInfo> Info( 
            std::uint64_t hash
        ) const
        {
            auto it = m_Blocks.find( hash );

            if (it == m_Blocks.end())
                return std::nullopt;

            const auto & toc = m_Packs[it->second.m_Pack].m_Toc;

            BlockInfo bi;
            bi.m_Group = toc.m_Groups[it->second.m_Group];

            const auto b = std::lower_bound( toc.m_Blocks.begin(), toc.m_Blocks.end(), hash, [] (
                const lodpack::BlockEntry & e, std::uint64_t h )
            {
                return e.m_BlockDataHash < h;
            } );

            if (b != toc.m_Blocks.end() && b->m_BlockDataHash == hash)
                bi.m_Block = *b;
            else
            {
                for (const auto & e : toc.m_Blocks)
                    if (e.m_BlockDataHash == hash)
                        bi.m_Block = e;
            }

            return bi;
        }

        // The group holding a block. Its entry, its blocks and its bytes for building a patch pack.
        struct Group
        {
            lodpack::GroupEntry m_Entry {};
            std::vector<lodpack::BlockEntry> m_Blocks;
            Bytes m_Data;
            std::string m_Pack;
        };

        [[nodiscard]] Result<Group> GroupOf( 
            std::uint64_t hash
        )
        {
            auto it = m_Blocks.find( hash );

            if (it == m_Blocks.end())
                return Fail( Errc::NOT_FOUND, std::format( "lodpack block {:#018x} not found", hash ) );

            const auto & pk = m_Packs[it->second.m_Pack];

            Group g;
            g.m_Entry = pk.m_Toc.m_Groups[it->second.m_Group];
            g.m_Pack = PackBase( pk.m_Path );

            for (const auto & b : pk.m_Toc.m_Blocks)
                if (b.m_GroupIdx == it->second.m_Group)
                    g.m_Blocks.push_back( b );

            SMPACK_TRY( d, ReadRange( it->second.m_Pack, g.m_Entry.m_FileOffset, g.m_Entry.m_GroupDataSize ) );
            g.m_Data = std::move( d );
            return g;
        }
    };

    // Models in a WAD.

    struct ModelRef
    {
        std::string m_Name; // Without the 'MESH_' prefix.
        std::size_t m_Mesh = 0; // Entry index of 'MESH_<name>'.
        std::optional<std::size_t> m_Gpu, m_Group, m_StaticData;
        std::vector<mesh::Prim> m_Prims;
        std::vector<mesh::Part> m_Parts;
        std::vector<std::string> m_Materials;
        std::optional<mesh::Skeleton> m_Skeleton;
        std::vector<bool> m_HelperMaterials; // Per material ID. Collider or height-carving helper, never drawn
        std::vector<std::vector<std::string>> m_MaterialRefs; // Per material ID. Textures, parent materials and shaders

        [[nodiscard]] bool Helper( std::uint32_t mat ) const
        {
            return mat < m_HelperMaterials.size() && m_HelperMaterials[mat];
        }

        // Per part. Drawn with the given config ['-1' = default, '-2' = every part].
        [[nodiscard]] std::vector<bool> VisibleParts( int config = -1 ) const
        {
            std::vector<bool> out( m_Parts.size(), true );

            if (!m_Skeleton || config == -2)
                return out;

            const auto vis = m_Skeleton->Visibility( config == -1 ? m_Skeleton->DefaultConfig() : config );

            for (std::size_t i = 0; i < m_Parts.size(); ++i)
                if (m_Parts[i].m_Parent < vis.size())
                    out[i] = vis[m_Parts[i].m_Parent];

            return out;
        }
    };


    [[nodiscard]] inline bool IsHelperShader(
        std::string_view n
    )
    {
        return n.starts_with( "customrt_" ) || n.starts_with( "veg_collider_" ) || n.starts_with( "windadjust_" ) || n.starts_with( "height_mesh_" ) ||
            n.starts_with( "heightcarving_" );
    }


    [[nodiscard]] inline bool IsModelParm(
        const wad::Entry & e
    )
    {
        return e.m_E.m_Id == 1 && e.m_E.m_ServerId == 12;
    }


    // Names listed by a 'ReferenceParms' chunk.
    [[nodiscard]] inline std::vector<std::string> ReferenceNames( 
        ByteSpan d
    )
    {
        std::vector<std::string> out;
        ByteReader r( d );

        auto n = r.At<std::uint32_t>( 0 );

        if (!n)
            return out;

        for (std::uint32_t i = 0; i < *n && i < 4096; ++i)
        {
            const std::uint64_t o = 8 + 16 + 76ull * i;

            if (o >= d.size())
                break;

            out.emplace_back( FixedStr( reinterpret_cast<const char *>( d.data() + o ), std::min<std::size_t>( 56, d.size() - o ) ) );
        }

        return out;
    }

    // The parms a chunk references.
    [[nodiscard]] inline std::vector<std::string> ReferencesAfter(
        const wad::Wad & w,
        ByteSpan data,
        std::size_t i
    )
    {
        const auto & es = w.m_Entries;

        // Block padding may sit between a parm and its references.
        while (i + 1 < es.size() && es[i + 1].m_E.m_Id == 25)
            ++i;

        if (i + 1 < es.size() && es[i + 1].m_E.m_Id == 60)
            return ReferenceNames( wad::ChunkData( data, es[i + 1] ) );

        std::vector<std::string> out;
        for (std::size_t k = i + 1; k < es.size() && es[k].m_E.m_Id == 1 && es[k].m_E.m_Length == 0; ++k)
        {
            const auto n = es[k].Name();

            if (n.starts_with( "MAT_" ) || n.starts_with( "TX_" ) || n.find( "_vs_" ) != std::string::npos || n.find( "_ps_" ) != std::string::npos)
                out.push_back( n );
            else
                break;
        }

        return out;
    }


    [[nodiscard]] inline std::vector<ModelRef> FindModels(
        const wad::Wad & w,
        ByteSpan data,
        std::vector<std::string> & warnings
    )
    {
        std::vector<ModelRef> out;
        const auto & es = w.m_Entries;

        auto nearest = [&] ( std::size_t from, auto pred ) -> std::optional<std::size_t>
        {
            for (std::size_t d = 1; d < 16; ++d)
            {
                if (from >= d && pred( es[from - d] ))
                    return from - d;

                if (from + d < es.size() && pred( es[from + d] ))
                    return from + d;
            }

            for (std::size_t i = 0; i < es.size(); ++i)
                if (pred( es[i] ))
                    return i;

            return std::nullopt;
        };

        std::unordered_map<std::string, std::size_t> material_parms;

        for (std::size_t i = 0; i < es.size(); ++i)
            if (es[i].m_E.m_Id == 1 && es[i].m_E.m_ServerId == 10 && es[i].m_E.m_Length) material_parms.emplace( es[i].Name(), i );

        for (std::size_t i = 0; i < es.size(); ++i)
        {
            const auto & e = es[i];

            if (!IsModelParm( e ) || e.m_E.m_Length == 0)
                continue;

            const std::string n = e.Name();

            if (!n.starts_with( "MESH_" ))
                continue;

            ModelRef m;
            m.m_Name = n.substr( 5 );
            m.m_Mesh = i;

            auto prims = mesh::ParseMeshData( wad::ChunkData( data, e ) );

            if (!prims)
            {
                warnings.push_back( std::format( "{}: {}", n, prims.error().m_Message ) );
                continue;
            }

            m.m_Prims = std::move( *prims );

            m.m_Gpu = nearest( i, [&] ( const wad::Entry & x )
            {
                return x.m_E.m_Id == 29 && x.Name() == "MG_" + m.m_Name + "_gpu";
            } );

            m.m_Group = nearest( i, [&] ( const wad::Entry & x )
            {
                return IsModelParm( x ) && x.m_E.m_Length && x.Name() == "MG_" + m.m_Name;
            } );

            if (!m.m_Group)
                m.m_StaticData = nearest( i, [&] ( const wad::Entry & x )
            {
                return IsModelParm( x ) && x.m_E.m_Length && x.Name() == m.m_Name + "_smsh_data";
            } );

            m.m_Materials = ReferencesAfter( w, data, i );

            for (const auto & mat : m.m_Materials)
            {
                bool helper = false;
                std::vector<std::string> refs;

                if (auto it = material_parms.find( mat ); it != material_parms.end())
                    refs = ReferencesAfter( w, data, it->second );

                for (const auto & r : refs)
                    helper = helper || IsHelperShader( r );

                m.m_HelperMaterials.push_back( helper );
                m.m_MaterialRefs.push_back( std::move( refs ) );
            }

            Result<std::vector<mesh::Part>> parts = std::vector<mesh::Part> {};

            if (m.m_Group)
                parts = mesh::ParseModelGroup( wad::ChunkData( data, es[*m.m_Group] ) );
            else if (m.m_StaticData)
                parts = mesh::ParseStaticMesh( wad::ChunkData( data, es[*m.m_StaticData] ) );

            if (!parts)
                warnings.push_back( std::format( "{}: {}", n, parts.error().m_Message ) );
            else
                m.m_Parts = std::move( *parts );

            if (m.m_Group)
            {
                // The skeleton lives in the game object prototype.
                std::string base = m.m_Name;
                if (auto us = base.rfind( '_' ); us != std::string::npos && us + 1 < base.size() &&
                    std::all_of( base.begin() + static_cast<std::ptrdiff_t>(us) + 1, base.end(), [] ( char c )
                {
                    return std::isdigit( static_cast<unsigned char>(c) );
                } )) base.resize( us );

                auto lower = [] ( std::string x )
                {
                    for (auto & c : x)
                        c = static_cast<char>(std::tolower( static_cast<unsigned char>(c) ));

                    return x;
                };

                const std::string want = lower( "goProto" + base );

                if (auto pi = nearest( i, [&] ( const wad::Entry & x )
                {
                    return x.m_E.m_Id == 1 && x.m_E.m_ServerId == 1 && x.m_E.m_Length && lower( x.Name() ) == want;
                } ))
                {
                    auto sk = mesh::ParseSkeleton( wad::ChunkData( data, es[*pi] ) );

                    if (sk)
                        m.m_Skeleton = std::move( *sk );
                    else
                        warnings.push_back( std::format( "{}: skeleton: {}", n, sk.error().m_Message ) );
                }
            }

            if (m.m_Parts.empty())
            {
                // No LOD description. Every primitive as one part.
                mesh::Part p;
                mesh::Lod l;
                for (const auto & pr : m.m_Prims) l.m_PrimIds.push_back( pr.m_PrimId );
                p.m_Lods.push_back( std::move( l ) );
                m.m_Parts.push_back( std::move( p ) );
            }

            out.push_back( std::move( m ) );
        }

        return out;
    }


    // Texture names a material references [the 'ReferenceParms' right after the material parm].
    // Everything a material references. Textures, parent materials and its shaders.
    [[nodiscard]] inline std::vector<std::string> MaterialRefs(
        const wad::Wad & w,
        ByteSpan data,
        const std::string & mat
    )
    {
        const auto & es = w.m_Entries;

        for (std::size_t i = 0; i + 1 < es.size(); ++i)
            if (es[i].m_E.m_Id == 1 && es[i].m_E.m_ServerId == 10 && es[i].m_E.m_Length && es[i].Name() == mat)
                return ReferencesAfter( w, data, i );

        return {};
    }


    [[nodiscard]] inline std::vector<std::string> MaterialTextures( 
        const wad::Wad & w,
        ByteSpan data,
        const std::string & mat
    )
    {
        std::vector<std::string> tx;

        for (auto & n : MaterialRefs( w, data, mat ))
            if (n.starts_with( "TX_" ))
                tx.push_back( std::move( n ) );

        return tx;
    }


    // Materials drawn into custom render targets [vegetation colliders, wind volumes] or the height carving pass
    // [snow and mud deformation shells] are helpers, not visible surfaces.
    [[nodiscard]] inline bool IsHelperMaterial(
        const wad::Wad & w,
        ByteSpan data,
        const std::string & mat
    )
    {
        for (const auto & n : MaterialRefs( w, data, mat ))
            if (IsHelperShader( n ))
                return true;

        return false;
    }


    // The role is the word before the 16 digit hash. Digits, lower case letters, digits.
    [[nodiscard]] inline std::string TextureRole( std::string_view n )
    {
        if (n.size() < 19 || n[n.size() - 17] != '_')
            return {};

        for (std::size_t i = n.size() - 16; i < n.size(); ++i)
            if (!std::isxdigit( static_cast<unsigned char>( n[i] ) ))
                return {};

        const std::size_t end = n.size() - 17;
        const std::size_t us = n.rfind( '_', end - 1 );

        if (us == std::string_view::npos)
            return {};

        const std::string_view w = n.substr( us + 1, end - us - 1 );

        std::size_t i = 0;
        while (i < w.size() && std::isdigit( static_cast<unsigned char>( w[i] ) ))
            ++i;

        const std::size_t letters = i;
        while (i < w.size() && std::islower( static_cast<unsigned char>( w[i] ) ))
            ++i;
        if (i == letters)
            return {};

        while (i < w.size() && std::isdigit( static_cast<unsigned char>( w[i] ) ))
            ++i;
        return i == w.size() ? std::string( w ) : std::string();
    }


    // Letters of a role without the set prefix and suffix digits. '0d' to 'd', 'opc2' to 'opc'.
    [[nodiscard]] inline std::string_view RoleLetters( std::string_view role )
    {
        std::size_t i = 0;
        while (i < role.size() && std::isdigit( static_cast<unsigned char>( role[i] ) ))
            ++i;

        std::size_t e = i;
        while (e < role.size() && std::isalpha( static_cast<unsigned char>( role[e] ) ))
            ++e;

        return role.substr( i, e - i );
    }


    // 'd' or '0d', but not 'd2'.
    [[nodiscard]] inline bool RoleIs(
        std::string_view role,
        char c
    )
    {
        std::size_t i = 0;
        while (i < role.size() && std::isdigit( static_cast<unsigned char>( role[i] ) ))
            ++i;

        return role.size() == i + 1 && role[i] == c;
    }


    [[nodiscard]] inline bool IsOpacityRole(
        std::string_view role
    )
    {
        const auto l = RoleLetters( role );
        return l == "o" || l == "op" || l == "opc" || l == "opacity";
    }


    // Texture name without role and hash. 'TX_baldur00_beard_d_C5EB...' becomes 'TX_baldur00_beard'.
    [[nodiscard]] inline std::string_view TextureStem( std::string_view n )
    {
        const auto role = TextureRole( n );

        if (role.empty())
            return n;

        return n.substr( 0, n.size() - 17 - role.size() - 1 );
    }

    struct MaterialMaps
    {
        std::string m_Base, m_Normal, m_Opacity;
    };

    // Pick the base color, normal and opacity maps from the textures a material references.
    [[nodiscard]] inline MaterialMaps PickMaterialMaps(
        const std::vector<std::string> & tx,
        const std::string & model_base
    )
    {
        auto lower = [] ( std::string_view x )
        {
            std::string o( x );

            for (auto & c : o)
                c = static_cast<char>(std::tolower( static_cast<unsigned char>(c) ));

            return o;
        };

        MaterialMaps mm;

        // Layered materials list several sets, prefer a model specific set over the shared 'TX_b_' library,
        // then numbered color maps ('d2', 'd4').
        for (int pass = 0; pass < 3 && mm.m_Base.empty(); ++pass)
            for (const auto & t : tx)
            {
                const auto role = TextureRole( t );
                const bool ok = pass < 2 ? RoleIs( role, 'd' ) && (pass == 1 || !t.starts_with( "TX_b_" )) : RoleLetters( role ) == "d";

                if (ok)
                {
                    mm.m_Base = t;
                    break;
                }
            }

        // Fur and other layered shaders take their color from a role less map of the model itself
        // ('TX_<model>_<part>_<hash>', e.g. 'TX_atreuswolf00_body').
        const std::string base_l = lower( model_base );
        std::string colour_map;

        for (const auto & t : tx)
        {
            const auto tl = lower( t );

            if (!tl.starts_with( "tx_" + base_l + "_" ) || t.size() < 3 + base_l.size() + 1 + 17)
                continue;

            const std::string word = tl.substr( 3 + base_l.size() + 1, tl.size() - (3 + base_l.size() + 1) - 17 );

            if (!word.empty() && std::all_of( word.begin(), word.end(), [] ( char c )
            {
                return std::isalpha( static_cast<unsigned char>(c) );
            } ))
            {
                colour_map = t;
                break;
            }
        }

        if (!colour_map.empty() && (mm.m_Base.empty() || lower( mm.m_Base ).find( base_l ) == std::string::npos))
            mm.m_Base = colour_map;

        const std::string_view stem = mm.m_Base.empty() ? std::string_view() : TextureStem( mm.m_Base );

        for (const auto & t : tx)
        {
            const auto role = TextureRole( t );

            if (!stem.empty() && TextureStem( t ) == stem)
            {
                if (RoleIs( role, 'n' ) && mm.m_Normal.empty())
                    mm.m_Normal = t;

                if (IsOpacityRole( role ) && mm.m_Opacity.empty())
                    mm.m_Opacity = t;
            }
        }

        if (mm.m_Normal.empty())
            for (const auto & t : tx)
                if (RoleIs( TextureRole( t ), 'n' ))
                {
                    mm.m_Normal = t;
                    break;
                }

        // Hair, lashes and cards often share a generic cutout ('TX_hair_o', 'TX_hair_opc2'). Plain 'o' first.
        for (int pass = 0; pass < 2 && mm.m_Opacity.empty(); ++pass)
            for (const auto & t : tx)
            {
                const auto role = TextureRole( t );

                if (pass == 0 ? RoleLetters( role ) == "o" : IsOpacityRole( role ))
                {
                    mm.m_Opacity = t;
                    break;
                }
            }

        return mm;
    }


    // Pixel shader permutation bits ('<hash>_ps_20004207' is '0x20004207'), '0' when absent.
    [[nodiscard]] inline std::uint32_t ShaderBits( 
        const std::vector<std::string> & refs
    )
    {
        for (const auto & r : refs)
        {
            const auto p = r.find( "_ps_" );

            if (p == std::string::npos || r.size() != p + 4 + 8)
                continue;

            std::uint32_t v = 0;
            if (std::from_chars( r.data() + p + 4, r.data() + r.size(), v, 16 ).ec == std::errc {})
                return v;
        }

        return 0;
    }

    // Alpha tested permutation (lashes, fur shells, braids).
    inline constexpr std::uint32_t SHADER_ALPHA_TEST = 0x4000;

    [[nodiscard]] inline std::string ModelLabel(
        const ModelRef & m
    )
    {
        return m.m_Group ? "model" : m.m_StaticData ? "static" : "mesh";
    }


    [[nodiscard]] inline const mesh::Prim * FindPrim( 
        const ModelRef & m,
        std::uint16_t id
    )
    {
        if (id < m.m_Prims.size() && m.m_Prims[id].m_PrimId == id)
            return &m.m_Prims[id];

        for (const auto & p : m.m_Prims)
            if (p.m_PrimId == id)
                return &p;

        return id < m.m_Prims.size() ? &m.m_Prims[id] : nullptr;
    }


    inline void LodStats(
        const ModelRef & m,
        std::size_t lod,
        std::uint64_t & verts,
        std::uint64_t & tris,
        int config = -1
    )
    {
        verts = tris = 0;
        const auto shown = m.VisibleParts( config );

        for (std::size_t pi = 0; pi < m.m_Parts.size(); ++pi)
        {
            const auto & part = m.m_Parts[pi];

            if (part.m_Lods.empty() || !shown[pi])
                continue;

            const auto & l = part.m_Lods[std::min( lod, part.m_Lods.size() - 1 )];

            for (auto id : l.m_PrimIds)
                if (const auto * p = FindPrim( m, id ); p && p->Visible() && !m.Helper( p->m_MaterialId ))
                    verts += p->m_VertexCount, tris += p->m_PrimitiveCount;
        }
    }


    // '--variant NAME|N|all, --all-parts'. Returns '-1' for the default config, '-2' for every part.
    [[nodiscard]] inline Result<int> ResolveConfig(
        const ModelRef & m,
        const Args & args
    )
    {
        if (args.Has( "--all-parts" ))
            return -2;

        const auto v = args.Get( "--variant" );

        if (!v || v->empty() || *v == "default")
            return -1;

        if (*v == "all")
            return -2;

        if (!m.m_Skeleton || m.m_Skeleton->m_Configs.empty())
            return -1;

        const auto & cs = m.m_Skeleton->m_Configs;

        for (std::size_t i = 0; i < cs.size(); ++i)
        {
            std::string a = cs[i].m_Name, b = *v;

            for (auto & c : a)
                c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

            for (auto & c : b)
                c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

            if (a == b)
                return static_cast<int>( i );
        }

        if (auto n = ParseU64( *v ); n && *n < cs.size())
            return static_cast<int>( *n );

        return Fail( Errc::BAD_ARGUMENT, std::format( "{}: no variant '{}'", m.m_Name, *v ) );
    }


    [[nodiscard]] inline bool Selected(
        const ModelRef & m,
        const Args & args
    )
    {
        if (auto id = args.Get( "--id" ))
            return std::to_string( m.m_Mesh ) == *id;

        if (auto n = args.Get( "--name" ))
            return m.m_Name == *n || "MESH_" + m.m_Name == *n;

        if (auto f = args.Get( "--filter" ))
            return Icontains( m.m_Name, *f );

        return true;
    }


    [[nodiscard]] inline Result<void> CmdMeshList(
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT, "mesh list <file.wad> [--filter TEXT] [--json]" );

        SMPACK_TRY( in, Load( args.m_Pos[0] ) );

        if (in.m_Kind != Kind::WAD)
            return Fail( Errc::UNSUPPORTED, "mesh list needs a .wad" );

        SMPACK_TRY( w, wad::Parse( in.m_Data ) );

        std::vector<std::string> warnings;
        auto models = FindModels( w, in.m_Data, warnings );

        if (models.empty())
            return Fail( Errc::NOT_FOUND, "this WAD contains no models (no MESH, MG or static mesh data)" );

        LodLibrary lib;
        lib.Open( fs::path( args.m_Pos[0] ).parent_path() );
        if (args.Has( "--json" ))
        {
            json::Value arr = json::Value::MakeArray();

            for (const auto & m : models)
            {
                if (!Selected( m, args ))
                    continue;

                std::uint64_t v = 0, t = 0;
                LodStats( m, 0, v, t );

                std::size_t lods = 0, streamed = 0, missing = 0;
                for (const auto & p : m.m_Parts)
                    lods = std::max( lods, p.m_Lods.size() );

                for (const auto & p : m.m_Prims)
                {
                    if (!p.Streamed())
                        continue;

                    ++streamed;

                    if (!lib.Has( p.m_StreamHash ))
                        ++missing;
                }

                std::size_t hidden_parts = 0;
                for (bool b : m.VisibleParts())
                    hidden_parts += !b;

                json::Value j = json::Value::MakeObject();

                j["id"] = m.m_Mesh;
                j["name"] = m.m_Name;
                j["kind"] = ModelLabel( m );
                j["hidden_parts"] = hidden_parts;

                json::Value vars = json::Value::MakeArray();

                if (m.m_Skeleton)
                    for (const auto & c : m.m_Skeleton->m_Configs)
                        vars.Push( c.m_Name );

                j["variants"] = vars;
                j["default_variant"] = m.m_Skeleton ? m.m_Skeleton->DefaultConfig() : -1;
                j["parts"] = m.m_Parts.size();
                j["prims"] = m.m_Prims.size();
                j["lods"] = lods;
                j["vertices"] = v;
                j["triangles"] = t;
                j["materials"] = m.m_Materials.size();
                j["joints"] = m.m_Skeleton ? m.m_Skeleton->Size() : 0;

                std::uint64_t va = 0, ta = 0;
                LodStats( m, 0, va, ta, -2 );

                j["visible"] = ta > 0;
                j["streamed_prims"] = streamed;
                j["missing_prims"] = missing;

                arr.Push( std::move( j ) );
            }

            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "meshlist";
            doc["file"] = GameName( args.m_Pos[0] );
            doc["lodpacks"] = lib.PackCount();
            doc["models"] = arr;

            print( "{}", json::Dump( doc ) );
            return {};
        }

        println( "{:>6}  {:<7} {:>5} {:>5} {:>4} {:>9} {:>9}  {}", "id", "kind", "parts", "prims", "lods", "verts", "tris", "name" );

        for (const auto & m : models)
        {
            if (!Selected( m, args ))
                continue;

            std::uint64_t v = 0, t = 0;
            LodStats( m, 0, v, t );

            std::size_t lods = 0;
            for (const auto & p : m.m_Parts)
                lods = std::max( lods, p.m_Lods.size() );

            println( "{:>6}  {:<7} {:>5} {:>5} {:>4} {:>9} {:>9}  {}", m.m_Mesh, ModelLabel( m ), m.m_Parts.size(), m.m_Prims.size(), lods, v, t, m.m_Name );
        }

        for (const auto & s : warnings)
            println( stderr, "  ! {}", s );

        return {};
    }


    // 'atreuswolf00_0' becomes 'atreuswolf00'.
    [[nodiscard]] inline std::string ModelBaseName( 
        std::string n
    )
    {
        if (auto us = n.rfind( '_' ); us != std::string::npos && us + 1 < n.size() &&
            std::all_of( n.begin() + static_cast<std::ptrdiff_t>(us) + 1, n.end(), [] ( char c )
        {
            return std::isdigit( static_cast<unsigned char>(c) );
        } )) n.resize( us );

        return n;
    }


    // Level position of the first placed part of a static mesh. Exports move it to the origin.
    [[nodiscard]] inline std::array<float, 3> StaticOrigin( const ModelRef & m )
    {
        for (const auto & p : m.m_Parts)
            if (p.m_Transform)
                return { (*p.m_Transform)[12], (*p.m_Transform)[13], (*p.m_Transform)[14] };

        return { 0, 0, 0 };
    }


    // Matrix the exporter gives the node of a part [row vectors, model space]. Identity for skinned parts.
    [[nodiscard]] inline mesh::Mat44 PartExportMatrix( 
        const ModelRef & m,
        std::size_t pi,
        bool skinned,
        bool world
    )
    {
        mesh::Mat44 r {};
        r[0] = r[5] = r[10] = r[15] = 1;
        const auto & part = m.m_Parts[pi];

        if (part.m_Transform)
        {
            r = *part.m_Transform;

            if (!world && m.m_StaticData)
            {
                const auto org = StaticOrigin( m );
                r[12] -= org[0], r[13] -= org[1], r[14] -= org[2];
            }
        }

        if (m.m_Skeleton && !skinned && part.m_Parent < m.m_Skeleton->Size())
        {
            const auto & ib = m.m_Skeleton->m_InvBind[part.m_Parent];
            r = mesh::Mul( scene::detail::IsIdentity( ib ) ? r : ib, m.m_Skeleton->m_World[part.m_Parent] );
        }

        return r;
    }

    struct SceneOptions
    {
        int m_Lod = 0; // '-1', every LOD as separate nodes.
        int m_Config = -1; // '-1', default variant, '-2' every part.
        bool m_Textures = true;
        bool m_World = false; // Keep the level placement of static props.
    };

    // Build the scene of one model. Materials are described in '<tex_out>' for the caller that writes the PNGs.
    [[nodiscard]] inline Result<scene::Scene> BuildScene(
        const ModelRef & m,
        LodLibrary & lib,
        const SceneOptions & o,
        ByteSpan gpu,
        json::Value & tex_out,
        std::vector<std::string> & warnings
    )
    {
        scene::Scene sc;
        sc.m_Name = m.m_Name;
        std::map<std::uint64_t, Bytes> block_cache;
        std::map<std::uint16_t, std::shared_ptr<const mesh::Geometry>> geo_cache;

        auto geometry = [&] ( const mesh::Prim & p ) -> std::shared_ptr<const mesh::Geometry>
        {
            if (auto it = geo_cache.find( p.m_PrimId ); it != geo_cache.end())
                return it->second;

            auto & slot = geo_cache[p.m_PrimId];
            ByteSpan src = gpu;

            if (p.Streamed())
            {
                auto bit = block_cache.find( p.m_StreamHash );

                if (bit == block_cache.end())
                {
                    auto b = lib.Read( p.m_StreamHash );

                    if (!b)
                    {
                        warnings.push_back( std::format( "{} prim {}: {}", m.m_Name, p.m_PrimId, b.error().m_Message ) );
                        return nullptr;
                    }

                    bit = block_cache.emplace( p.m_StreamHash, std::move( *b ) ).first;
                }

                src = bit->second;
            }

            auto g = mesh::Decode( p, src );

            if (!g)
            {
                warnings.push_back( std::format( "{} prim {}: {}", m.m_Name, p.m_PrimId, g.error().m_Message ) );
                return nullptr;
            }

            slot = std::make_shared<const mesh::Geometry>( std::move( *g ) );
            return slot;
        };

        auto available = [&] ( const mesh::Lod & l )
        {
            for (auto id : l.m_PrimIds)
            {
                const auto * p = FindPrim( m, id );

                if (p && !p->Visible())
                    continue;

                if (!p || (p->Streamed() && !lib.Has( p->m_StreamHash )))
                    return false;
            }

            return true;
        };

        const auto shown = m.VisibleParts( o.m_Config );
        std::vector<bool> mat_used;

        for (std::size_t pi = 0; pi < m.m_Parts.size(); ++pi)
        {
            const auto & part = m.m_Parts[pi];

            if (part.m_Lods.empty() || !shown[pi])
                continue;

            std::vector<std::size_t> levels;
            if (o.m_Lod < 0)
            {
                for (std::size_t l = 0; l < part.m_Lods.size(); ++l)
                    levels.push_back( l );
            }
            else
            {
                std::size_t l = std::min<std::size_t>( static_cast<std::size_t>( o.m_Lod ), part.m_Lods.size() - 1 );

                const std::size_t want = l;
                while (l + 1 < part.m_Lods.size() && !available( part.m_Lods[l] ))
                    ++l;

                if (l != want)
                    warnings.push_back( std::format( "{} part {}: LOD {} is in a .lodpack that was not found, using LOD {}", m.m_Name, pi, want, l ) );

                levels.push_back( l );
            }
            for (auto l : levels)
            {
                scene::Node node;

                // The LOD is part of the name whenever it is not LOD 0, so an edited file can be matched back.
                node.m_Name = m.m_Parts.size() == 1 && levels.size() == 1 && l == 0 ? m.m_Name : std::format( "{}_part{}", m.m_Name, pi );

                if (levels.size() > 1 || l != 0)
                    node.m_Name += std::format( "_lod{}", l );

                node.m_Part = static_cast<int>(pi);
                node.m_Lod = static_cast<int>(l);
                node.m_Matrix = part.m_Transform;

                for (auto id : part.m_Lods[l].m_PrimIds)
                {
                    const auto * p = FindPrim( m, id );

                    if (!p || !p->Visible() || m.Helper( p->m_MaterialId ))
                        continue; // Shadow proxies and collider volumes.

                    auto g = geometry( *p );

                    if (!g)
                        continue;

                    node.m_Subs.push_back( scene::Submesh { std::move( g ), p->m_MaterialId, p->m_PrimId } );

                    if (mat_used.size() <= p->m_MaterialId)
                        mat_used.resize( p->m_MaterialId + 1 );

                    mat_used[p->m_MaterialId] = true;
                }

                // Rigid parts of jointed models are stored in the space of their parent joint.
                // The renderer draws them with 'inv_bind * joint', so the inverse bind becomes the node's local matrix.
                if (m.m_Skeleton && !node.m_Subs.empty() && !node.Skinned() && part.m_Parent < m.m_Skeleton->Size())
                {
                    node.m_Joint = static_cast<int>( part.m_Parent );
                    const auto & ib = m.m_Skeleton->m_InvBind[part.m_Parent];

                    if (!scene::detail::IsIdentity( ib ))
                        node.m_Matrix = ib;
                }

                if (!node.m_Subs.empty())
                    sc.m_Nodes.push_back( std::move( node ) );
            }
        }

        if (m.m_Skeleton)
            sc.m_Skeleton = m.m_Skeleton;

        if (!o.m_World && m.m_StaticData)
        {
            // Static meshes carry their level placement, move the first part to the origin and keep the layout.
            const auto org = StaticOrigin( m );
            for (auto & k : sc.m_Nodes)
                if (k.m_Matrix) (*k.m_Matrix)[12] -= org[0], (*k.m_Matrix)[13] -= org[1], (*k.m_Matrix)[14] -= org[2];
        }

        if (sc.m_Nodes.empty())
        {
            bool any = false;

            for (bool b : shown)
                any = any || b;

            return Fail( Errc::NOT_FOUND, any ? std::format( "{}: its geometry is in a .lodpack that was not found next to the WAD", m.m_Name )
                : std::format( "{}: every part is hidden in this variant", m.m_Name ) );
        }

        const std::string model_base = ModelBaseName( m.m_Name );

        for (std::size_t i = 0; i < mat_used.size(); ++i)
        {
            scene::Material mat;
            mat.m_Name = i < m.m_Materials.size() && !m.m_Materials[i].empty() ? m.m_Materials[i] : std::format( "material_{}", i );

            if (mat_used[i])
            {
                static const std::vector<std::string> no_refs;
                const auto & refs = i < m.m_MaterialRefs.size() ? m.m_MaterialRefs[i] : no_refs;

                std::vector<std::string> tx;
                for (const auto & r : refs)
                    if (r.starts_with( "TX_" )) tx.push_back( r );

                const auto maps = PickMaterialMaps( tx, model_base );
                const bool alpha_test = (ShaderBits( refs ) & SHADER_ALPHA_TEST) != 0;

                // A color map with a separate cutout is written as one PNG with the cutout in alpha.
                std::string colour_file;
                if (!maps.m_Base.empty())
                    colour_file = maps.m_Opacity.empty() ? io::SafeName( maps.m_Base ) : io::SafeName( maps.m_Base ) + "_" + std::string( maps.m_Opacity.substr( maps.m_Opacity.size() - 8 ) );
                else if (!maps.m_Opacity.empty())
                    colour_file = io::SafeName( maps.m_Opacity ) + "_mask";

                mat.m_Overlay = colour_file.empty();

                if (!maps.m_Opacity.empty() || alpha_test)
                    mat.m_Alpha = scene::AlphaMode::ALPHA_MASK;

                if (!colour_file.empty())
                    mat.m_BaseColor = "textures/" + colour_file + ".png";

                if (!maps.m_Normal.empty())
                    mat.m_Normal = "textures/" + io::SafeName( maps.m_Normal ) + ".png";

                if (!o.m_Textures)
                    mat.m_BaseColor.clear(), mat.m_Normal.clear();

                json::Value list = json::Value::MakeArray();

                for (const auto & t : tx)
                    list.Push( t );

                json::Value j = json::Value::MakeObject();

                j["name"] = mat.m_Name;
                j["textures"] = list;
                j["base_color"] = o.m_Textures ? maps.m_Base : std::string();
                j["opacity"] = o.m_Textures ? maps.m_Opacity : std::string();
                j["base_color_file"] = mat.m_BaseColor.empty() ? std::string() : colour_file;
                j["normal"] = mat.m_Normal.empty() ? std::string() : maps.m_Normal;
                j["normal_file"] = mat.m_Normal.empty() ? std::string() : io::SafeName( maps.m_Normal );
                j["alpha"] = mat.m_Alpha == scene::AlphaMode::ALPHA_MASK ? "mask" : mat.m_Overlay ? "blend" : "opaque";
                j["base_uv"] = mat.m_BaseUv;

                tex_out.Push( std::move( j ) );
            }

            sc.m_Materials.push_back( std::move( mat ) );
        }
        return sc;
    }


    [[nodiscard]] inline Result<void> CmdMeshExport(
        const Args & args
    )
    {
        if (args.m_Pos.empty())
            return Fail( Errc::BAD_ARGUMENT,
                "mesh export <file.wad> [--name EXACT | --id N | --filter TEXT] [--as gltf|glb|fbx] [--lod N|all] "
                "[--variant NAME|N|all] [--all-parts] [--no-textures] [--world] [-o DIR]" );

        const fs::path wp = args.m_Pos[0];

        SMPACK_TRY( in, Load( wp ) );

        if (in.m_Kind != Kind::WAD)
            return Fail( Errc::UNSUPPORTED, "mesh export needs a .wad" );

        SMPACK_TRY( w, wad::Parse( in.m_Data ) );

        std::string fmt = args.GetOr( "--as", "glb" );
        for (auto & c : fmt)
            c = static_cast<char>(std::tolower( static_cast<unsigned char>(c) ));

        if (fmt != "gltf" && fmt != "glb" && fmt != "fbx")
            return Fail( Errc::BAD_ARGUMENT, "'--as' must be gltf, glb or fbx" );

        SceneOptions so;
        const std::string lod_s = args.GetOr( "--lod", "0" );

        if (lod_s == "all")
            so.m_Lod = -1;
        else if (auto v = ParseU64( lod_s ); v && *v < 64)
            so.m_Lod = static_cast<int>( *v );
        else
            return Fail( Errc::BAD_ARGUMENT, "'--lod' must be a number or 'all'" );

        so.m_Textures = !args.Has( "--no-textures" );
        so.m_World = args.Has( "--world" );

        const fs::path out = args.GetOr( "--output", PackBase( wp ) + "_models" );
        const bool js = args.Has( "--json" );

        std::vector<std::string> warnings;
        auto models = FindModels( w, in.m_Data, warnings );

        LodLibrary lib;
        lib.Open( wp.parent_path() );

        std::map<std::string, int> seen;

        for (const auto & m : models)
            if (Selected( m, args ))
                ++seen[m.m_Name];

        json::Value results = json::Value::MakeArray();
        json::Value skipped = json::Value::MakeArray();
        std::size_t done = 0;

        auto skip = [&] ( const ModelRef & m, std::string reason )
        {
            if (!js)
                println( "  {}: skipped, {}", m.m_Name, reason );

            json::Value j = json::Value::MakeObject();
            j["id"] = m.m_Mesh;
            j["name"] = m.m_Name;
            j["reason"] = std::move( reason );

            skipped.Push( std::move( j ) );
        };

        for (const auto & m : models)
        {
            if (!Selected( m, args ))
                continue;

            std::uint64_t v_all = 0, t_all = 0;
            LodStats( m, 0, v_all, t_all, -2 );

            if (t_all == 0)
            {
                // Shadow proxies and collider shells only. Nothing a player ever sees.
                skip( m, "only shadow or helper geometry (never drawn on screen)" );
                continue;
            }

            auto cfg = ResolveConfig( m, args );

            if (!cfg)
                return std::unexpected( cfg.error() );

            so.m_Config = *cfg;

            json::Value mats = json::Value::MakeArray();

            ByteSpan gpu;
            if (m.m_Gpu)
                gpu = wad::ChunkData( in.m_Data, w.m_Entries[*m.m_Gpu] );

            auto sc = BuildScene( m, lib, so, gpu, mats, warnings );
            if (!sc)
            {
                skip( m, sc.error().m_Message );
                continue;
            }

            sc->m_Source = { GameName( wp ), m.m_Mesh, so.m_World };

            const std::string stem = io::SafeName( seen[m.m_Name] > 1 ? std::format( "{}_{}", m.m_Name, m.m_Mesh ) : m.m_Name );
            const fs::path file = out / (stem + "." + fmt);

            SMPACK_TRYV( io::EnsureParent( file ) );

            if (fmt == "fbx") 
                SMPACK_TRYV( scene::WriteFbx( *sc, file ) );
            else
                SMPACK_TRYV( scene::WriteGltf( *sc, file ) );

            std::uint64_t v = 0, t = 0;
            for (const auto & n : sc->m_Nodes)
                for (const auto & s : n.m_Subs)
                    v += s.m_Geo->m_Positions.size() / 3, t += s.m_Geo->m_Indices.size() / 3;

            if (!js) println( "  {} ({} vertices, {} triangles)", file.filename().string(), v, t );

            json::Value j = json::Value::MakeObject();

            j["id"] = m.m_Mesh;
            j["name"] = m.m_Name;
            j["file"] = file.string();
            j["vertices"] = v;
            j["triangles"] = t;
            j["variant"] = m.m_Skeleton && so.m_Config != -2 ? (so.m_Config == -1 ? m.m_Skeleton->DefaultConfig() : so.m_Config) : -1;
            j["materials"] = mats;

            results.Push( std::move( j ) );
            ++done;
        }

        if (js)
        {
            json::Value doc = json::Value::MakeObject();

            doc["smpack"] = "meshexport";
            doc["format"] = fmt;
            doc["output"] = out.string();
            doc["models"] = results;
            doc["skipped"] = skipped;

            json::Value ws = json::Value::MakeArray();

            for (const auto & s : warnings)
                ws.Push( s );

            doc["warnings"] = ws;

            print( "{}", json::Dump( doc ) );
        }
        else
        {
            for (const auto & s : warnings)
                println( stderr, "  ! {}", s );

            println( "exported {} model(s) to {}", done, out.string() );
        }

        if (done == 0 && skipped.AsArray().empty())
            return Fail( Errc::NOT_FOUND, "no model matched the selection" );

        return {};
    }
}