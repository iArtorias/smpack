// 'std::print', 'std::println' shim.

#pragma once

#if __has_include(<print>)
#include <print>
namespace smpack::out
{
    using std::print;
    using std::println;
}
#else

#include <cstdio>
#include <format>
#include <string_view>
#include <utility>

namespace smpack::out
{

    template <class... Args>
    void print( std::format_string<Args...> fmt, Args&&... args )
    {
        const std::string s = std::format( fmt, std::forward<Args>( args )... );
        std::fwrite( s.data(), 1, s.size(), stdout );
    }


    template <class... Args>
    void print( std::FILE * f, std::format_string<Args...> fmt, Args&&... args )
    {
        const std::string s = std::format( fmt, std::forward<Args>( args )... );
        std::fwrite( s.data(), 1, s.size(), f );
    }


    template <class... Args>
    void println( std::format_string<Args...> fmt, Args&&... args )
    {
        print( fmt, std::forward<Args>( args )... );
        std::fputc( '\n', stdout );
    }


    template <class... Args>
    void println( std::FILE * f, std::format_string<Args...> fmt, Args&&... args )
    {
        print( f, fmt, std::forward<Args>( args )... );
        std::fputc( '\n', f );

        // Progress or diagnostics must reach a parent process immediately,
        // even when 'stderr' is a pipe and the CRT buffers it.
        if (f == stderr)
            std::fflush( f );
    }


    inline void println()
    {
        std::fputc( '\n', stdout );
    }
}

#endif