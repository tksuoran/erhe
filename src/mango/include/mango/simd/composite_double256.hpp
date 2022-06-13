/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

#define SIMD_COMPOSITE_FUNC1(R, A, FUNC) \
    static inline R FUNC(A a) \
    { \
        R result; \
        result.lo = FUNC(a.lo); \
        result.hi = FUNC(a.hi); \
        return result; \
    }

#define SIMD_COMPOSITE_FUNC2(R, AB, FUNC) \
    static inline R FUNC(AB a, AB b) \
    { \
        R result; \
        result.lo = FUNC(a.lo, b.lo); \
        result.hi = FUNC(a.hi, b.hi); \
        return result; \
    }

#define SIMD_COMPOSITE_FUNC3(R, ABC, FUNC) \
    static inline R FUNC(ABC a, ABC b, ABC c) \
    { \
        R result; \
        result.lo = FUNC(a.lo, b.lo, c.lo); \
        result.hi = FUNC(a.hi, b.hi, c.hi); \
        return result; \
    }

#define SIMD_COMPOSITE_ZEROMASK_FUNC2(R, AB, MASK, FUNC) \
    static inline R FUNC(AB a, AB b, MASK mask) \
    { \
        R result; \
        result.lo = FUNC(a.lo, b.lo, mask.lo); \
        result.hi = FUNC(a.hi, b.hi, mask.hi); \
        return result; \
    }

#define SIMD_COMPOSITE_MASK_FUNC2(R, AB, MASK, FUNC) \
    static inline R FUNC(AB a, AB b, MASK mask, AB value) \
    { \
        R result; \
        result.lo = FUNC(a.lo, b.lo, mask.lo, value.lo); \
        result.hi = FUNC(a.hi, b.hi, mask.hi, value.hi); \
        return result; \
    }

    // -----------------------------------------------------------------
    // f64x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f64x4 shuffle(f64x4 v)
    {
        const f64x2 v0 = x & 2 ? v.hi : v.lo;
        const f64x2 v1 = y & 2 ? v.hi : v.lo;
        const f64x2 v2 = z & 2 ? v.hi : v.lo;
        const f64x2 v3 = w & 2 ? v.hi : v.lo;

        f64x4 result;
        result.lo = shuffle<x & 1, y & 1>(v0, v1);
        result.hi = shuffle<z & 1, w & 1>(v2, v3);
        return result;
    }

    template <>
    inline f64x4 shuffle<0, 1, 2, 3>(f64x4 v)
    {
        // .xyzw
        return v;
    }

    template <>
    inline f64x4 shuffle<0, 0, 0, 0>(f64x4 v)
    {
        // .xxxx
        const f64x2 xx = shuffle<0, 0>(v.lo);
        f64x4 result;
        result.lo = xx;
        result.hi = xx;
        return result;
    }

    template <>
    inline f64x4 shuffle<1, 1, 1, 1>(f64x4 v)
    {
        // .yyyy
        const f64x2 yy = shuffle<1, 1>(v.lo);
        f64x4 result;
        result.lo = yy;
        result.hi = yy;
        return result;
    }

    template <>
    inline f64x4 shuffle<2, 2, 2, 2>(f64x4 v)
    {
        // .zzzz
        const f64x2 zz = shuffle<0, 0>(v.hi);
        f64x4 result;
        result.lo = zz;
        result.hi = zz;
        return result;
    }

    template <>
    inline f64x4 shuffle<3, 3, 3, 3>(f64x4 v)
    {
        // .wwww
        const f64x2 ww = shuffle<1, 1>(v.hi);
        f64x4 result;
        result.lo = ww;
        result.hi = ww;
        return result;
    }

    // set component

    template <int Index>
    static inline f64x4 set_component(f64x4 a, f64 s)
    {
        static_assert(Index < 4, "Index out of range.");
        switch (Index)
        {
            case 0: a.lo = set_component<0>(a.lo, s); break;
            case 1: a.lo = set_component<1>(a.lo, s); break;
            case 2: a.hi = set_component<0>(a.hi, s); break;
            case 3: a.hi = set_component<1>(a.hi, s); break;
        }
        return a;
    }

    // get component

    template <int Index>
    static inline f64 get_component(f64x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        f64 s = 0.0;
        switch (Index)
        {
            case 0: s = get_component<0>(a.lo); break;
            case 1: s = get_component<1>(a.lo); break;
            case 2: s = get_component<0>(a.hi); break;
            case 3: s = get_component<1>(a.hi); break;
        }
        return s;
    }

    static inline f64x4 f64x4_zero()
    {
        f64x4 result;
        result.lo = f64x2_zero();
        result.hi = f64x2_zero();
        return result;
    }

    static inline f64x4 f64x4_set(f64 s)
    {
        f64x4 result;
        result.lo = f64x2_set(s);
        result.hi = f64x2_set(s);
        return result;
    }

    static inline f64x4 f64x4_set(f64 x, f64 y, f64 z, f64 w)
    {
        f64x4 result;
        result.lo = f64x2_set(x, y);
        result.hi = f64x2_set(z, w);
        return result;
    }

    static inline f64x4 f64x4_uload(const f64* source)
    {
        f64x4 result;
        result.lo = f64x2_uload(source + 0);
        result.hi = f64x2_uload(source + 2);
        return result;
    }

    static inline void f64x4_ustore(f64* dest, f64x4 a)
    {
        f64x2_ustore(dest + 0, a.lo);
        f64x2_ustore(dest + 2, a.hi);
    }

    static inline f64x4 movelh(f64x4 a, f64x4 b)
    {
        f64x4 result;
        result.lo = a.lo;
        result.hi = b.lo;
        return result;
    }

    static inline f64x4 movehl(f64x4 a, f64x4 b)
    {
        f64x4 result;
        result.lo = b.hi;
        result.hi = a.hi;
        return result;
    }

    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, unpackhi)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, unpacklo)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, bitwise_nand)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, bitwise_and)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, bitwise_or)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, bitwise_xor)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, bitwise_not)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, min)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, max)

    static inline f64x4 hmin(f64x4 a)
    {
        const f64x2 xy = min(a.lo, shuffle<1, 0>(a.lo));
        const f64x2 zw = min(a.hi, shuffle<1, 0>(a.hi));
        const f64x2 s = min(xy, zw);
        f64x4 result;
        result.lo = s;
        result.hi = s;
        return result;
    }

    static inline f64x4 hmax(f64x4 a)
    {
        const f64x2 xy = max(a.lo, shuffle<1, 0>(a.lo));
        const f64x2 zw = max(a.hi, shuffle<1, 0>(a.hi));
        const f64x2 s = max(xy, zw);
        f64x4 result;
        result.lo = s;
        result.hi = s;
        return result;
    }

    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, abs)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, neg)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, sign)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, add)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, sub)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, mul)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, div)

    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x4, f64x4, mask64x4, min)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x4, f64x4, mask64x4, max)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x4, f64x4, mask64x4, add)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x4, f64x4, mask64x4, sub)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x4, f64x4, mask64x4, mul)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x4, f64x4, mask64x4, div)
    SIMD_COMPOSITE_MASK_FUNC2(f64x4, f64x4, mask64x4, min)
    SIMD_COMPOSITE_MASK_FUNC2(f64x4, f64x4, mask64x4, max)
    SIMD_COMPOSITE_MASK_FUNC2(f64x4, f64x4, mask64x4, add)
    SIMD_COMPOSITE_MASK_FUNC2(f64x4, f64x4, mask64x4, sub)
    SIMD_COMPOSITE_MASK_FUNC2(f64x4, f64x4, mask64x4, mul)
    SIMD_COMPOSITE_MASK_FUNC2(f64x4, f64x4, mask64x4, div)

    static inline f64x4 div(f64x4 a, f64 b)
    {
        f64x4 result;
        result.lo = div(a.lo, b);
        result.hi = div(a.hi, b);
        return result;
    }

    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, hadd)
    SIMD_COMPOSITE_FUNC2(f64x4, f64x4, hsub)
    SIMD_COMPOSITE_FUNC3(f64x4, f64x4, madd)
    SIMD_COMPOSITE_FUNC3(f64x4, f64x4, msub)
    SIMD_COMPOSITE_FUNC3(f64x4, f64x4, nmadd)
    SIMD_COMPOSITE_FUNC3(f64x4, f64x4, nmsub)
    SIMD_COMPOSITE_FUNC3(f64x4, f64x4, lerp)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, rcp)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, rsqrt)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, sqrt)

    static inline f64 dot4(f64x4 a, f64x4 b)
    {
        f64 low = dot2(a.lo, b.lo);
        f64 high = dot2(a.hi, b.hi);
        return low + high;
    }

    // compare

    SIMD_COMPOSITE_FUNC2(mask64x4, f64x4, compare_neq)
    SIMD_COMPOSITE_FUNC2(mask64x4, f64x4, compare_eq)
    SIMD_COMPOSITE_FUNC2(mask64x4, f64x4, compare_lt)
    SIMD_COMPOSITE_FUNC2(mask64x4, f64x4, compare_le)
    SIMD_COMPOSITE_FUNC2(mask64x4, f64x4, compare_gt)
    SIMD_COMPOSITE_FUNC2(mask64x4, f64x4, compare_ge)

    static inline f64x4 select(mask64x4 mask, f64x4 a, f64x4 b)
    {
        f64x4 result;
        result.lo = select(mask.lo, a.lo, b.lo);
        result.hi = select(mask.hi, a.hi, b.hi);
        return result;
    }

    // rounding

    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, round)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, trunc)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, floor)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, ceil)
    SIMD_COMPOSITE_FUNC1(f64x4, f64x4, fract)

#undef SIMD_COMPOSITE_FUNC1
#undef SIMD_COMPOSITE_FUNC2
#undef SIMD_COMPOSITE_FUNC3
#undef SIMD_COMPOSITE_ZEROMASK_FUNC2
#undef SIMD_COMPOSITE_MASK_FUNC2

} // namespace mango::simd
