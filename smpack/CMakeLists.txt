cmake_minimum_required(VERSION 3.20)
project(smpack LANGUAGES C CXX VERSION 1.3.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# LZ4 is included in 'third_party/lz4' (BSD 2-Clause).
add_library(smpack_lz4 STATIC
    third_party/lz4/lz4.c
    third_party/lz4/lz4hc.c
    third_party/lz4/lz4frame.c
    third_party/lz4/xxhash.c)
target_include_directories(smpack_lz4 PUBLIC third_party/lz4)
set_target_properties(smpack_lz4 PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(smpack_core INTERFACE)
target_include_directories(smpack_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(smpack_core INTERFACE smpack_lz4)

if(MSVC)
    target_compile_options(smpack_core INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    target_compile_definitions(smpack_core INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(smpack_core INTERFACE -Wall -Wextra)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC 13 emits false positives for 'std::variant' at -O2/-O3.
        target_compile_options(smpack_core INTERFACE -Wno-maybe-uninitialized)
    endif()
endif()

add_executable(smpack src/main.cpp)
target_link_libraries(smpack PRIVATE smpack_core)

if(MINGW)
    enable_language(RC)
    target_sources(smpack PRIVATE src/smpack.rc)
    target_link_options(smpack PRIVATE -static)
elseif(MSVC)
    target_sources(smpack PRIVATE src/smpack.manifest)
endif()

enable_testing()
add_executable(smpack_tests tests/tests.cpp)
target_link_libraries(smpack_tests PRIVATE smpack_core)
add_test(NAME unit COMMAND smpack_tests)

option(SMPACK_ASAN "Build with address sanitizers" OFF)
if(SMPACK_ASAN AND NOT MSVC)
    foreach(t smpack smpack_tests)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endforeach()
endif()