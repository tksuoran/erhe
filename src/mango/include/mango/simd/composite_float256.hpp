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
    // f32x8
    // -----------------------------------------------------------------

    static inline f32x8 f32x8_zero()
    {
        f32x8 result;
        result.lo = f32x4_zero();
        result.hi = f32x4_zero();
        return result;
    }

    static inline f32x8 f32x8_set(f32 s)
    {
        f32x8 result;
        result.lo = f32x4_set(s);
        result.hi = f32x4_set(s);
        return result;
    }

    static inline f32x8 f32x8_set(f32 s0, f32 s1, f32 s2, f32 s3, f32 s4, f32 s5, f32 s6, f32 s7)
    {
        f32x8 result;
        result.lo = f32x4_set(s0, s1, s2, s3);
        result.hi = f32x4_set(s4, s5, s6, s7);
        return result;
    }

    static inline f32x8 f32x8_uload(const f32* source)
    {
        f32x8 result;
        result.lo = f32x4_uload(source + 0);
        result.hi = f32x4_uload(source + 4);
        return result;
    }

    static inline void f32x8_ustore(f32* dest, f32x8 a)
    {
        f32x4_ustore(dest + 0, a.lo);
        f32x4_ustore(dest + 4, a.hi);
    }

    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, unpackhi)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, unpacklo)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, bitwise_nand)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, bitwise_and)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, bitwise_or)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, bitwise_xor)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, bitwise_not)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, min)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, max)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, abs)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, neg)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, sign)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, add)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, sub)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, mul)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, div)

    SIMD_COMPOSITE_ZEROMASK_FUNC2(f32x8, f32x8, mask32x8, min)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f32x8, f32x8, mask32x8, max)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f32x8, f32x8, mask32x8, add)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f32x8, f32x8, mask32x8, sub)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f32x8, f32x8, mask32x8, mul)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f32x8, f32x8, mask32x8, div)
    SIMD_COMPOSITE_MASK_FUNC2(f32x8, f32x8, mask32x8, min)
    SIMD_COMPOSITE_MASK_FUNC2(f32x8, f32x8, mask32x8, max)
    SIMD_COMPOSITE_MASK_FUNC2(f32x8, f32x8, mask32x8, add)
    SIMD_COMPOSITE_MASK_FUNC2(f32x8, f32x8, mask32x8, sub)
    SIMD_COMPOSITE_MASK_FUNC2(f32x8, f32x8, mask32x8, mul)
    SIMD_COMPOSITE_MASK_FUNC2(f32x8, f32x8, mask32x8, div)

    static inline f32x8 div(f32x8 a, f32 b)
    {
        f32x8 result;
        result.lo = div(a.lo, b);
        result.hi = div(a.hi, b);
        return result;
    }

    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, hadd)
    SIMD_COMPOSITE_FUNC2(f32x8, f32x8, hsub)
    SIMD_COMPOSITE_FUNC3(f32x8, f32x8, madd)
    SIMD_COMPOSITE_FUNC3(f32x8, f32x8, msub)
    SIMD_COMPOSITE_FUNC3(f32x8, f32x8, nmadd)
    SIMD_COMPOSITE_FUNC3(f32x8, f32x8, nmsub)
    SIMD_COMPOSITE_FUNC3(f32x8, f32x8, lerp)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, rcp)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, rsqrt)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, sqrt)

    // compare

    SIMD_COMPOSITE_FUNC2(mask32x8, f32x8, compare_neq)
    SIMD_COMPOSITE_FUNC2(mask32x8, f32x8, compare_eq)
    SIMD_COMPOSITE_FUNC2(mask32x8, f32x8, compare_lt)
    SIMD_COMPOSITE_FUNC2(mask32x8, f32x8, compare_le)
    SIMD_COMPOSITE_FUNC2(mask32x8, f32x8, compare_gt)
    SIMD_COMPOSITE_FUNC2(mask32x8, f32x8, compare_ge)

    static inline f32x8 select(mask32x8 mask, f32x8 a, f32x8 b)
    {
        f32x8 result;
        result.lo = select(mask.lo, a.lo, b.lo);
        result.hi = select(mask.hi, a.hi, b.hi);
        return result;
    }

    // rounding

    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, round)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, trunc)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, floor)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, ceil)
    SIMD_COMPOSITE_FUNC1(f32x8, f32x8, fract)

#undef SIMD_COMPOSITE_FUNC1
#undef SIMD_COMPOSITE_FUNC2
#undef SIMD_COMPOSITE_FUNC3
#undef SIMD_COMPOSITE_ZEROMASK_FUNC2
#undef SIMD_COMPOSITE_MASK_FUNC2

} // namespace mango::simd
