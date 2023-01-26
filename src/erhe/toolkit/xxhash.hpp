// https://github.com/Cyan4973/xxHash/issues/496#issuecomment-772434891

// C++20 Compile-time XXH32()
//
// g++-10 (10.2.0)          g++-10 -std=c++20 -DINCLUDE_XXHASH ./cxx20_ct_xxhash.cpp
// Clang (10.0.0)           clang  -std=c++20 -DINCLUDE_XXHASH ./cxx20_ct_xxhash.cpp
// Visual C++ 2019 (19.28)  cl /std:c++latest /DINCLUDE_XXHASH .\cxx20_ct_xxhash.cpp
//
// Result at Compiler Explorer
// https://godbolt.org/z/bGv7Mh

#pragma once

#include <cstdio>  // printf()
#include <cstdint> // uint8_t, uint32_t
#include <limits.h>
#include <limits>

#if !defined(__GNUC__) || (__GNUC__ > 8)
#   include <bit>      // std::rotl() ( https://en.cppreference.com/w/cpp/numeric/rotl )
#endif

// "private" utility functions
namespace compiletime_xxhash::detail {

#if !defined(__GNUC__) || (__GNUC__ > 8)
    constexpr std::uint32_t rotl(std::uint32_t n, unsigned int c)
    {
        return std::rotl(n, c);
    }
#else
    constexpr std::uint32_t rotl(std::uint32_t n, unsigned int c)
    {
        const unsigned int mask = (CHAR_BIT * sizeof(n) - 1);  // assumes width is a power of 2.

        // assert ( (c<=mask) &&"rotate by type width or more");
        c &= mask;
        return (n << c) | (n >> ((-c) & mask));
    }
#endif

    constexpr uint8_t read_u8(const char* input, int pos) {
        return static_cast<uint8_t>(input[pos]);
    }

    constexpr uint32_t read_u32le(const char* input, int pos) {
        const uint32_t b0 = read_u8(input, pos + 0);
        const uint32_t b1 = read_u8(input, pos + 1);
        const uint32_t b2 = read_u8(input, pos + 2);
        const uint32_t b3 = read_u8(input, pos + 3);
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
}

// "private" XXH32 functions
namespace compiletime_xxhash::detail::xxh32 {
    constexpr uint32_t prime32_1 = 0x9E3779B1U;
    constexpr uint32_t prime32_2 = 0x85EBCA77U;
    constexpr uint32_t prime32_3 = 0xC2B2AE3DU;
    constexpr uint32_t prime32_4 = 0x27D4EB2FU;
    constexpr uint32_t prime32_5 = 0x165667B1U;

    constexpr uint32_t xxh32_avalanche(uint32_t h32) {
        h32 ^= h32 >> 15;
        h32 *= prime32_2;
        h32 ^= h32 >> 13;
        h32 *= prime32_3;
        h32 ^= h32 >> 16;
        return h32;
    }

    constexpr uint32_t xxh32_finalize(const char* input, int inputLen, int pos, uint32_t h32) {
        // XXH_PROCESS4
        while ((inputLen - pos) >= 4)
        {
            h32 += read_u32le(input, pos) * prime32_3;
            h32 = rotl(h32, 17) * prime32_4;
            pos += 4;
        }
        // XXH_PROCESS1
        while ((inputLen - pos) > 0)
        {
            h32 += read_u8(input, pos) * prime32_5;
            h32 = rotl(h32, 11) * prime32_1;
            pos += 1;
        }
        return h32;
    }

    constexpr uint32_t xxh32_digest(
        const char* input,
        int         inputLen,
        int         pos,
        uint32_t    v1,
        uint32_t    v2,
        uint32_t    v3,
        uint32_t    v4
    )
    {
        uint32_t h32 = 0;
        if (inputLen >= 16)
        {
            h32 = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
        }
        else
        {
            h32 = v3 + prime32_5;
        }
        h32 += inputLen;
        h32 = xxh32_finalize(input, inputLen, pos, h32);
        return xxh32_avalanche(h32);
    }

    constexpr uint32_t xxh32_round(uint32_t acc, const char* input, const int pos)
    {
        const uint32_t d = read_u32le(input, pos);
        acc += d * prime32_2;
        acc = rotl(acc, 13) * prime32_1;
        return acc;
    }

    constexpr uint32_t xxh32(const char* input, int inputLen, const uint32_t seed)
    {
        uint32_t v1 = seed + prime32_1 + prime32_2;
        uint32_t v2 = seed + prime32_2;
        uint32_t v3 = seed;
        uint32_t v4 = seed - prime32_1;
        int pos = 0;
        while(pos+16 <= inputLen) {
            v1 = xxh32_round(v1, input, pos + 0*4);
            v2 = xxh32_round(v2, input, pos + 1*4);
            v3 = xxh32_round(v3, input, pos + 2*4);
            v4 = xxh32_round(v4, input, pos + 3*4);
            pos += 16;
        }
        return xxh32_digest(input, inputLen, pos, v1, v2, v3, v4);
    }
}

// "public" function
namespace compiletime_xxhash {
    constexpr uint32_t xxh32(const char* input, const std::size_t inputLen, const uint32_t seed)
    {
        return detail::xxh32::xxh32(input, static_cast<int>(inputLen), seed);
    }
}

constexpr int compiletime_strlen(const char* input)
{
    int i = 0;
    while (input[i] != 0)
    {
        ++i;
    }
    return i;
}

template<int N>
constexpr uint32_t ERHE_HASH(const char(&s)[N])
{
    return compiletime_xxhash::detail::xxh32::xxh32(s, static_cast<int>(N - 1), {});
}