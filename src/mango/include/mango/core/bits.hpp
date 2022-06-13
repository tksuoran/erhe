/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <cmath>
#include <cassert>
#include <algorithm>
#include <mango/core/configure.hpp>
#include <mango/core/half.hpp>

namespace mango
{

    // ----------------------------------------------------------------------------
    // configure built-ins
    // ----------------------------------------------------------------------------

#if defined(MANGO_COMPILER_GCC) || defined(MANGO_COMPILER_CLANG) || defined(MANGO_COMPILER_INTEL)

    #define MANGO_BITS_GCC_BUILTINS

#endif

    // ----------------------------------------------------------------------------
    // byteswap
    // ----------------------------------------------------------------------------

#if defined(MANGO_COMPILER_MICROSOFT)

    static inline u16 byteswap(u16 v)
    {
        return _byteswap_ushort(v);
    }

    static inline u32 byteswap(u32 v)
    {
        return _byteswap_ulong(v);
    }

    static inline u64 byteswap(u64 v)
    {
        return _byteswap_uint64(v);
    }

#elif defined(MANGO_COMPILER_GCC) || defined(MANGO_COMPILER_CLANG) || defined(MANGO_COMPILER_INTEL)

    // GCC / CLANG intrinsics

    static inline u16 byteswap(u16 v)
    {
        return __builtin_bswap32(v << 16);
    }

    static inline u32 byteswap(u32 v)
    {
        return __builtin_bswap32(v);
    }

    static inline u64 byteswap(u64 v)
    {
        return __builtin_bswap64(v);
    }

#else

    // Generic implementation

    // These idioms are often recognized by compilers and result in bswap instruction being generated
    // but cannot be guaranteed so compiler intrinsics above are preferred when available.

    static inline u16 byteswap(u16 v)
    {
        return u16((v << 8) | (v >> 8));
    }

    static inline u32 byteswap(u32 v)
    {
        return (v >> 24) | ((v >> 8) & 0x0000ff00) | ((v << 8) & 0x00ff0000) | (v << 24);
    }

    static inline u64 byteswap(u64 v)
    {
        v = (v >> 32) | (v << 32);
        v = ((v & 0xffff0000ffff0000ull) >> 16) | ((v << 16) & 0xffff0000ffff0000ull);
        v = ((v & 0xff00ff00ff00ff00ull) >>  8) | ((v <<  8) & 0xff00ff00ff00ff00ull);
        return v;
    }

#endif

    static inline Half byteswap(Half v)
    {
        v.u = byteswap(v.u);
        return v;
    }

    static inline Float byteswap(Float v)
    {
        v.u = byteswap(v.u);
        return v;
    }

    static inline Double byteswap(Double v)
    {
        v.u = byteswap(v.u);
        return v;
    }

    // --------------------------------------------------------------
    // unsigned mask builders
    // --------------------------------------------------------------

    constexpr u8 u8_mask(u8 c0, u8 c1, u8 c2, u8 c3) noexcept
    {
        return (c3 << 6) | (c2 << 4) | (c1 << 2) | c0;
    }

    constexpr u16 u16_mask(char c0, char c1) noexcept
    {
        return (c1 << 8) | c0;
    }

    constexpr u16 u16_mask_rev(char c0, char c1) noexcept
    {
        return (c0 << 8) | c1;
    }

    constexpr u32 u32_mask(char c0, char c1, char c2, char c3) noexcept
    {
        return (c3 << 24) | (c2 << 16) | (c1 << 8) | c0;
    }

    constexpr u32 u32_mask_rev(char c0, char c1, char c2, char c3) noexcept
    {
        return (c0 << 24) | (c1 << 16) | (c2 << 8) | c3;
    }

    // ----------------------------------------------------------------------------
    // misc
    // ----------------------------------------------------------------------------

    template <typename D, typename S>
    D reinterpret_bits(S src) noexcept
    {
        static_assert(sizeof(D) == sizeof(S), "Incompatible types.");
        D dest;
        std::memcpy(&dest, &src, sizeof(S));
        return dest;
    }

#if defined(MANGO_COMPILER_MICROSOFT) || defined(MANGO_COMPILER_CLANG) || defined(MANGO_COMPILER_INTEL)

    static inline
    u32 byteclamp(s32 value)
    {
        return std::max(0, std::min(255, value));
    }

#elif defined(MANGO_COMPILER_GCC)

    static inline
    u32 byteclamp(s32 value)
    {
        return std::clamp(value, 0, 255);
    }

#else

    static inline
    u32 byteclamp(s32 value)
    {
        return u32(value & 0xffffff00 ? (((~value) >> 31) & 0xff) : value);
    }

#endif

    constexpr int modulo(int value, int range)
    {
        const int remainder = value % range;
        return remainder < 0 ? remainder + range : remainder;
    }

    constexpr int floor_div(int value, int multiple)
    {
        return value / multiple;
    }

    constexpr int ceil_div(int value, int multiple)
    {
        return (value + multiple - 1) / multiple;
    }

    static inline float snap(float value, float grid)
    {
        assert(grid != 0);
        return std::floor(0.5f + value / grid) * grid;
    }

    constexpr u32 wang_hash(u32 seed)
    {
        seed = (seed ^ 61) ^ (seed >> 16);
        seed *= 9;
        seed = seed ^ (seed >> 4);
        seed *= 0x27d4eb2d;
        seed = seed ^ (seed >> 15);
        return seed;
    }

    // ----------------------------------------------------------------------------
    // scale / extend
    // ----------------------------------------------------------------------------

    constexpr u16 u16_scale(u16 value, int from, int to)
    {
        // scale value "from" bits "to" bits
        return value * ((1 << to) - 1) / ((1 << from) - 1);
    }

    constexpr u32 u32_scale(u32 value, int from, int to)
    {
        // scale value "from" bits "to" bits
        return value * ((1u << to) - 1) / ((1u << from) - 1);
    }

    constexpr u64 u64_scale(u64 value, int from, int to)
    {
        // scale value "from" bits "to" bits
        return value * ((1ull << to) - 1) / ((1ull << from) - 1);
    }

    constexpr u16 u16_extend(u16 value, int from, int to)
    {
        // bit-pattern replicating scaling (can at most double the bits)
        return (value << (to - from)) | (value >> (from * 2 - to));
    }

    constexpr u32 u32_extend(u32 value, int from, int to)
    {
        // bit-pattern replicating scaling (can at most double the bits)
        return (value << (to - from)) | (value >> (from * 2 - to));
    }

    constexpr u64 u64_extend(u64 value, int from, int to)
    {
        // bit-pattern replicating scaling (can at most double the bits)
        return (value << (to - from)) | (value >> (from * 2 - to));
    }

    constexpr s16 s16_extend(s16 value, int bits)
    {
        // sign-extend to 16 bits
        u16 mask = 1 << (bits - 1);
        return (value ^ mask) - mask;
    }

    constexpr s32 s32_extend(s32 value, int bits)
    {
        // sign-extend to 32 bits
        u32 mask = 1u << (bits - 1);
        return (value ^ mask) - mask;
    }

    constexpr s64 s64_extend(s64 value, int bits)
    {
        // sign-extend to 64 bits
        u64 mask = 1ull << (bits - 1);
        return (value ^ mask) - mask;
    }

    // ----------------------------------------------------------------------------
    // 32 bits
    // ----------------------------------------------------------------------------

#ifdef MANGO_ENABLE_BMI

    static inline u32 u32_extract_lsb(u32 value)
    {
        // value:  xxxxxx100000
        // result: 000000100000
        return _blsi_u32(value);
    }

    static inline u32 u32_clear_lsb(u32 value)
    {
        // value:  xxxxxx100000
        // result: xxxxxx000000
        return _blsr_u32(value);
    }

    static inline u32 u32_mask_lsb(u32 value)
    {
        // value:  xxxxxx100000
        // result: 000000111111

        // NOTE: 0 expands to 0xffffffff
        return _blsmsk_u32(value);
    }

#else

    constexpr u32 u32_extract_lsb(u32 value)
    {
        // value:  xxxxxx100000
        // result: 000000100000
        return value & (0 - value);
    }

    constexpr u32 u32_clear_lsb(u32 value)
    {
        // value:  xxxxxx100000
        // result: xxxxxx000000
        return value & (value - 1);
    }

    constexpr u32 u32_mask_lsb(u32 value)
    {
        // value:  xxxxxx100000
        // result: 000000111111

        // NOTE: 0 expands to 0xffffffff
        return value ^ (value - 1);
    }

#endif

    constexpr u32 u32_mask_without_lsb(u32 value)
    {
        // value:  xxxxxx100000
        // result: 000000011111

        // NOTE: 0 expands to 0xffffffff
        return ~(value | (0 - value));
    }

    constexpr u32 u32_extend_lsb(u32 value)
    {
        // value:  xxxxxxxx100
        // result: 11111111100
        return value | (0 - value);
    }

    constexpr u32 u32_extend_without_lsb(u32 value)
    {
        // value:  xxxxxxxx100
        // result: 11111111000
        return value ^ (0 - value);
    }

#ifdef MANGO_ENABLE_BMI

    static inline int u32_tzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        return _tzcnt_u32(value);
    }

#elif defined(__aarch64__)

    static inline int u32_tzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        value = __rbit(value);
        return __clz(value);
    }

#elif defined(MANGO_BITS_GCC_BUILTINS)

    static inline int u32_tzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        return __builtin_ctz(value);
    }

#else

    static inline int u32_tzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        const u32 lsb = u32_extract_lsb(value);
        static const u8 table [] =
        {
            0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
            31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
        };
        return table[(lsb * 0x077cb531) >> 27];
    }

#endif

    static inline int u32_index_of_lsb(u32 value)
    {
        // NOTE: value 0 is undefined
        return u32_tzcnt(value);
    }

#ifdef MANGO_ENABLE_LZCNT

    static inline u32 u32_mask_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000111111111
        u32 mask = 1u << (31 - _lzcnt_u32(value));
        return mask | (mask - 1);
    }

    static inline u32 u32_extract_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        return 1u << (31 - _lzcnt_u32(value));
    }

    static inline int u32_index_of_msb(u32 value)
    {
        // NOTE: value 0 is undefined
        return int(31 - _lzcnt_u32(value));
    }

    static inline int u32_lzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        return int(_lzcnt_u32(value));
    }

#elif defined(__ARM_FEATURE_CLZ)

    static inline u32 u32_mask_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000111111111
        u32 mask = 1u << (31 - __clz(value));
        return mask | (mask - 1);
    }

    static inline u32 u32_extract_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        return 1u << (31 - __clz(value));
    }

    static inline int u32_index_of_msb(u32 value)
    {
        // NOTE: value 0 is undefined
        return int(31 - __clz(value));
    }

    static inline int u32_lzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        return int(__clz(value));
    }

#elif defined(MANGO_BITS_GCC_BUILTINS)

    static inline u32 u32_mask_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000111111111
        u32 mask = 1u << (31 - __builtin_clz(value));
        return mask | (mask - 1);
    }

    static inline u32 u32_extract_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        return 1u << (31 - __builtin_clz(value));
    }

    static inline int u32_index_of_msb(u32 value)
    {
        // NOTE: value 0 is undefined
        return int(31 - __builtin_clz(value));
    }

    static inline int u32_lzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        return int(__builtin_clz(value));
    }

#else

    static inline u32 u32_mask_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000111111111
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        return value;
    }

    static inline u32 u32_extract_msb(u32 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        value = u32_mask_msb(value);
        return value ^ (value >> 1);
    }

    static inline int u32_index_of_msb(u32 value)
    {
        // NOTE: value 0 is undefined
        /* NOTE: This generates branchless code
        int base = 0;
        u32 temp;
        temp = value & 0xffff0000; if (temp) { base |= 16; value = temp; }
        temp = value & 0xff00ff00; if (temp) { base |= 8;  value = temp; }
        temp = value & 0xf0f0f0f0; if (temp) { base |= 4;  value = temp; }
        temp = value & 0xcccccccc; if (temp) { base |= 2;  value = temp; }
        temp = value & 0xaaaaaaaa; if (temp) { base |= 1; }
        return base;
        */
        const u32 mask = u32_mask_msb(value);
        static const u8 table [] =
        {
            0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30,
            8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31
        };
        return table[(mask * 0x07c4acdd) >> 27];
    }

    static inline int u32_lzcnt(u32 value)
    {
        // NOTE: value 0 is undefined
        const u32 mask = u32_mask_msb(value);
        static const u8 table [] =
        {
            31, 22, 30, 21, 18, 10, 29, 2, 20, 17, 15, 13, 9, 6, 28, 1,
            23, 19, 11, 3, 16, 14, 7, 24, 12, 4, 8, 25, 5, 26, 27, 0
        };
        return table[(mask * 0x07c4acdd) >> 27];
    }

#endif

    static inline int u32_log2(u32 value)
    {
        // NOTE: value 0 is undefined
        return u32_index_of_msb(value);
    }

#if defined(__aarch64__)

    static inline u32 u32_reverse_bits(u32 value)
    {
        return __rbit(value);
    }

#else

    static inline u32 u32_reverse_bits(u32 value)
    {
        value = ((value >> 1) & 0x55555555) | ((value << 1) & 0xaaaaaaaa);
        value = ((value >> 2) & 0x33333333) | ((value << 2) & 0xcccccccc);
        value = ((value >> 4) & 0x0f0f0f0f) | ((value << 4) & 0xf0f0f0f0);
        value = ((value >> 8) & 0x00ff00ff) | ((value << 8) & 0xff00ff00);
        value = (value >> 16) | (value << 16);
        return value;
    }

#endif

#if defined(MANGO_ENABLE_POPCNT)

    static inline int u32_count_bits(u32 value)
    {
        return _mm_popcnt_u32(value);
    }

#elif defined(MANGO_BITS_GCC_BUILTINS)

    static inline int u32_count_bits(u32 value)
    {
        return __builtin_popcountl(value);
    }

#elif defined(__aarch64__)

    static inline int u32_count_bits(u32 value)
    {
        uint8x8_t count8x8 = vcnt_u8(vcreate_u8(u64(value)));
        return int(vaddlv_u8(count8x8));
    }

#else

    static inline int u32_count_bits(u32 value)
    {
        value -= (value >> 1) & 0x55555555;
        value = (value & 0x33333333) + ((value >> 2) & 0x33333333);
        value = (value + (value >> 4)) & 0x0f0f0f0f;
        return int((value * 0x01010101) >> 24);
    }

#endif

#ifdef MANGO_ENABLE_BMI

    static inline u32 u32_extract_bits(u32 value, int offset, int size)
    {
        return _bextr_u32(value, offset, size);
    }

#else

    constexpr u32 u32_extract_bits(u32 value, int offset, int size)
    {
        return (value >> offset) & ((1 << size) - 1);
    }

#endif

#ifdef MANGO_ENABLE_BMI2

    // The BMI2 bit interleaving is FAST on Intel CPUs, but emulated in microcode on AMD Zen
    // and should be avoided at ANY cost.

    static inline
    u32 u32_interleave_bits(u32 x, u32 y)
    {
        //     x: ----------------xxxxxxxxxxxxxxxx
        //     y: ----------------yyyyyyyyyyyyyyyy
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx
        u32 value = _pdep_u32(y, 0xaaaaaaaa) |
                    _pdep_u32(x, 0x55555555);
        return value;
    }

    static inline
    void u32_deinterleave_bits(u32& x, u32& y, u32 value)
    {
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx
        //     x: 0000000000000000xxxxxxxxxxxxxxxx
        //     y: 0000000000000000yyyyyyyyyyyyyyyy
        x = _pext_u32(value, 0x55555555);
        y = _pext_u32(value, 0xaaaaaaaa);
    }

#else

#if defined(__ARM_FEATURE_CRYPTO)

    static inline
    u32 u32_interleave_bits(u32 x, u32 y)
    {
        poly64_t a = x;
        poly64_t b = y;
        a = vmull_p64(a, a);
        b = vmull_p64(b, b);
        return u32(a | (b << 1));
    }

#elif defined(__PCLMUL__) && defined(MANGO_ENABLE_SSE4_2)

    static inline
    u32 u32_interleave_bits(u32 x, u32 y)
    {
        __m128i value = _mm_set_epi64x(x, y);
        __m128i a = _mm_clmulepi64_si128(value, value, 0x11);
        __m128i b = _mm_clmulepi64_si128(value, value, 0x00);
        return u32(_mm_extract_epi64(a, 0) | (_mm_extract_epi64(b, 0) << 1));
    }

#else

    static inline
    u32 u32_interleave_bits(u32 x, u32 y)
    {
        //     x: ----------------xxxxxxxxxxxxxxxx
        //     y: ----------------yyyyyyyyyyyyyyyy
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx
        u64 value = ((u64(y) << 32) | x) & 0x0000ffff0000ffff;
        value = (value | (value << 8)) & 0x00ff00ff00ff00ff;
        value = (value | (value << 4)) & 0x0f0f0f0f0f0f0f0f;
        value = (value | (value << 2)) & 0x3333333333333333;
        value = (value | (value << 1)) & 0x5555555555555555;
        return u32((value >> 31) | value);
    }

#endif // defined(__PCLMUL__) && defined(MANGO_ENABLE_SSE4_2)

    static inline
    void u32_deinterleave_bits(u32& x, u32& y, u32 value)
    {
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx
        //     x: 0000000000000000xxxxxxxxxxxxxxxx
        //     y: 0000000000000000yyyyyyyyyyyyyyyy
        u64 v = ((u64(value) << 31) | value) & 0x5555555555555555;
        v = (v ^ (v >> 1)) & 0x3333333333333333;
        v = (v ^ (v >> 2)) & 0x0f0f0f0f0f0f0f0f;
        v = (v ^ (v >> 4)) & 0x00ff00ff00ff00ff;
        v = (v ^ (v >> 8)) & 0x0000ffff0000ffff;
        x = u32(v);
        y = u32(v >> 32);
    }

#endif // MANGO_ENABLE_BMI2

    constexpr u32 u32_select(u32 mask, u32 a, u32 b)
    {
        // bitwise mask ? a : b
        return (mask & (a ^ b)) ^ b;
    }

    constexpr bool u32_has_zero_byte(u32 value)
    {
        return ((value - 0x01010101) & ~value & 0x80808080) != 0;
    }

    static inline bool u32_is_power_of_two(u32 value)
    {
        return u32_clear_lsb(value) == 0;
    }

    static inline u32 u32_floor_power_of_two(u32 value)
    {
        return u32_extract_msb(value);
    }

    static inline u32 u32_ceil_power_of_two(u32 value)
    {
        const u32 mask = u32_mask_msb(value - 1);
        return mask + 1;
    }

    constexpr u32 u32_clamp(u32 value, u32 low, u32 high)
    {
        return (value < low) ? low : (high < value) ? high : value;
    }

    // ----------------------------------------------------------------------------
    // 64 bits
    // ----------------------------------------------------------------------------

#ifdef MANGO_ENABLE_BMI

    static inline u64 u64_extract_lsb(u64 value)
    {
        return _blsi_u64(value);
    }

    static inline u64 u64_clear_lsb(u64 value)
    {
        return _blsr_u64(value);
    }

    static inline u64 u64_mask_lsb(u64 value)
    {
        // NOTE: 0 expands to 0xffffffffffffffff
        return _blsmsk_u64(value);
    }

#else

    constexpr u64 u64_extract_lsb(u64 value)
    {
        return value & (0 - value);
    }

    constexpr u64 u64_clear_lsb(u64 value)
    {
        return value & (value - 1);
    }

    constexpr u64 u64_mask_lsb(u64 value)
    {
        // NOTE: 0 expands to 0xffffffffffffffff
        return value ^ (value - 1);
    }

#endif

    constexpr u64 u64_mask_without_lsb(u64 value)
    {
        // NOTE: 0 expands to 0xffffffffffffffff
        return ~(value | (0 - value));
    }

    static inline u64 u64_extend_lsb(u64 value)
    {
        return value | (0 - value);
    }

    constexpr u64 u64_extend_without_lsb(u64 value)
    {
        return value ^ (0 - value);
    }

#ifdef MANGO_ENABLE_BMI

    static inline int u64_tzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        return int(_tzcnt_u64(value));
    }

#elif defined(__aarch64__)

    static inline int u64_tzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        value = __rbitll(value);
        return __clzll(value);
    }

#elif defined(MANGO_BITS_GCC_BUILTINS)

    static inline int u64_tzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        return __builtin_ctzll(value);
    }

#else

    static inline int u64_tzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        const u64 lsb = u64_extract_lsb(value);
        static const u8 table [] =
        {
            0, 1, 2, 53, 3, 7, 54, 27, 4, 38, 41, 8, 34, 55, 48, 28,
            62, 5, 39, 46, 44, 42, 22, 9, 24, 35, 59, 56, 49, 18, 29, 11,
            63, 52, 6, 26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
            51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12,
        };
        return table[(lsb * 0x022fdd63cc95386du) >> 58];
    }

#endif

    static inline int u64_index_of_lsb(u64 value)
    {
        // NOTE: value 0 is undefined
        return u64_tzcnt(value);
    }

#ifdef MANGO_ENABLE_LZCNT

    static inline u64 u64_mask_msb(u64 value)
    {
        // value:  0001xxxxxxxx
        // result: 000111111111
        u64 mask = u64(1) << (63 - _lzcnt_u64(value));
        return mask | (mask - 1);
    }

    static inline u64 u64_extract_msb(u64 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        return u64(1) << (63 - _lzcnt_u64(value));
    }

    static inline int u64_index_of_msb(u64 value)
    {
        // NOTE: value 0 is undefined
        return int(63 - _lzcnt_u64(value));
    }

    static inline int u64_lzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        return int(_lzcnt_u64(value));
    }

#elif defined(__ARM_FEATURE_CLZ)

    static inline u64 u64_mask_msb(u64 value)
    {
        // value:  0001xxxxxxxx
        // result: 000111111111
        u64 mask = u64(1) << (63 - __clzll(value));
        return mask | (mask - 1);
    }

    static inline u64 u64_extract_msb(u64 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        return u64(1) << (63 - __clzll(value));
    }

    static inline int u64_index_of_msb(u64 value)
    {
        // NOTE: value 0 is undefined
        return int(63 - __clzll(value));
    }

    static inline int u64_lzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        return int(__clzll(value));
    }

#elif defined(MANGO_BITS_GCC_BUILTINS)

    static inline u64 u64_mask_msb(u64 value)
    {
        // value:  0001xxxxxxxx
        // result: 000111111111
        u64 mask = u64(1) << (63 - __builtin_clzll(value));
        return mask | (mask - 1);
    }

    static inline u64 u64_extract_msb(u64 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        return u64(1) << (63 - __builtin_clzll(value));
    }

    static inline int u64_index_of_msb(u64 value)
    {
        // NOTE: value 0 is undefined
        return int(63 - __builtin_clzll(value));
    }

    static inline int u64_lzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        return int(__builtin_clzll(value));
    }

#else

    static inline u64 u64_mask_msb(u64 value)
    {
        value |= value >> 1;
        value |= value >> 2;
        value |= value >> 4;
        value |= value >> 8;
        value |= value >> 16;
        value |= value >> 32;
        return value;
    }

    static inline u64 u64_extract_msb(u64 value)
    {
        // value:  0001xxxxxxxx
        // result: 000100000000
        value = u64_mask_msb(value);
        return value ^ (value >> 1);
    }

    static inline int u64_index_of_msb(u64 value)
    {
        // NOTE: value 0 is undefined
        const u64 mask = u64_mask_msb(value);
        static const u8 table [] =
        {
            0, 47, 1, 56, 48, 27, 2, 60, 57, 49, 41, 37, 28, 16, 3, 61,
            54, 58, 35, 52, 50, 42, 21, 44, 38, 32, 29, 23, 17, 11, 4, 62,
            46, 55, 26, 59, 40, 36, 15, 53, 34, 51, 20, 43, 31, 22, 10, 45,
            25, 39, 14, 33, 19, 30, 9, 24, 13, 18, 8, 12, 7, 6, 5, 63
        };
        return table[(mask * 0x03f79d71b4cb0a89u) >> 58];
    }

    static inline int u64_lzcnt(u64 value)
    {
        // NOTE: value 0 is undefined
        const u64 mask = u64_mask_msb(value);
        static const u8 table [] =
        {
            63, 16, 62, 7, 15, 36, 61, 3, 6, 14, 22, 26, 35, 47, 60, 2,
            9, 5, 28, 11, 13, 21, 42, 19, 25, 31, 34, 40, 46, 52, 59, 1,
            17, 8, 37, 4, 23, 27, 48, 10, 29, 12, 43, 20, 32, 41, 53, 18,
            38, 24, 49, 30, 44, 33, 54, 39, 50, 45, 55, 51, 56, 57, 58, 0
        };
        return table[(mask * 0x03f79d71b4cb0a89u) >> 58];
    }

#endif

    static inline int u64_log2(u64 value)
    {
        // NOTE: value 0 is undefined
        return u64_index_of_msb(value);
    }

#if defined(__aarch64__)

    static inline u64 u64_reverse_bits(u64 value)
    {
        return __rbitll(value);
    }

#else

    static inline u64 u64_reverse_bits(u64 value)
    {
#if defined(MANGO_CPU_64BIT)
        value = ((value >>  1) & 0x5555555555555555) | ((value <<  1) & 0xaaaaaaaaaaaaaaaa);
        value = ((value >>  2) & 0x3333333333333333) | ((value <<  2) & 0xcccccccccccccccc);
        value = ((value >>  4) & 0x0f0f0f0f0f0f0f0f) | ((value <<  4) & 0xf0f0f0f0f0f0f0f0);
        value = ((value >>  8) & 0x00ff00ff00ff00ff) | ((value <<  8) & 0xff00ff00ff00ff00);
        value = ((value >> 16) & 0x0000ffff0000ffff) | ((value << 16) & 0xffff0000ffff0000);
        value = ( value >> 32)                       | ( value << 32);
        return value;
#else
        u32 low = value & 0xffffffff;
        u32 high = value >> 32;
        low = u32_reverse_bits(low);
        high = u32_reverse_bits(high);
        return (u64(low) << 32) | high;
#endif
    }

#endif

#if defined(MANGO_ENABLE_POPCNT)

    static inline int u64_count_bits(u64 value)
    {
    #if defined(MANGO_CPU_64BIT)
        return int(_mm_popcnt_u64(value));
    #else
        // popcnt_u64 is invalid instruction in 32 bit legacy mode
        u32 low = value & 0xffffffff;
        u32 high = value >> 32;
        return _mm_popcnt_u32(low) + _mm_popcnt_u32(high);
    #endif
    }

#elif defined(MANGO_BITS_GCC_BUILTINS)

    static inline int u64_count_bits(u64 value)
    {
        return __builtin_popcountll(value);
    }

#elif defined(__aarch64__)

    static inline int u64_count_bits(u64 value)
    {
        uint8x8_t count8x8 = vcnt_u8(vcreate_u8(value));
        return int(vaddlv_u8(count8x8));
    }

#else

    static inline int u64_count_bits(u64 value)
    {
        const u64 c = 0x3333333333333333u;
        value -= (value >> 1) & 0x5555555555555555u;
        value = (value & c) + ((value >> 2) & c);
        value = (value + (value >> 4)) & 0x0f0f0f0f0f0f0f0fu;
        return int((value * 0x0101010101010101u) >> 56);
    }

#endif

#ifdef MANGO_ENABLE_BMI

    static inline u64 u64_extract_bits(u64 value, int offset, int size)
    {
        return _bextr_u64(value, offset, size);
    }

#else

    constexpr u64 u64_extract_bits(u64 value, int offset, int size)
    {
        return (value >> offset) & ((1ull << size) - 1);
    }

#endif

#ifdef MANGO_ENABLE_BMI2

    // The BMI2 bit interleaving is FAST on Intel CPUs, but emulated in microcode on AMD Zen
    // and should be avoided at ANY cost.

    static inline
    u64 u64_interleave_bits(u64 x, u64 y)
    {
        //     x: --------------------------------xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
        //     y: --------------------------------yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx
        u64 value = _pdep_u64(y, 0xaaaaaaaaaaaaaaaa) |
                    _pdep_u64(x, 0x5555555555555555);
        return value;
    }

    static inline
    void u64_deinterleave_bits(u64& x, u64& y, u64 value)
    {
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx
        //     x: 00000000000000000000000000000000xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
        //     y: 00000000000000000000000000000000yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
        x = _pext_u64(value, 0x5555555555555555);
        y = _pext_u64(value, 0xaaaaaaaaaaaaaaaa);
    }

#else

#if defined(__ARM_FEATURE_CRYPTO)

    static inline
    u64 u64_interleave_bits(u64 x, u64 y)
    {
        poly64_t a = x;
        poly64_t b = y;
        a = vmull_p64(a, a);
        b = vmull_p64(b, b);
        return u64(a | (b << 1));
    }

#elif defined(__PCLMUL__) && defined(MANGO_ENABLE_SSE4_2)

    static inline
    u64 u64_interleave_bits(u64 x, u64 y)
    {
        __m128i value = _mm_set_epi64x(x, y);
        __m128i a = _mm_clmulepi64_si128(value, value, 0x11);
        __m128i b = _mm_clmulepi64_si128(value, value, 0x00);
        return _mm_extract_epi64(a, 0) | (_mm_extract_epi64(b, 0) << 1);
    }

#else

    static inline
    u64 u64_interleave_bits(u64 x, u64 y)
    {
        //     x: --------------------------------xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
        //     y: --------------------------------yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx

        x = (x | (x << 16)) & 0x0000ffff0000ffff;
        x = (x | (x <<  8)) & 0x00ff00ff00ff00ff;
        x = (x | (x <<  4)) & 0x0f0f0f0f0f0f0f0f;
        x = (x | (x <<  2)) & 0x3333333333333333;
        x = (x | (x <<  1)) & 0x5555555555555555;

        y = (y | (y << 16)) & 0x0000ffff0000ffff;
        y = (y | (y <<  8)) & 0x00ff00ff00ff00ff;
        y = (y | (y <<  4)) & 0x0f0f0f0f0f0f0f0f;
        y = (y | (y <<  2)) & 0x3333333333333333;
        y = (y | (y <<  1)) & 0x5555555555555555;

        u64 value = (y << 1) | x;
        return value;
    }

#endif // defined(__PCLMUL__) && defined(MANGO_ENABLE_SSE4_2)

    static inline
    void u64_deinterleave_bits(u64& x, u64& y, u64 value)
    {
        // value: yxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyxyx
        //     x: 00000000000000000000000000000000xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
        //     y: 00000000000000000000000000000000yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy

        x = value & 0x5555555555555555;
        x = (x ^ (x >> 1 )) & 0x3333333333333333;
        x = (x ^ (x >> 2 )) & 0x0f0f0f0f0f0f0f0f;
        x = (x ^ (x >> 4 )) & 0x00ff00ff00ff00ff;
        x = (x ^ (x >> 8 )) & 0x0000ffff0000ffff;
        x = (x ^ (x >> 16)) & 0x00000000ffffffff;

        y = (value >> 1) & 0x5555555555555555;
        y = (y ^ (y >> 1 )) & 0x3333333333333333;
        y = (y ^ (y >> 2 )) & 0x0f0f0f0f0f0f0f0f;
        y = (y ^ (y >> 4 )) & 0x00ff00ff00ff00ff;
        y = (y ^ (y >> 8 )) & 0x0000ffff0000ffff;
        y = (y ^ (y >> 16)) & 0x00000000ffffffff;
    }

#endif // MANGO_ENABLE_BMI2

    constexpr u64 u64_select(u64 mask, u64 a, u64 b)
    {
        // bitwise mask ? a : b
        return (mask & (a ^ b)) ^ b;
    }

    constexpr bool u64_has_zero_byte(u64 value)
    {
	    return (~value & (value - 0x0101010101010101) & 0x8080808080808080) != 0;
    }

    static inline bool u64_is_power_of_two(u64 value)
    {
        return u64_clear_lsb(value) == 0;
    }

    static inline u64 u64_floor_power_of_two(u64 value)
    {
        return u64_extract_msb(value);
    }

    static inline u64 u64_ceil_power_of_two(u64 value)
    {
        const u64 mask = u64_mask_msb(value - 1);
        return mask + 1;
    }

    constexpr u64 u64_clamp(u64 value, u64 low, u64 high)
    {
        return (value < low) ? low : (high < value) ? high : value;
    }

} // namespace mango
