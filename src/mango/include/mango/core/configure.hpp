/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2018 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <new>

// -----------------------------------------------------------------------
// platform
// -----------------------------------------------------------------------

#if defined(_XBOX_VER) && (_XBOX_VER < 200)

    // Microsoft XBOX
    #define MANGO_PLATFORM_XBOX
    #define MANGO_PLATFORM_NAME "Xbox"

#elif (defined(_XBOX_VER) && (_XBOX_VER >= 200)) || defined(_XENON)

    // Microsoft XBOX 360
    #define MANGO_PLATFORM_XBOX360
    #define MANGO_PLATFORM_NAME "Xbox 360"

#elif defined(_DURANGO)

    // Microsoft XBOX ONE
    #define MANGO_PLATFORM_XBOXONE
    #define MANGO_PLATFORM_NAME "Xbox One"

#elif defined(__CELLOS_LV2__)

    // SONY Playstation 3
    #define MANGO_PLATFORM_PS3
    #define MANGO_PLATFORM_NAME "Playstation 3"

#elif defined(__ORBIS__)

    // SONY Playstation 4
    #define MANGO_PLATFORM_PS4
    #define MANGO_PLATFORM_NAME "Playstation 4"

#elif defined(_WIN32) || defined(_WINDOWS_)

    // Microsoft Windows
    #define MANGO_PLATFORM_WINDOWS
    #define MANGO_PLATFORM_NAME "Windows"

    #ifndef NOMINMAX
    #define NOMINMAX
    #endif

    #include <windows.h>

#elif defined(__MINGW32__) || defined(__MINGW64__)

    // MinGW
    #define MANGO_PLATFORM_MINGW
    #define MANGO_PLATFORM_WINDOWS
    #define MANGO_PLATFORM_NAME "MinGW"

    #ifndef NOMINMAX
    #define NOMINMAX
    #endif

    #include <windows.h>
    #include <windef.h>

#elif defined(__APPLE__)

    #include <TargetConditionals.h>

    #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR

        // Apple iOS
        #define MANGO_PLATFORM_IOS
        #define MANGO_PLATFORM_UNIX
        #define MANGO_PLATFORM_NAME "iOS"

    #else

        // Apple macOS
        #define MANGO_PLATFORM_OSX
        #define MANGO_PLATFORM_UNIX
        #define MANGO_PLATFORM_NAME "macOS"

    #endif

#elif defined(__ANDROID__)

    // Google Android
    #define MANGO_PLATFORM_ANDROID
    #define MANGO_PLATFORM_UNIX
    #define MANGO_PLATFORM_NAME "Android"

    #include <stdint.h>
    #include <malloc.h>

#elif defined(__linux__)

    // Linux
    #define MANGO_PLATFORM_LINUX
    #define MANGO_PLATFORM_UNIX
    #define MANGO_PLATFORM_NAME "Linux"

    #include <stdint.h>
    #include <malloc.h>

#elif defined(__CYGWIN__)

    // Cygwin
    #define MANGO_PLATFORM_CYGWIN
    #define MANGO_PLATFORM_UNIX
    #define MANGO_PLATFORM_NAME "Cygwin"

    #include <stdint.h>
    #include <malloc.h>

#elif defined(__DragonFly__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)

    // BSD
    #define MANGO_PLATFORM_BSD
    #define MANGO_PLATFORM_UNIX
    #define MANGO_PLATFORM_NAME "BSD"

    #include <inttypes.h>
    #include <malloc.h>

#elif defined(sun) || defined(__sun)

    // SUN
    #define MANGO_PLATFORM_SUN
    #define MANGO_PLATFORM_UNIX
    #define MANGO_PLATFORM_NAME "SUN"

    #include <inttypes.h>
    #include <malloc.h>

#elif defined(__hpux)

    // HPUX
    #define MANGO_PLATFORM_HPUX
    #define MANGO_PLATFORM_UNIX
    #define MANGO_PLATFORM_NAME "HPUX"

    #include <inttypes.h>
    #include <malloc.h>

#elif defined(__sgi) || defined(__sgi__)

    // Silicon Graphics IRIX
    #define MANGO_PLATFORM_IRIX
    #define MANGO_PLATFORM_UNIX
    #define MANGO_PLATFORM_NAME "SGI IRIX"

#else

    // unsupported
    #error "Platform not supported."

#endif

// -----------------------------------------------------------------------
// compiler
// -----------------------------------------------------------------------

#if defined(__INTEL_COMPILER) || defined(__ICL) || defined(__ICC)

    // Intel C/C++ Compiler
    #define MANGO_COMPILER_INTEL

#elif defined(_MSC_VER)

    // Microsoft Visual C++
    #define MANGO_COMPILER_MICROSOFT

    // noexcept specifier support was added in Visual Studio 2015
    #if _MSC_VER < 1900
        #define noexcept
    #endif

    // Fix <cmath> macros
    #ifndef _USE_MATH_DEFINES
        #define _USE_MATH_DEFINES
    #endif

    // SSE2 is always supported on x64
    #if defined(_M_X64) || defined(_M_AMD64)
        #ifndef __SSE2__
        #define __SSE2__
        #endif
    #endif

    // AVX and AVX2 include support for these
    #if defined(__AVX__) || defined(__AVX2__)
        #ifndef __SSE2__
        #define __SSE2__
        #endif

        // 32 it x86 target has limited / broken SSE3..SSE4 support :(
        #if !defined(_M_IX86) && !defined(__i386__)

            #ifndef __SSE3__
            #define __SSE3__
            #endif

            #ifndef __SSSE3__
            #define __SSSE3__
            #endif

            #ifndef __SSE4_1__
            #define __SSE4_1__
            #endif

            #ifndef __SSE4_2__
            #define __SSE4_2__
            #endif

        #endif
    #endif

    #pragma warning(disable : 4146 4996 4201 4244 26812)

#elif defined(__llvm__) || defined(__clang__)

    // LLVM / Clang
    #define MANGO_COMPILER_CLANG

#elif defined(__GNUC__)

    // GNU C/C++ Compiler
    #define MANGO_COMPILER_GCC

    #if __GNUC__ >= 6
        #pragma GCC diagnostic ignored "-Wignored-attributes"
    #endif

#elif defined(__MWERKS__)

    // Metrowerks CodeWarrior

#elif defined(__COMO__)

    // Comeau C++

#else

    // generic

#endif

// -----------------------------------------------------------------------
// CPU
// -----------------------------------------------------------------------

#if defined(__amd64__) || defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)

    // 64 bit Intel
    #define MANGO_CPU_INTEL
    #define MANGO_CPU_64BIT
    #define MANGO_LITTLE_ENDIAN
    #define MANGO_CPU_NAME "x86_64"

#elif defined(_M_IX86) || defined(__i386__)

    // 32 bit Intel
    #define MANGO_CPU_INTEL
    #define MANGO_LITTLE_ENDIAN
    #define MANGO_CPU_NAME "x86"

#elif defined(__ia64__) || defined(__itanium__) || defined(_M_IA64)

    // Intel Itanium (IA-64)
    #define MANGO_CPU_INTEL
    #define MANGO_CPU_64BIT
    #define MANGO_LITTLE_ENDIAN /* bi-endian; depends on OS */
    #define MANGO_CPU_NAME "Itanium"

#elif defined(__aarch64__)

    // 64 bit ARM
    #define MANGO_CPU_ARM
    #define MANGO_CPU_64BIT
    #define MANGO_LITTLE_ENDIAN /* bi-endian; depends on OS */
    #define MANGO_CPU_NAME "ARM64"

#elif defined(__arm__)

    // 32 bit ARM
    #define MANGO_CPU_ARM
    #define MANGO_LITTLE_ENDIAN /* bi-endian; depends on OS */
    #define MANGO_CPU_NAME "ARM"

#elif defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__) || defined(__powerpc64le__) || defined(__ppc64le__) || defined(__PPC64LE__)

    // 64 bit PowerPC
    #define MANGO_CPU_PPC
    #define MANGO_CPU_64BIT

    #if defined(__powerpc64le__) || defined(__ppc64le__) || defined(__PPC64LE__)
        #define MANGO_LITTLE_ENDIAN
    #else
        #define MANGO_BIG_ENDIAN /* bi-endian; depends on OS */
    #endif

    #define MANGO_CPU_NAME "PowerPC"

#elif defined(__POWERPC__) || defined(__ppc__) || defined(__powerpc__) || defined(_M_PPC)

    // 32 bit PowerPC
    #define MANGO_CPU_PPC
    #define MANGO_BIG_ENDIAN /* bi-endian; depends on OS */
    #define MANGO_CPU_NAME "PowerPC"

#elif defined(__m68k__)

    #define MANGO_CPU_M68K
    #define MANGO_BIG_ENDIAN
    #define MANGO_CPU_NAME "Motorola 68k"

#elif defined(__sparc) || defined(sparc)

    // SUN Sparc
    #define MANGO_CPU_SPARC
    #define MANGO_BIG_ENDIAN /* bi-endian; depends on OS */
    #define MANGO_CPU_NAME "Sparc"

#elif defined(__mips__) || defined(__mips64)

    // MIPS
    #define MANGO_CPU_MIPS
    #define MANGO_CPU_NAME "MIPS"

    #if (defined(MIPSEL) || (__MIPSEL__)) && !defined(_MIPSEB)
        #define MANGO_LITTLE_ENDIAN
    #else
        #define MANGO_BIG_ENDIAN
    #endif

    #if (_MIPS_SIM == _ABI64) || defined(__mips64)
        #define MANGO_CPU_64BIT
    #endif

#elif defined(__alpha__) || defined(_M_ALPHA)

    // Alpha
    #define MANGO_CPU_ALPHA
    #define MANGO_BIG_ENDIAN /* bi-endian; depends on OS */
    #define MANGO_CPU_NAME "Alpha"

#else

    // generic CPU
    #define MANGO_CPU_NAME "Generic"

    // last chance to detect endianess
    #include <stdlib.h>

    #if defined (__GLIBC__)
        #include <endian.h>
        #if (__BYTE_ORDER == __BIG_ENDIAN)
            #define MANGO_BIG_ENDIAN
        #else
            #define MANGO_LITTLE_ENDIAN
        #endif
    #else
        #error "CPU endianess not supported."
    #endif

#endif

// last chance to detect a 64 bit processor
#if !defined(MANGO_CPU_64BIT) && (defined(__LP64__) || defined(__MINGW64__))
    #define MANGO_CPU_64BIT
#endif

// compiling for little endian
#if defined(__LITTLE_ENDIAN__) && defined(MANGO_BIG_ENDIAN)
    #undef MANGO_BIG_ENDIAN
    #define MANGO_LITTLE_ENDIAN
#endif

// compiling for big endian
#if defined(__BIG_ENDIAN__) && defined(MANGO_LITTLE_ENDIAN)
    #undef MANGO_LITTLE_ENDIAN
    #define MANGO_BIG_ENDIAN
#endif

// -----------------------------------------------------------------------
// SIMD
// -----------------------------------------------------------------------

#if defined(MANGO_CPU_INTEL)

    #if defined(__AVX512F__) && defined(__AVX512DQ__)
        #define MANGO_ENABLE_AVX512
        #include <immintrin.h>
    #endif

    #ifdef __AVX2__
        #define MANGO_ENABLE_AVX2
        #include <immintrin.h>
    #endif

    #ifdef __AVX__
        #define MANGO_ENABLE_AVX
        #include <immintrin.h>
    #endif

    #ifdef __SSE4_2__
        #define MANGO_ENABLE_SSE4_2
        #include <nmmintrin.h>
    #endif

    #ifdef __SSE4_1__
        #define MANGO_ENABLE_SSE4_1
        #include <smmintrin.h>
    #endif

    #ifdef __SSSE3__
        #define MANGO_ENABLE_SSSE3
        #include <tmmintrin.h>
    #endif

    #ifdef __SSE3__
        #define MANGO_ENABLE_SSE3
        #include <pmmintrin.h>
    #endif

    #ifdef __SSE2__
        #define MANGO_ENABLE_SSE2
        #include <emmintrin.h>
    #endif

    // Intel SSE vector intrinsics
    #define MANGO_ENABLE_SSE
    #include <xmmintrin.h>

    #ifdef __XOP__
        #if defined(MANGO_COMPILER_MICROSOFT)
            #define MANGO_ENABLE_XOP
            #define MANGO_ENABLE_FMA4
            #include <ammintrin.h>
        #elif defined(MANGO_COMPILER_GCC) || defined(MANGO_COMPILER_CLANG)
            #define MANGO_ENABLE_XOP
            #define MANGO_ENABLE_FMA4
            #include <x86intrin.h>
        #endif
    #endif

    #ifdef __F16C__
        #define MANGO_ENABLE_F16C
        #include <immintrin.h>
    #endif

    #ifdef __POPCNT__
        #define MANGO_ENABLE_POPCNT
        #include <immintrin.h>
    #endif

    #ifdef __BMI__
        // NOTE: slow on AMD Zen architecture (emulated in microcode)
        #define MANGO_ENABLE_BMI
        #include <immintrin.h>
    #endif

    #ifdef __BMI2__
        // NOTE: slow on AMD Zen architecture (emulated in microcode)
        #define MANGO_ENABLE_BMI2
        #include <immintrin.h>
    #endif

    #ifdef __LZCNT__
        #define MANGO_ENABLE_LZCNT
        #include <immintrin.h>
    #endif

    #ifdef __AES__
        #define MANGO_ENABLE_AES
        #include <wmmintrin.h>
    #endif

    #ifdef __SHA__
        #define MANGO_ENABLE_SHA
        #include <immintrin.h>
    #endif

    #if defined(__FMA__) && !defined(MANGO_ENABLE_FMA3)
        #define MANGO_ENABLE_FMA3
        #include <immintrin.h>
    #endif

    #if defined(__FMA4__) && !defined(MANGO_ENABLE_FMA4)
        #if defined(MANGO_COMPILER_MICROSOFT)
            #define MANGO_ENABLE_FMA4
            #include <intrin.h>
        #elif defined(MANGO_COMPILER_GCC) || defined(MANGO_COMPILER_CLANG)
            #define MANGO_ENABLE_FMA4
            #include <x86intrin.h>
        #endif
    #endif

#elif defined(MANGO_CPU_ARM)

    #if defined(__ARM_NEON__) || defined(__ARM_NEON) || defined(__ARM_FEATURE_CRYPTO)
        // ARM NEON vector instrinsics
        #define MANGO_ENABLE_NEON

        #ifdef __aarch64__
            #define MANGO_ENABLE_NEON64
        #endif

        #if defined(_M_ARM64)
            #include <arm64_neon.h>
        #else
            #include <arm_neon.h>
        #endif
    #endif

    // ARM FP feature bits
    #if ((__ARM_FP & 0x2) != 0)
        #define MANGO_ENABLE_ARM_FP16
    #endif

    #ifdef __ARM_FEATURE_CRC32
        #include <arm_acle.h>
    #endif

    #ifdef __ARM_FEATURE_CLZ
        #include <arm_acle.h>
    #endif

#elif defined(MANGO_CPU_PPC)

    #if defined(_ARCH_PWR10)

        // VMX x (Power ISA vx.x, 2020)
        #define MANGO_ENABLE_ALTIVEC
        #define MANGO_ENABLE_VSX

    #elif defined(_ARCH_PWR9)

        // VMX 3 (Power ISA v3.0, 2017)
        #define MANGO_ENABLE_ALTIVEC
        #define MANGO_ENABLE_VSX

    #elif defined(_ARCH_PWR8)

        // VMX 2 (Power ISA v2.07, 2014)
        #define MANGO_ENABLE_ALTIVEC
        #define MANGO_ENABLE_VSX

    #elif defined(_ARCH_PWR7)

        // VSX (Power ISA v2.06, 2010)
        #define MANGO_ENABLE_ALTIVEC
        #define MANGO_ENABLE_VSX

    #elif defined(__PPU__) || defined(__SPU__)

        // SONY Playstation 3 SPU / PPU (VMX)

    #elif defined(MANGO_PLATFORM_XBOX360)

        // Microsoft Xbox 360 (VMX128)

    #elif defined(__VEC__)

        // VMX (Power ISA v2.03)
        #define MANGO_ENABLE_ALTIVEC

    #endif

#elif defined(MANGO_CPU_MIPS)

    #if defined(__mips_msa)

        // MIPS SIMD Architecture
        #define MANGO_ENABLE_MSA
        #include <msa.h>

    #endif

#endif

// -----------------------------------------------------------------------
// gcc built-ins
// -----------------------------------------------------------------------

#if defined(MANGO_COMPILER_GCC) || defined(MANGO_COMPILER_CLANG) || defined(MANGO_COMPILER_INTEL)

    #define MANGO_GCC_BUILTINS

#endif

// -----------------------------------------------------------------------
// macros
// -----------------------------------------------------------------------

#if __cplusplus >= 201402L
    // C++14
#endif

#if __cplusplus >= 201703L
    // C++17
#endif

#if __cplusplus >= 202002L
    // C++20
#endif

#if defined(__FAST_MATH__) || defined(_M_FP_FAST)
    #define MANGO_FAST_MATH
#endif

#define MANGO_UNREFERENCED(x) (void) x
#define MANGO_DEFAULT_ALIGNMENT 64

#ifdef MANGO_PLATFORM_WINDOWS

    #define MANGO_ALIGN(...) __declspec(align(__VA_ARGS__))
    #define MANGO_IMPORT __declspec(dllimport)
    #define MANGO_EXPORT __declspec(dllexport)

#elif __GNUC__ >= 4

    #define MANGO_ALIGN(...) __attribute__((aligned(__VA_ARGS__)))
    #define MANGO_IMPORT __attribute__ ((__visibility__ ("default")))
    #define MANGO_EXPORT __attribute__ ((__visibility__ ("default")))

#else

    #define MANGO_ALIGN(...)
    #define MANGO_IMPORT
    #define MANGO_EXPORT

#endif

// -----------------------------------------------------------------------
// integer types
// -----------------------------------------------------------------------

namespace mango
{

    using s8  = std::int8_t;
    using s16 = std::int16_t;
    using s32 = std::int32_t;
    using s64 = std::int64_t;

    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

} // namespace mango
