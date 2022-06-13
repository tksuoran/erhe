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
    // f64x8
    // -----------------------------------------------------------------

    static inline f64x8 f64x8_zero()
    {
        f64x8 result;
        result.lo = f64x4_zero();
        result.hi = f64x4_zero();
        return result;
    }

    static inline f64x8 f64x8_set(f64 s)
    {
        f64x8 result;
        result.lo = f64x4_set(s);
        result.hi = f64x4_set(s);
        return result;
    }

    static inline f64x8 f64x8_set(f64 s0, f64 s1, f64 s2, f64 s3, f64 s4, f64 s5, f64 s6, f64 s7)
    {
        f64x8 result;
        result.lo = f64x4_set(s0, s1, s2, s3);
        result.hi = f64x4_set(s4, s5, s6, s7);
        return result;
    }

    static inline f64x8 f64x8_uload(const f64* source)
    {
        f64x8 result;
        result.lo = f64x4_uload(source + 0);
        result.hi = f64x4_uload(source + 4);
        return result;
    }

    static inline void f64x8_ustore(f64* dest, f64x8 a)
    {
        f64x4_ustore(dest + 0, a.lo);
        f64x4_ustore(dest + 4, a.hi);
    }

    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, unpackhi)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, unpacklo)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, bitwise_nand)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, bitwise_and)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, bitwise_or)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, bitwise_xor)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, bitwise_not)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, min)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, max)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, abs)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, neg)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, sign)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, add)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, sub)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, mul)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, div)

    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x8, f64x8, mask64x8, min)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x8, f64x8, mask64x8, max)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x8, f64x8, mask64x8, add)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x8, f64x8, mask64x8, sub)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x8, f64x8, mask64x8, mul)
    SIMD_COMPOSITE_ZEROMASK_FUNC2(f64x8, f64x8, mask64x8, div)
    SIMD_COMPOSITE_MASK_FUNC2(f64x8, f64x8, mask64x8, min)
    SIMD_COMPOSITE_MASK_FUNC2(f64x8, f64x8, mask64x8, max)
    SIMD_COMPOSITE_MASK_FUNC2(f64x8, f64x8, mask64x8, add)
    SIMD_COMPOSITE_MASK_FUNC2(f64x8, f64x8, mask64x8, sub)
    SIMD_COMPOSITE_MASK_FUNC2(f64x8, f64x8, mask64x8, mul)
    SIMD_COMPOSITE_MASK_FUNC2(f64x8, f64x8, mask64x8, div)

    static inline f64x8 div(f64x8 a, f64 b)
    {
        f64x8 result;
        result.lo = div(a.lo, b);
        result.hi = div(a.hi, b);
        return result;
    }

    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, hadd)
    SIMD_COMPOSITE_FUNC2(f64x8, f64x8, hsub)
    SIMD_COMPOSITE_FUNC3(f64x8, f64x8, madd)
    SIMD_COMPOSITE_FUNC3(f64x8, f64x8, msub)
    SIMD_COMPOSITE_FUNC3(f64x8, f64x8, nmadd)
    SIMD_COMPOSITE_FUNC3(f64x8, f64x8, nmsub)
    SIMD_COMPOSITE_FUNC3(f64x8, f64x8, lerp)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, rcp)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, rsqrt)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, sqrt)

    // compare

    SIMD_COMPOSITE_FUNC2(mask64x8, f64x8, compare_neq)
    SIMD_COMPOSITE_FUNC2(mask64x8, f64x8, compare_eq)
    SIMD_COMPOSITE_FUNC2(mask64x8, f64x8, compare_lt)
    SIMD_COMPOSITE_FUNC2(mask64x8, f64x8, compare_le)
    SIMD_COMPOSITE_FUNC2(mask64x8, f64x8, compare_gt)
    SIMD_COMPOSITE_FUNC2(mask64x8, f64x8, compare_ge)

    static inline f64x8 select(mask64x8 mask, f64x8 a, f64x8 b)
    {
        f64x8 result;
        result.lo = select(mask.lo, a.lo, b.lo);
        result.hi = select(mask.hi, a.hi, b.hi);
        return result;
    }

    // rounding

    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, round)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, trunc)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, floor)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, ceil)
    SIMD_COMPOSITE_FUNC1(f64x8, f64x8, fract)

#undef SIMD_COMPOSITE_FUNC1
#undef SIMD_COMPOSITE_FUNC2
#undef SIMD_COMPOSITE_FUNC3
#undef SIMD_COMPOSITE_ZEROMASK_FUNC2
#undef SIMD_COMPOSITE_MASK_FUNC2

} // namespace mango::simd
