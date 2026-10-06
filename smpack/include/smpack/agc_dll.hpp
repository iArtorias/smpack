// Optional backend. Sony's own 'libSceAgcTextureTool.dll' and 'libSceAgcGpuAddress.dll',
// which ship next to 'GoWR.exe'. Windows only.
// Used for cross checking the native detiler [smpack tex verify] and for surface types the native
// code does not model [3D textures].

#pragma once

#include "smpack/agc.hpp"
#include "smpack/reader.hpp"

#if defined(_WIN32)
#include <filesystem>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace smpack::agc
{
    #if defined(_WIN32)

    class SceDll
    {
    public:

        struct MipInfo
        {
            std::uint32_t m_W, m_H, m_D, m_Pw, m_Ph, m_Pd;
            std::uint64_t m_Off, m_Size;
            std::uint32_t m_Tx, m_Ty;
        };

        struct Summary
        {
            std::uint32_t m_Tile, m_Dim, m_BpeLog2, m_Mem, m_Tpw, m_Tpt, m_Width, m_Height, m_Depth, m_Nfl2, m_NumMips, m_NumSlices;
            std::uint8_t m_Pipe, m_Pad[3];
            std::uint32_t m_Ndf, m_Ncf, m_Bsl2, m_Bs;
            std::uint64_t m_BaseAlign;
            std::uint32_t m_FirstTail, m_Bwl, m_Bhl, m_Bdl, m_Bw, m_Bh, m_Bd;
            std::int32_t m_Nbs;
            std::uint64_t m_Bss, m_Total;
            MipInfo m_Mips[15];
        };

        struct Tex
        {
            std::uint32_t m_Ts[8];
            Summary m_S;
            void * m_El;
            void * m_Md;
        };

        [[nodiscard]] static Result<SceDll> Load( const std::filesystem::path & dir )
        {
            SceDll d;
            const auto tt = dir / "libSceAgcTextureTool.dll";
            const auto ga = dir / "libSceAgcGpuAddress.dll";

            d.m_Ga = LoadLibraryW( ga.wstring().c_str() );
            d.m_Tt = LoadLibraryW( tt.wstring().c_str() );

            if (!d.m_Ga || !d.m_Tt)
            {
                return Fail( Errc::NOT_FOUND, std::format( "cannot load {} / {} [pass --sce-dll <game dir>]", ga.string(), tt.string() ) );
            }

            d.m_Ctor = reinterpret_cast<CtorFn>(reinterpret_cast<void *>(GetProcAddress( d.m_Tt, "??0Texture@Agc@TextureTool@sce@@QEAA@XZ" )));
            d.m_Init = reinterpret_cast<InitFn>(reinterpret_cast<void *>(GetProcAddress(
                d.m_Tt, "?initializeWithTSharp@Texture@Agc@TextureTool@sce@@QEAA?AW4Error@34@PEBU1Core@24@VMemoryRegion@34@@Z" )));
            d.m_Detile = reinterpret_cast<DetileFn>(reinterpret_cast<void *>(GetProcAddress(
                d.m_Ga, "?detileSurface@AgcGpuAddress@sce@@YA?AW4Status@12@PEIAX_KPEIBX1PEBUSurfaceSummary@12@II@Z" )));

            if (!d.m_Ctor || !d.m_Init || !d.m_Detile)
                return Fail( Errc::NOT_FOUND, "Sony dynamic library exports not found" );

            return d;
        }

        SceDll() = default;
        SceDll( SceDll && o ) noexcept
        {
            *this = std::move( o );
        }

        SceDll & operator=( SceDll && o ) noexcept
        {
            std::swap( m_Ga, o.m_Ga );
            std::swap( m_Tt, o.m_Tt );
            m_Ctor = o.m_Ctor;
            m_Init = o.m_Init;
            m_Detile = o.m_Detile;
            return *this;
        }

        ~SceDll()
        {
            if (m_Ga) FreeLibrary( m_Ga );
            if (m_Tt) FreeLibrary( m_Tt );
        }


        // Detile into tight DDS order.
        [[nodiscard]] Result<Bytes> Detile(
            const TSharp & ts,
            ByteSpan surface,
            bool force_linear = false
        ) const
        {
            TSharp src = ts;
            if (force_linear) src.SetTileMode( 0 );
            TSharp lin = ts;
            lin.SetTileMode( 0 );
            Tex t {}, u {};
            m_Ctor( &t );
            m_Ctor( &u );
            MemRegion mr { nullptr, nullptr }, mr2 { nullptr, nullptr };

            if (m_Init( &t, src.m_W, &mr ) != 0 || m_Init( &u, lin.m_W, &mr2 ) != 0)
            {
                return Fail( Errc::UNSUPPORTED, "Sony 'TextureTool' rejected the descriptor" );
            }

            const std::uint32_t bpe = 1u << t.m_S.m_BpeLog2;
            const std::uint32_t mips = t.m_S.m_NumMips, slices = std::max( 1u, t.m_S.m_NumSlices );
            std::vector<std::byte> lin_buf( static_cast<std::size_t>(u.m_S.m_Total) );

            Bytes out;
            std::vector<std::byte> surf( surface.begin(), surface.end() );

            if (surf.size() < t.m_S.m_Total)
                surf.resize( static_cast<std::size_t>(t.m_S.m_Total) );

            for (std::uint32_t s = 0; s < slices; ++s)
            {
                for (std::uint32_t m = 0; m < mips; ++m)
                {
                    const auto & um = u.m_S.m_Mips[m];
                    std::byte * dst = lin_buf.data() + um.m_Off + u.m_S.m_Bss * s;

                    if (m_Detile( dst, um.m_Size, surf.data(), surf.size(), &t.m_S, m, s ) != 0)
                    {
                        return Fail( Errc::INCONSISTENT, std::format( "detileSurface failed for mip {} slice {}", m, s ) );
                    }

                    // 'detileSurface' writes rows tightly [pitch = mip width], which is
                    // also what the game uploads to D3D12.
                    const auto & tm = t.m_S.m_Mips[m];
                    const std::size_t n = static_cast<std::size_t>(tm.m_W) * tm.m_H * tm.m_D * bpe;

                    out.insert( out.end(), dst, dst + n );
                }
            }

            return out;
        }

    private:

        struct MemRegion
        {
            void * m_B;
            void * m_E;
        };

        using CtorFn = void * (__cdecl *)(Tex *);
        using InitFn = int( __cdecl * )(Tex *, const std::uint32_t *, MemRegion *);
        using DetileFn = int( __cdecl * )(void *, std::uint64_t, const void *, std::uint64_t, const Summary *, std::uint32_t, std::uint32_t);
        HMODULE m_Ga = nullptr, m_Tt = nullptr;
        CtorFn m_Ctor = nullptr;
        InitFn m_Init = nullptr;
        DetileFn m_Detile = nullptr;
    };

    inline constexpr bool HAVE_SCE_DLL = true;

    #else

    inline constexpr bool HAVE_SCE_DLL = false;

    #endif
}