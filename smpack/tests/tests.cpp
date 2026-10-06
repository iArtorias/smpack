// Self contained unit tests.
//
// Swizzle expectations below were captured from Sony's
// 'libSceAgcGpuAddress.dll' ['computeSurfaceSummary', 'computeTiledElementByteOffset']
// shipped with the game, so they pin the native detiler to the reference.

#include <cstdio>
#include <random>

#include "smpack/agc.hpp"
#include "smpack/animset.hpp"
#include "smpack/audiopack.hpp"
#include "smpack/bootopts.hpp"
#include "smpack/dds.hpp"
#include "smpack/json.hpp"
#include "smpack/lodpack.hpp"
#include "smpack/lz4frame.hpp"
#include "smpack/shaderpack.hpp"
#include "smpack/texpack.hpp"
#include "smpack/wad.hpp"
#include "../src/cmd_mesh_import.hpp"

using namespace smpack;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond)                                                                \
           ++g_pass;                                                             \
        else                                                                     \
        {                                                                        \
            ++g_fail;                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

static Bytes RandomBytes(
    std::size_t n,
    unsigned seed
)
{
    std::mt19937 r( seed );
    Bytes b( n );

    for (auto & x : b)
        x = static_cast<std::byte>(r() & 0xFF);

    return b;
}


static void TestLayoutReference()
{
    // BC1 1704x2048, 12 mips, 'STANDARD_4KB' [texture 18fe3ca88f0ca016 in 170_midgard10_postgame].
    auto l = agc::ComputeLayout( 5, 64, 4, 1704, 2048, 12 );

    CHECK( l.has_value() );
    CHECK( l->m_Total == 2469888 );
    CHECK( l->m_FirstTail == 5 );
    CHECK( l->m_Mips[0].m_Offset == 634880 && l->m_Mips[0].m_Size == 1835008 && l->m_Mips[0].m_Pw == 448 );
    CHECK( l->m_Mips[1].m_Offset == 176128 && l->m_Mips[1].m_Pw == 224 );
    CHECK( l->m_Mips[4].m_Offset == 4096 && l->m_Mips[4].m_Size == 8192 );
    CHECK( l->m_Mips[6].m_TailX == 8 && l->m_Mips[6].m_TailY == 8 );
    CHECK( l->m_Mips[11].m_TailX == 0 && l->m_Mips[11].m_TailY == 4 );

    // 32_32_32_32 33x65, exercises the ceil halving mip chain rule.
    auto m = agc::ComputeLayout( 5, 128, 1, 33, 65, 7 );
    CHECK( m && m->m_Total == 98304 && m->m_Mips[0].m_Offset == 36864 && m->m_Mips[1].m_Pw == 32 && m->m_Mips[1].m_Ph == 48 );
    CHECK( m->m_FirstTail == 3 );

    // 2D array, 3 slices.
    auto a = agc::ComputeLayout( 5, 64, 1, 300, 200, 6, 3 );
    CHECK( a && a->m_SliceSize == 749568 && a->m_Total == 2248704 );

    // 64KB and 256B modes.
    auto b = agc::ComputeLayout( 9, 64, 1, 40, 24, 6 );
    CHECK( b && b->m_Total == 65536 && b->m_Mips[0].m_TailX == 64 && b->m_Mips[5].m_TailX == 8 && b->m_Mips[5].m_TailY == 8 );

    auto c = agc::ComputeLayout( 1, 64, 1, 40, 24, 6 );
    CHECK( c && c->m_Total == 11776 && c->m_Mips[0].m_Offset == 4096 && c->m_Mips[1].m_Pw == 24 );

    // Linear [WAD low mips]. BC1 64x64 7 mips = 8448 bytes, pitch 256 B.
    auto lin = agc::ComputeLayout( 0, 64, 4, 64, 64, 7 );
    CHECK( lin && lin->m_Total == 8448 && lin->m_Mips[0].m_Offset == 4352 && lin->m_Mips[0].m_Pw == 32 );

    auto lin2 = agc::ComputeLayout( 0, 8, 1, 100, 37, 7 );
    CHECK( lin2 && lin2->m_Total == 19712 && lin2->m_Mips[1].m_Ph == 19 );
}


static void TestSwizzleReference()
{
    // Element byte offsets inside one 4 KB block (from the dynamic library).
    auto l8 = agc::ComputeLayout( 5, 64, 1, 32, 16, 1 ); // 8 B/element, block 32x16
    CHECK( l8 && l8->m_BlockW == 32 && l8->m_BlockH == 16 );
    CHECK( l8->m_Xs[1] == 0x8 && l8->m_Xs[2] == 0x40 && l8->m_Xs[4] == 0x80 && l8->m_Xs[8] == 0x200 && l8->m_Xs[16] == 0x800 );
    CHECK( l8->m_Ys[1] == 0x10 && l8->m_Ys[2] == 0x20 && l8->m_Ys[4] == 0x100 && l8->m_Ys[8] == 0x400 );

    auto l1 = agc::ComputeLayout( 5, 8, 1, 64, 64, 1 );
    CHECK( l1 && l1->m_Xs[16] == 0x200 && l1->m_Ys[16] == 0x100 && l1->m_Ys[32] == 0x400 );

    auto l16 = agc::ComputeLayout( 9, 128, 1, 64, 64, 1 );
    CHECK( l16 && l16->m_Xs[1] == 0x40 && l16->m_Xs[32] == 0x8000 && l16->m_Ys[32] == 0x4000 );
}


static void TestTileRoundtrip()
{
    const struct
    {
        std::uint32_t m_Tile, m_Bits, m_Blk, m_W, m_H, m_Mips, m_Slices;
    }

    cases[] = 
    {
        { 5, 64, 4, 1704, 2048, 12, 1 }, { 5, 128, 4, 999, 333, 10, 1 }, { 5, 32, 1, 77, 13, 7, 2 },
        { 9, 64, 1, 130, 70, 8, 1 }, { 1, 16, 1, 40, 24, 6, 1 }, { 0, 64, 4, 64, 64, 7, 1 },
        { 0, 8, 1, 100, 37, 7, 3 },
    };

    for (const auto & c : cases)
    {
        auto l = agc::ComputeLayout( c.m_Tile, c.m_Bits, c.m_Blk, c.m_W, c.m_H, c.m_Mips, c.m_Slices );
        CHECK( l.has_value() );

        if (!l)
            continue;

        const auto img = agc::LinearImageFor( *l );

        Bytes lin = RandomBytes( static_cast<std::size_t>(img.Total()), c.m_W * 31 + c.m_H );
        auto t = agc::Tile( *l, lin );
        CHECK( t && t->size() == l->m_Total );

        auto d = agc::Detile( *l, *t );
        CHECK( d && *d == lin );
    }
}


static void TestTsharp()
{
    // Real T# from a shipped BC1, 2048x2048 texture.
    agc::TSharp t {};
    const std::uint32_t w[8] = { 0, 0xcaa00000, 0x81ffc1ff, 0x905b0fac, 0, 0xb0, 0, 0x2ab000 };
    std::memcpy( t.m_W, w, 32 );

    CHECK( t.Format() == 170 && t.Width() == 2048 && t.Height() == 2048 );
    CHECK( t.NumMips() == 12 && t.TileMode() == 5 && t.Type() == 9 && t.NumSlices() == 1 );
    CHECK( agc::SwizzleStr( t.Swizzle() ) == "RGBA" );

    t.SetSize( 1024, 512 );
    t.SetLastMip( 10 );

    CHECK( t.Width() == 1024 && t.Height() == 512 && t.NumMips() == 11 && t.MaxMip() == 10 );
    CHECK( t.Format() == 170 && t.TileMode() == 5 );
}


static void TestVolumeLayout()
{
    // Real T# of a shipped 3D probe index volume.
    // 20x27x16, 8_8_8_8UNorm, tile 5, stored in the WAD as a linear chunk.
    agc::TSharp t {};
    const std::uint32_t w[8] = { 0, 0xc3800000, 0x80068004, 0xa0500fac, 0xf, 0, 0, 0 };
    std::memcpy( t.m_W, w, 32 );
    CHECK( t.Is3d() && t.Width() == 20 && t.Height() == 27 && t.Depth() == 16 && t.NumMips() == 1 );

    auto l = agc::LayoutFor( t, /*force_linear=*/true );
    CHECK( l.has_value() );

    // Texels are tight [row pitch = width], the chunk keeps the 256 byte pitched size.
    CHECK( l && l->m_Slices == 16 && l->m_SliceSize == 20ull * 27 * 4 && l->m_Total == 64ull * 27 * 16 * 4 );
    CHECK( l && l->ElementOffset( 1, 1, 0, 1 ) == (20ull * 27 + 20 + 1) * 4 );
    CHECK( !agc::LayoutFor( t, false ).has_value() ); // Tiled 3D. Sony library only.

    dds::Image img;
    img.m_Dxgi = 28;
    img.m_Width = 20;
    img.m_Height = 27;
    img.m_Depth = 16;
    img.m_Data.assign( 20ull * 27 * 16 * 4, std::byte { 7 } );
    auto back = dds::Read( dds::Write( img ) );
    CHECK( back && back->m_Depth == 16 && back->m_Slices == 1 && back->m_Data == img.m_Data );
}


static void TestLz4()
{
    Bytes b = RandomBytes( 200000, 7 );

    for (std::size_t i = 0; i < b.size(); i += 3)
        b[i] = std::byte { 0 }; // Make it compressible.

    auto c = lz4::CompressFrame( b, 0 );
    CHECK( c && lz4::LooksLikeFrame( *c ) );
    CHECK( c && static_cast<std::uint8_t>( (*c)[4] ) == 0x6C && static_cast<std::uint8_t>( (*c)[5] ) == 0x40 ); // Shipped FLG, BD.

    auto d = lz4::DecompressFrame( *c );
    CHECK( d && *d == b );

    auto hc = lz4::CompressFrame( b, 9 );
    auto d2 = lz4::DecompressFrame( *hc );
    CHECK( d2 && *d2 == b );
}


static void TestJson()
{
    auto v = json::Parse( R"({"a": [1, 2, {"b": "x\"y"}], "h": "0x00000000deadbeef", "f": 1.5, "t": true, "n": null})" );

    CHECK( v.has_value() );
    CHECK( v->Get( "a" ).AsArray().size() == 3 );
    CHECK( v->Get( "a" ).AsArray()[2].Get( "b" ).AsString() == "x\"y" );
    CHECK( v->Get( "h" ).AsU64() == 0xdeadbeef );
    CHECK( v->Get( "t" ).AsBool() );

    auto again = json::Parse( json::Dump( *v ) );
    CHECK( again && json::Dump( *again ) == json::Dump( *v ) );

    auto bad = json::Parse( "{\"a\": }" );
    CHECK( !bad.has_value() );
}


static void TestBootopts()
{
    auto doc = json::Parse( R"({"allcontentidarray": [{"content-id": "x", "patch-texpacks": [], "patch-lodpacks": []}]})" );
    CHECK( doc.has_value() );

    auto r1 = bootopts::SetPatch( *doc, "patch-texpacks", "modA", true );
    auto r2 = bootopts::SetPatch( *doc, "patch-texpacks", "modB", true );
    auto r3 = bootopts::SetPatch( *doc, "patch-lodpacks", "geoA", true );
    auto r4 = bootopts::SetPatch( *doc, "patch-lodpacks", "geoB", true );
    CHECK( r1 && *r1 && r2 && *r2 && r3 && r4 );

    const auto & e = doc->Get( "allcontentidarray" ).AsArray()[0];
    CHECK( e.Get( "patch-texpacks" ).AsArray()[0].AsString() == "modB" ); // Newest first. First match wins.
    CHECK( e.Get( "patch-lodpacks" ).AsArray()[1].AsString() == "geoB" ); // Newest last. Last registration wins.

    auto dup = bootopts::SetPatch( *doc, "patch-texpacks", "modA", true );
    CHECK( dup && !*dup );

    auto rm = bootopts::SetPatch( *doc, "patch-texpacks", "modA", false );
    CHECK( rm && *rm && e.Get( "patch-texpacks" ).AsArray().size() == 1 );

    auto longname = bootopts::SetPatch( *doc, "patch-texpacks", std::string( 40, 'x' ), true );
    CHECK( !longname.has_value() );
}


static void TestDds()
{
    dds::Image img;
    img.m_Dxgi = agc::dxgi::BC7_UNORM;
    img.m_Width = 100;
    img.m_Height = 60;
    img.m_Mips = 7;
    img.m_Data = RandomBytes( static_cast<std::size_t>(dds::TightSize( img.m_Dxgi, 100, 60, 7, 1 )), 3 );

    auto b = dds::Write( img );
    auto r = dds::Read( b );

    CHECK( r && r->m_Width == 100 && r->m_Height == 60 && r->m_Mips == 7 && r->m_Dxgi == img.m_Dxgi && r->m_Data == img.m_Data );
    CHECK( agc::DxgiCompatible( 182, agc::dxgi::BC7_UNORM ) ); // sRGB game format accepts UNORM DDS.
    CHECK( !agc::DxgiCompatible( 169, agc::dxgi::BC7_UNORM ) );
}


// Build a small but realistic WTOC image by hand.
static Bytes MakeWad( wad::Wad & out_w )
{
    wad::TocHeader h {};
    h.m_Magic = wad::MAGIC;
    h.m_Version = wad::VERSION;

    std::vector<wad::Entry> es;
    std::vector<wad::ChunkPayload> pl;
    auto add = [&]( std::uint16_t id, std::uint8_t mt, std::uint32_t align, std::string_view name, Bytes data, bool eob,
        Bytes temp = {} )
    {
        wad::Entry e {};
        e.m_E.m_Id = id;
        e.m_E.m_MemType = mt;
        e.m_E.m_ChunkAlign = align;
        std::memcpy( e.m_E.m_Name, name.data(), name.size() );
        e.m_E.m_Bits = eob ? 1 : 0;
        es.push_back( e );
        pl.push_back( { std::move( data ), {}, std::move( temp ) } );
    };

    add( 21, 0, 16, "WAD_Test", RandomBytes( 8, 1 ), false );
    add( 1, 0, 8, "CXT_Test", RandomBytes( 156, 2 ), false );
    add( 29, 2, 256, "TX_a", RandomBytes( 8448, 3 ), false );
    add( 1, 8, 16, "TX_a", RandomBytes( 200, 4 ), false, RandomBytes( 40, 9 ) );
    add( 29, 1, 256, "MG_b", RandomBytes( 1000, 5 ), false );
    add( 25, 0, 1, "autopad", {}, true );
    add( 7, 0, 16, "Data", RandomBytes( 33, 6 ), false );
    add( 19, 0, 16, "WAD_Test", {}, true );

    // Give the autopad a length [skipped bytes after its block].
    es[5].m_E.m_Length = 123;
    auto b = wad::Build( h, es, pl );
    CHECK( b.has_value() );

    auto w = wad::Parse( *b );
    CHECK( w.has_value() );

    if (w)
        out_w = *w;

    return b ? *b : Bytes {};
}


static void TestWad()
{
    wad::Wad w;
    Bytes img = MakeWad( w );

    CHECK( w.m_Entries.size() == 8 );
    CHECK( w.m_DataEnd == img.size() );

    // Alignment invariants.
    CHECK( w.m_Entries[2].m_E.m_MemOff % 256 == 0 && w.m_Entries[4].m_E.m_MemOff % 256 == 0 );
    CHECK( w.m_Entries[1].m_E.m_MemOff % 8 == 0 );

    // Payloads read back.
    CHECK( wad::ChunkData( img, w.m_Entries[3] ).size() == 200 );
    CHECK( wad::TempData( img, w.m_Entries[3] ).size() == 40 );

    // Rebuild from parsed data must be byte identical.
    std::vector<wad::ChunkPayload> pl;

    for (const auto & e : w.m_Entries)
    {
        auto d = wad::ChunkData( img, e ), t = wad::TempData( img, e ), g = wad::DebugData( img, e );
        pl.push_back( { Bytes( d.begin(), d.end() ), Bytes( g.begin(), g.end() ), Bytes( t.begin(), t.end() ) } );
    }

    auto again = wad::Build( w.m_Header, w.m_Entries, pl );
    CHECK( again && *again == img );

    // Grow a chunk. Offsets after it must move, and parsing must still succeed.
    pl[2].m_Data.resize( 20000 );
    auto grown = wad::Build( w.m_Header, w.m_Entries, pl );
    CHECK( grown.has_value() );

    auto w2 = wad::Parse( *grown );
    CHECK( w2 && w2->m_Entries[2].m_E.m_Length == 20000 && w2->m_DataEnd == grown->size() );
    CHECK( w2 && w2->m_Header.m_TotalMemByType[2] >= 20000 );

    // Manifest JSON round trip.
    for (const auto & e : w.m_Entries)
    {
        auto te = wad::EntryFromJson( wad::EntryToJson( e ) );
        CHECK( te && std::memcmp( &*te, &e.m_E, offsetof( wad::TocEntry, m_MemOff ) ) == 0 );
    }
}


static void TestTexpackBuild()
{
    // Two textures. BC1 256x128 [9 mips] and BC7 64x64 [7 mips].
    std::vector<texpack::NewTexture> tex;

    for (int k = 0; k < 2; ++k)
    {
        texpack::NewTexture nt;
        nt.m_TexIdentifier = 0x1111 + k;
        nt.m_ContentHash = 0xAAAA0000ull + k;
        agc::TSharp ts {};
        ts.SetFormat( k == 0 ? 170 : 181 );

        const std::uint32_t w = k == 0 ? 256 : 64, h = k == 0 ? 128 : 64;

        ts.SetSize( w, h );
        ts.SetLastMip( static_cast<std::uint32_t>( std::bit_width( std::max( w, h ) ) ) - 1 );
        ts.SetTileMode( 5 );
        ts.m_W[3] |= 9u << 28; // 2D.

        std::memcpy( nt.m_Gnf.data(), "GNF ", 4 );

        auto l = agc::LayoutFor( ts );
        CHECK( l.has_value() );

        nt.m_Surface = RandomBytes( static_cast<std::size_t>(l->m_Total), 11 + k );
        texpack::UpdateGnf( nt.m_Gnf, ts, l->m_Total );
        tex.push_back( std::move( nt ) );
    }

    auto built = texpack::Build( tex );

    CHECK( built.has_value() );

    if (!built)
        return;

    auto pkg = texpack::Parse( built->m_Toc );
    CHECK( pkg && pkg->m_Textures.size() == 2 );

    auto pkg2 = texpack::Parse( built->m_Payload );
    CHECK( pkg2 && pkg2->m_Textures.size() == 2 );

    for (std::size_t i = 0; i < 2; ++i)
    {
        auto at = texpack::Assemble( built->m_Payload, pkg2->m_Textures[i] );
        CHECK( at && at->m_Surface == tex[i].m_Surface );
        CHECK( pkg2->m_Textures[i].MipCount() == at->m_Tsharp.NumMips() );
        CHECK( pkg2->m_Textures[i].Top().m_Info.m_LargestMipWidth == at->m_Tsharp.Width() );
        for (const auto & b : pkg2->m_Textures[i].m_Mips) CHECK( b.Offset() % 16 == 0 && b.m_Info.m_DiskSizeBytes % 16 == 0 );
    }

    // Default partition. Head block holds mips up to index 9.
    CHECK( texpack::DefaultPartition( 12 ).size() == 3 );
    CHECK( texpack::DefaultPartition( 8 ).size() == 1 );
}


static void TestLodpack()
{
    std::vector<lodpack::NewGroup> gs( 2 );
    gs[0].m_Data = RandomBytes( 96, 1 );
    gs[0].m_Blocks = { { 0, 0, 0x30, 64, 0 }, { 0, 64, 0x10, 32, 0 } };
    gs[1].m_Data = RandomBytes( 48, 2 );
    gs[1].m_Blocks = { { 0, 0, 0x20, 48, 0 } };

    auto b = lodpack::Build( gs );
    CHECK( b.has_value() );

    auto p = lodpack::Parse( b->m_Pack );
    CHECK( p && p->m_Groups.size() == 2 && p->m_Blocks.size() == 3 );
    CHECK( p && p->m_Blocks[0].m_BlockDataHash == 0x10 && p->m_Blocks[2].m_BlockDataHash == 0x30 ); // Sorted.
    CHECK( p && p->m_Blocks[1].m_GroupIdx == 1 );
    CHECK( lodpack::LooksLikeToc( b->m_Pack ) );

    gs[1].m_Blocks.push_back( { 0, 0, 0x10, 8, 0 } );
    CHECK( !lodpack::Build( gs ).has_value() ); // Duplicate hash rejected.
}


static void TestAnimset()
{
    std::vector<Bytes> blobs;
    for (int i = 0; i < 3; ++i)
    {
        Bytes b( 0x98 + 8 + 1000u * (i + 1) );

        animset::FileSetHeader h {};
        h.m_StreamFlags = 3;
        h.m_NameHash = 0x100 + i;
        std::snprintf( h.m_Name, sizeof( h.m_Name ), "set%d", i );
        h.m_NumFiles = 1;
        std::memcpy( b.data(), &h, sizeof( h ) );
        blobs.push_back( b );
    }

    auto as = animset::Build( blobs );
    CHECK( as && as->size() % animset::SLOT_ALIGN == 0 );

    auto sets = animset::Parse( *as );
    CHECK( sets && sets->size() == 3 );
    CHECK( sets && (*sets)[1].m_Offset == animset::SLOT_ALIGN && (*sets)[1].Name() == "set1" );
    CHECK( animset::LooksLikeAs( *as ) );
}


static void TestAudiopack()
{
    audiopack::Toc t;
    t.m_Header = { audiopack::MAGIC, 1, 3, 1 };
    t.m_Entries = { { 10, 5, 0 }, { 20, 7, 5 }, { 30, 3, 12 } };

    Bytes part = RandomBytes( 15, 4 );
    std::map<std::uint32_t, Bytes> add;

    add[25] = RandomBytes( 9, 5 );
    add[20] = RandomBytes( 2, 6 );

    auto get = [&] ( std::uint32_t id ) -> Result<Bytes>
    {
        if (auto it = add.find( id ); it != add.end())
            return it->second;

        for (const auto & e : t.m_Entries)
            if (e.m_FileId == id)
                return Bytes( part.begin() + e.m_FileOffset, part.begin() + e.m_FileOffset + e.m_FileSize );

        return Fail( Errc::NOT_FOUND, "x" );
    };

    auto b = audiopack::Build( t, add, get );
    CHECK( b.has_value() );

    auto t2 = audiopack::Parse( b->m_Toc );
    CHECK( t2 && t2->m_Entries.size() == 4 );
    CHECK( t2 && t2->m_Entries[2].m_FileId == 25 && t2->m_Entries[1].m_FileSize == 2 );  // sorted, replaced
}


static void TestShaderpackRebuild()
{
    // Header, 2 records, bins and trailing section.
    shaderpack::Header h {};
    h.m_Magic = shaderpack::MAGIC;
    h.m_Version = shaderpack::EXPECTED_VERSION;
    h.m_NumUniqueShaders = 2;
    h.m_UniqueShaderRecordsOff = 0x80;
    h.m_UniqueShaderBinsOff = 0x80 + 2 * sizeof( shaderpack::ShaderRecord );
    shaderpack::ShaderRecord r[2] {};
    r[0].m_BinSize = 20;
    r[0].m_BinOffset = 0;
    r[1].m_BinSize = 16;
    r[1].m_BinOffset = 32;
    h.m_NumUniqueShaderCompressed = 48;
    h.m_ShaderComboRecordsOff = h.m_UniqueShaderBinsOff + 48;
    h.m_PsoRecordsOff = h.m_RootSignatureRecordsOff = h.m_RootSignatureBinsOff = h.m_ShaderComboRecordsOff;

    ByteWriter w;
    w.Put( h );
    w.Put( r[0] );
    w.Put( r[1] );

    Bytes bins = RandomBytes( 48, 1 );
    std::fill( bins.begin() + 20, bins.begin() + 32, std::byte { 0 } ); // Inter shader padding is zero.
    w.Put( ByteSpan( bins ) );
    w.Put( ByteSpan( RandomBytes( 64, 2 ) ) );

    Bytes img = w.Take();

    auto p = shaderpack::Parse( img );
    CHECK( p.has_value() );

    auto same = shaderpack::Rebuild( img, *p, {} );
    CHECK( same && *same == img );

    auto bigger = shaderpack::Rebuild( img, *p, { { 0, RandomBytes( 100, 3 ) } } );
    auto p2 = shaderpack::Parse( *bigger );

    CHECK( p2 && p2->m_Shaders[0].m_BinSize == 100 && p2->m_Shaders[1].m_BinOffset == 112 );
    CHECK( p2 && p2->m_Header.m_ShaderComboRecordsOff == p->m_Header.m_ShaderComboRecordsOff + 80 );
}


static void TestHalf()
{
    for (float f : {0.0f, 1.0f, -2.5f, 0.333251953125f, 65504.0f, 6.1035156e-05f, 1e-6f})
        CHECK( std::abs( mesh::detail::HalfToFloat( mesh::detail::FloatToHalf( f ) ) - f ) <= std::abs( f ) * 1e-3f + 1e-7f );

    CHECK( mesh::detail::FloatToHalf( 1e9f ) == 0x7C00 );
}


static void TestElementRoundtrip()
{
    using mesh::VFormat;
    const float in[4] = { 0.25f, -0.5f, 0.75f, 1.0f };

    for (auto f : { VFormat::F32, VFormat::F16, VFormat::S16N, VFormat::S8N })
    {
        std::byte buf[16] {};
        float out[4] = {};
        mesh::detail::WriteElement( buf, std::uint8_t( f ), 4, in );
        mesh::detail::ReadElement( buf, std::uint8_t( f ), 4, out );

        for (int i = 0; i < 4; ++i)
            CHECK( std::abs( out[i] - in[i] ) < 0.01f );
    }

    const float un[4] = { 0.0f, 0.5f, 1.0f, 1.0f };
    for (auto f : { VFormat::U16N, VFormat::U8N, VFormat::R10G10B10A2 })
    {
        std::byte buf[16] {};
        float out[4] = {};
        mesh::detail::WriteElement( buf, std::uint8_t( f ), 4, un );
        mesh::detail::ReadElement( buf, std::uint8_t( f ), 4, out );

        for (int i = 0; i < 4; ++i)
            CHECK( std::abs( out[i] - un[i] ) < 0.01f );
    }
}


// One skinned vertex through 'Encoder::WriteSkin' and back through 'mesh::decode'.
static void CheckSkin(
    std::uint8_t influences,
    mesh::VertexElement ji,
    mesh::VertexElement jw 
)
{
    mesh::Prim pr;
    pr.m_VertexCount = 1;
    pr.m_PrimitiveCount = 1;
    pr.m_Influences = influences;
    pr.m_Elements = { mesh::VertexElement { 0, 0, 3, 0, 0, 0, 0 }, ji, jw };
    pr.m_StreamOffsets = { 0, 64, 128 };
    pr.m_IndexOffset = 192;

    Bytes buf( 256 );
    std::vector<std::string> warn;
    cli::mesh_import::Encoder enc( pr, {}, nullptr, nullptr, warn, "test" );
    const std::uint16_t joints[4] = { 300, 7, 300, 42 };
    const float weights[4] = { 0.5f, 0.2f, 0.1f, 0.2f };
    bool clamped = false;
    enc.WriteSkin( ji, &jw, buf.data() + 64 + ji.m_Offset, buf.data() + 128 + jw.m_Offset, joints, weights, clamped );
    auto g = mesh::Decode( pr, buf );

    CHECK( g.has_value() );

    if (!g)
        return;

    std::map<int, float> got;

    for (int k = 0; k < 4; ++k)
        if (g->m_Weights[k] > 0)
            got[g->m_Joints[k]] += g->m_Weights[k];

    const bool idx8 = ji.m_Format == std::uint8_t( mesh::VFormat::U8 );
    const int big = idx8 ? 0 : 300; // 8 bit indices fall back to an ancestor [none here, joint 0].

    CHECK( std::abs( got[big] - 0.6f ) < 0.01f );
    CHECK( std::abs( got[7] - 0.2f ) < 0.01f );
    CHECK( std::abs( got[42] - 0.2f ) < 0.01f );
    CHECK( clamped == idx8 );
}


static void TestSkinPacking()
{
    using mesh::VFormat;

    // k4. u16 indices, 'R10G10B10A2' weights [fourth implied].
    CheckSkin( 4, { 9, std::uint8_t( VFormat::U16 ), 4, 0, 1, 0, 0 }, { 10, std::uint8_t( VFormat::R10G10B10A2 ), 1, 0, 2, 0, 0 } );

    // k4. u8 indices, u8n weights stored in full.
    CheckSkin( 4, { 9, std::uint8_t( VFormat::U8 ), 4, 0, 1, 0, 0 }, { 10, std::uint8_t( VFormat::U8N ), 4, 0, 2, 0, 0 } );

    // k7 and k10. packed u32 words.
    CheckSkin( 7, { 9, std::uint8_t( VFormat::U32 ), 4, 0, 1, 0, 0 }, { 10, std::uint8_t( VFormat::U32 ), 2, 0, 2, 0, 0 } );
    CheckSkin( 10, { 9, std::uint8_t( VFormat::U32 ), 4, 0, 1, 0, 0 }, { 10, std::uint8_t( VFormat::U32 ), 3, 0, 2, 0, 0 } );
}


static void TestSkeletonVisibility()
{
    mesh::Skeleton sk;
    sk.m_Parents = { -1, 0, 1, 0, 3 };
    sk.m_Flags = { 0, mesh::JOINT_HIDDEN, 0, mesh::JOINT_VISIBILITY_ANIMATED, 0 };

    auto v = sk.Visibility( -1 );
    CHECK( v[0] && !v[1] && !v[2] && v[3] && v[4] ); // Hidden propagates to children.

    sk.m_ConfigJoints = { 3 };
    sk.m_Configs = { { "Helwalker00_decap", { false } }, { "Helwalker00", { true } } };

    CHECK( sk.DefaultConfig() == 1 );
    v = sk.Visibility( 0 );

    CHECK( !v[3] && !v[4] );
    CHECK( sk.Visibility( 0, true )[2] );
}


static void TestDominantPatches()
{
    // Two triangles sharing an edge, with a UV seam. Vertices 1, 3 and 2, 4 are at the same positions.
    cli::mesh_import::NewMesh g;
    g.m_Pos = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0 };
    g.m_Idx = { 0, 1, 2, 4, 3, 5 };

    auto p = cli::mesh_import::Encoder::DominantPatches( g );
    CHECK( p.size() == 24 );

    // Second triangle, corner 0 [vertex 4]. Itself, then the first triangle's copies of the shared edge.
    CHECK( p[12] == 4 && p[13] == 2 && p[15] == 2 );
    CHECK( p[16] == 3 && p[19] == 1 );
}


static void TestGltfTrs()
{
    const double t[3] = { 1, 2, 3 }, q[4] = { 0, 0.7071067811865476, 0, 0.7071067811865476 }, s[3] = { 2, 2, 2 };
    const auto m = gltf::detail::Trs( t, q, s );
    const float v[3] = { 1, 0, 0 };
    const auto r = cli::mesh_import::XformPoint( m, v ); // +X rotated 90 degrees about +Y is -Z.

    CHECK( std::abs( r[0] - 1 ) < 1e-5f && std::abs( r[1] - 2 ) < 1e-5f && std::abs( r[2] - 1 ) < 1e-5f );
}


int main()
{
    TestLayoutReference();
    TestSwizzleReference();
    TestTileRoundtrip();
    TestTsharp();
    TestVolumeLayout();
    TestLz4();
    TestJson();
    TestBootopts();
    TestDds();
    TestWad();
    TestTexpackBuild();
    TestLodpack();
    TestAnimset();
    TestAudiopack();
    TestShaderpackRebuild();
    TestHalf();
    TestElementRoundtrip();
    TestSkinPacking();
    TestSkeletonVisibility();
    TestDominantPatches();
    TestGltfTrs();

    std::printf( "%d passed, %d failed\n", g_pass, g_fail );
    return g_fail ? 1 : 0;
}