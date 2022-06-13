/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f64x2
    // -----------------------------------------------------------------

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 v)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return {{ v[x], v[y] }};
    }

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 a, f64x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return {{ a[x], b[y] }};
    }

    // indexed access

    template <unsigned int Index>
    static inline f64x2 set_component(f64x2 a, f64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline f64 get_component(f64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return a[Index];
    }

    static inline f64x2 f64x2_zero()
    {
        return {{ 0.0, 0.0 }};
    }

    static inline f64x2 f64x2_set(f64 s)
    {
        return {{ s, s }};
    }

    static inline f64x2 f64x2_set(f64 x, f64 y)
    {
        return {{ x, y }};
    }

    static inline f64x2 f64x2_uload(const f64* source)
    {
        return f64x2_set(source[0], source[1]);
    }

    static inline void f64x2_ustore(f64* dest, f64x2 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline f64x2 unpacklo(f64x2 a, f64x2 b)
    {
        return f64x2_set(a[0], b[0]);
    }

    static inline f64x2 unpackhi(f64x2 a, f64x2 b)
    {
        return f64x2_set(a[1], b[1]);
    }

    // bitwise

    static inline f64x2 bitwise_nand(f64x2 a, f64x2 b)
    {
        const Double x(~Double(a[0]).u & Double(b[0]).u);
        const Double y(~Double(a[1]).u & Double(b[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_and(f64x2 a, f64x2 b)
    {
        const Double x(Double(a[0]).u & Double(b[0]).u);
        const Double y(Double(a[1]).u & Double(b[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_or(f64x2 a, f64x2 b)
    {
        const Double x(Double(a[0]).u | Double(b[0]).u);
        const Double y(Double(a[1]).u | Double(b[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_xor(f64x2 a, f64x2 b)
    {
        const Double x(Double(a[0]).u ^ Double(b[0]).u);
        const Double y(Double(a[1]).u ^ Double(b[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_not(f64x2 a)
    {
        const Double x(~Double(a[0]).u);
        const Double y(~Double(a[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 min(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v[0] = std::min(a[0], b[0]);
        v[1] = std::min(a[1], b[1]);
        return v;
    }

    static inline f64x2 max(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v[0] = std::max(a[0], b[0]);
        v[1] = std::max(a[1], b[1]);
        return v;
    }

    static inline f64x2 abs(f64x2 a)
    {
        f64x2 v;
        v[0] = std::abs(a[0]);
        v[1] = std::abs(a[1]);
        return v;
    }

    static inline f64x2 neg(f64x2 a)
    {
        return f64x2_set(-a[0], -a[1]);
    }

    static inline f64x2 sign(f64x2 a)
    {
        f64x2 v;
        v[0] = a[0] < 0 ? -1.0 : (a[0] > 0 ? 1.0 : 0.0);
        v[1] = a[1] < 0 ? -1.0 : (a[1] > 0 ? 1.0 : 0.0);
        return v;
    }

    static inline f64x2 add(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v[0] = a[0] + b[0];
        v[1] = a[1] + b[1];
        return v;
    }

    static inline f64x2 sub(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v[0] = a[0] - b[0];
        v[1] = a[1] - b[1];
        return v;
    }

    static inline f64x2 mul(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v[0] = a[0] * b[0];
        v[1] = a[1] * b[1];
        return v;
    }

    static inline f64x2 div(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v[0] = a[0] / b[0];
        v[1] = a[1] / b[1];
        return v;
    }

    static inline f64x2 div(f64x2 a, f64 b)
    {
        f64x2 v;
        v[0] = a[0] / b;
        v[1] = a[1] / b;
        return v;
    }

    static inline f64x2 hadd(f64x2 a, f64x2 b)
    {
	    f64x2 v;
	    v[0] = a[0] + a[1];
	    v[1] = b[0] + b[1];
	    return v;
    }

    static inline f64x2 hsub(f64x2 a, f64x2 b)
    {
	    f64x2 v;
	    v[0] = a[0] - a[1];
	    v[1] = b[0] - b[1];
	    return v;
    }

    static inline f64x2 madd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a + b * c
        f64x2 v;
        v[0] = a[0] + b[0] * c[0];
        v[1] = a[1] + b[1] * c[1];
        return v;
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        // b * c - a
        f64x2 v;
        v[0] = b[0] * c[0] - a[0];
        v[1] = b[1] * c[1] - a[1];
        return v;
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a - b * c
        f64x2 v;
        v[0] = a[0] - b[0] * c[0];
        v[1] = a[1] - b[1] * c[1];
        return v;
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        // -(a + b * c)
        f64x2 v;
        v[0] = -(a[0] + b[0] * c[0]);
        v[1] = -(a[1] + b[1] * c[1]);
        return v;
    }

    static inline f64x2 lerp(f64x2 a, f64x2 b, f64x2 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

    static inline f64x2 rcp(f64x2 a)
    {
        f64x2 v;
        v[0] = 1.0 / a[0];
        v[1] = 1.0 / a[1];
        return v;
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        f64x2 v;
        v[0] = 1.0 / std::sqrt(a[0]);
        v[1] = 1.0 / std::sqrt(a[1]);
        return v;
    }

    static inline f64x2 sqrt(f64x2 a)
    {
        f64x2 v;
        v[0] = std::sqrt(a[0]);
        v[1] = std::sqrt(a[1]);
        return v;
    }

    static inline f64 dot2(f64x2 a, f64x2 b)
    {
        return a[0] * b[0] + a[1] * b[1];
    }

    // compare

    static inline mask64x2 compare_neq(f64x2 a, f64x2 b)
    {
        mask64x2 v = 0;
        v.mask |= u32(a[0] != b[0]) << 0;
        v.mask |= u32(a[1] != b[1]) << 1;
        return v;
    }

    static inline mask64x2 compare_eq(f64x2 a, f64x2 b)
    {
        mask64x2 v = 0;
        v.mask |= u32(a[0] == b[0]) << 0;
        v.mask |= u32(a[1] == b[1]) << 1;
        return v;
    }

    static inline mask64x2 compare_lt(f64x2 a, f64x2 b)
    {
        mask64x2 v = 0;
        v.mask |= u32(a[0] < b[0]) << 0;
        v.mask |= u32(a[1] < b[1]) << 1;
        return v;
    }

    static inline mask64x2 compare_le(f64x2 a, f64x2 b)
    {
        mask64x2 v = 0;
        v.mask |= u32(a[0] <= b[0]) << 0;
        v.mask |= u32(a[1] <= b[1]) << 1;
        return v;
    }

    static inline mask64x2 compare_gt(f64x2 a, f64x2 b)
    {
        mask64x2 v = 0;
        v.mask |= u32(a[0] > b[0]) << 0;
        v.mask |= u32(a[1] > b[1]) << 1;
        return v;
    }

    static inline mask64x2 compare_ge(f64x2 a, f64x2 b)
    {
        mask64x2 v = 0;
        v.mask |= u32(a[0] >= b[0]) << 0;
        v.mask |= u32(a[1] >= b[1]) << 1;
        return v;
    }

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        f64x2 result;
        result[0] = mask.mask & (1 << 0) ? a[0] : b[0];
        result[1] = mask.mask & (1 << 1) ? a[1] : b[1];
        return result;
    }

    // rounding

    static inline f64x2 round(f64x2 s)
    {
        f64x2 v;
        v[0] = std::round(s[0]);
        v[1] = std::round(s[1]);
        return v;
    }

    static inline f64x2 trunc(f64x2 s)
    {
        f64x2 v;
        v[0] = std::trunc(s[0]);
        v[1] = std::trunc(s[1]);
        return v;
    }

    static inline f64x2 floor(f64x2 s)
    {
        f64x2 v;
        v[0] = std::floor(s[0]);
        v[1] = std::floor(s[1]);
        return v;
    }

    static inline f64x2 ceil(f64x2 s)
    {
        f64x2 v;
        v[0] = std::ceil(s[0]);
        v[1] = std::ceil(s[1]);
        return v;
    }

    static inline f64x2 fract(f64x2 s)
    {
        f64x2 v;
        v[0] = s[0] - std::floor(s[0]);
        v[1] = s[1] - std::floor(s[1]);
        return v;
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

#define SIMD_ZEROMASK_DOUBLE128
#define SIMD_MASK_DOUBLE128
#include <mango/simd/common_mask.hpp>
#undef SIMD_ZEROMASK_DOUBLE128
#undef SIMD_MASK_DOUBLE128

} // namespace mango::simd
