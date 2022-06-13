/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f32x2
    // -----------------------------------------------------------------

    template <u32 x, u32 y>
    static inline f32x2 shuffle(f32x2 v)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return {{ v[x], v[y] }};
    }

    template <u32 x, u32 y>
    static inline f32x2 shuffle(f32x2 a, f32x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return {{ a[x], b[y] }};
    }

    // indexed access

    template <unsigned int Index>
    static inline f32x2 set_component(f32x2 a, f32 s)
    {
        static_assert(Index < 2, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline f32 get_component(f32x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return a[Index];
    }

    static inline f32x2 f32x2_zero()
    {
        return {{ 0.0f, 0.0f }};
    }

    static inline f32x2 f32x2_set(f32 s)
    {
        return {{ s, s }};
    }

    static inline f32x2 f32x2_set(f32 x, f32 y)
    {
        return {{ x, y }};
    }

    static inline f32x2 f32x2_uload(const f32* source)
    {
        return f32x2_set(source[0], source[1]);
    }

    static inline void f32x2_ustore(f32* dest, f32x2 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline f32x2 unpacklo(f32x2 a, f32x2 b)
    {
        return f32x2_set(a[0], b[0]);
    }

    static inline f32x2 unpackhi(f32x2 a, f32x2 b)
    {
        return f32x2_set(a[1], b[1]);
    }

    // bitwise

    static inline f32x2 bitwise_nand(f32x2 a, f32x2 b)
    {
        const Float x(~Float(a[0]).u & Float(b[0]).u);
        const Float y(~Float(a[1]).u & Float(b[1]).u);
        return f32x2_set(x, y);
    }

    static inline f32x2 bitwise_and(f32x2 a, f32x2 b)
    {
        const Float x(Float(a[0]).u & Float(b[0]).u);
        const Float y(Float(a[1]).u & Float(b[1]).u);
        return f32x2_set(x, y);
    }

    static inline f32x2 bitwise_or(f32x2 a, f32x2 b)
    {
        const Float x(Float(a[0]).u | Float(b[0]).u);
        const Float y(Float(a[1]).u | Float(b[1]).u);
        return f32x2_set(x, y);
    }

    static inline f32x2 bitwise_xor(f32x2 a, f32x2 b)
    {
        const Float x(Float(a[0]).u ^ Float(b[0]).u);
        const Float y(Float(a[1]).u ^ Float(b[1]).u);
        return f32x2_set(x, y);
    }

    static inline f32x2 bitwise_not(f32x2 a)
    {
        const Float x(~Float(a[0]).u);
        const Float y(~Float(a[1]).u);
        return f32x2_set(x, y);
    }

    static inline f32x2 min(f32x2 a, f32x2 b)
    {
        f32x2 v;
        v[0] = std::min(a[0], b[0]);
        v[1] = std::min(a[1], b[1]);
        return v;
    }

    static inline f32x2 max(f32x2 a, f32x2 b)
    {
        f32x2 v;
        v[0] = std::max(a[0], b[0]);
        v[1] = std::max(a[1], b[1]);
        return v;
    }

    static inline f32x2 abs(f32x2 a)
    {
        f32x2 v;
        v[0] = std::abs(a[0]);
        v[1] = std::abs(a[1]);
        return v;
    }

    static inline f32x2 neg(f32x2 a)
    {
        return f32x2_set(-a[0], -a[1]);
    }

    static inline f32x2 sign(f32x2 a)
    {
        f32x2 v;
        v[0] = a[0] < 0 ? -1.0f : (a[0] > 0 ? 1.0f : 0.0f);
        v[1] = a[1] < 0 ? -1.0f : (a[1] > 0 ? 1.0f : 0.0f);
        return v;
    }

    static inline f32x2 add(f32x2 a, f32x2 b)
    {
        f32x2 v;
        v[0] = a[0] + b[0];
        v[1] = a[1] + b[1];
        return v;
    }

    static inline f32x2 sub(f32x2 a, f32x2 b)
    {
        f32x2 v;
        v[0] = a[0] - b[0];
        v[1] = a[1] - b[1];
        return v;
    }

    static inline f32x2 mul(f32x2 a, f32x2 b)
    {
        f32x2 v;
        v[0] = a[0] * b[0];
        v[1] = a[1] * b[1];
        return v;
    }

    static inline f32x2 div(f32x2 a, f32x2 b)
    {
        f32x2 v;
        v[0] = a[0] / b[0];
        v[1] = a[1] / b[1];
        return v;
    }

    static inline f32x2 div(f32x2 a, f32 b)
    {
        f32x2 v;
        v[0] = a[0] / b;
        v[1] = a[1] / b;
        return v;
    }

    static inline f32x2 hadd(f32x2 a, f32x2 b)
    {
	    f32x2 v;
	    v[0] = a[0] + a[1];
	    v[1] = b[0] + b[1];
	    return v;
    }

    static inline f32x2 hsub(f32x2 a, f32x2 b)
    {
	    f32x2 v;
	    v[0] = a[0] - a[1];
	    v[1] = b[0] - b[1];
	    return v;
    }

    static inline f32x2 madd(f32x2 a, f32x2 b, f32x2 c)
    {
        // a + b * c
        f32x2 v;
        v[0] = a[0] + b[0] * c[0];
        v[1] = a[1] + b[1] * c[1];
        return v;
    }

    static inline f32x2 msub(f32x2 a, f32x2 b, f32x2 c)
    {
        // b * c - a
        f32x2 v;
        v[0] = b[0] * c[0] - a[0];
        v[1] = b[1] * c[1] - a[1];
        return v;
    }

    static inline f32x2 nmadd(f32x2 a, f32x2 b, f32x2 c)
    {
        // a - b * c
        f32x2 v;
        v[0] = a[0] - b[0] * c[0];
        v[1] = a[1] - b[1] * c[1];
        return v;
    }

    static inline f32x2 nmsub(f32x2 a, f32x2 b, f32x2 c)
    {
        // -(a + b * c)
        f32x2 v;
        v[0] = -(a[0] + b[0] * c[0]);
        v[1] = -(a[1] + b[1] * c[1]);
        return v;
    }

    static inline f32x2 lerp(f32x2 a, f32x2 b, f32x2 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

    static inline f32x2 rcp(f32x2 a)
    {
        f32x2 v;
        v[0] = 1.0f / a[0];
        v[1] = 1.0f / a[1];
        return v;
    }

    static inline f32x2 rsqrt(f32x2 a)
    {
        f32x2 v;
        v[0] = 1.0f / std::sqrt(a[0]);
        v[1] = 1.0f / std::sqrt(a[1]);
        return v;
    }

    static inline f32x2 sqrt(f32x2 a)
    {
        f32x2 v;
        v[0] = std::sqrt(a[0]);
        v[1] = std::sqrt(a[1]);
        return v;
    }

    static inline f32 dot2(f32x2 a, f32x2 b)
    {
        return a[0] * b[0] + a[1] * b[1];
    }

    // rounding

    static inline f32x2 round(f32x2 s)
    {
        f32x2 v;
        v[0] = std::round(s[0]);
        v[1] = std::round(s[1]);
        return v;
    }

    static inline f32x2 trunc(f32x2 s)
    {
        f32x2 v;
        v[0] = std::trunc(s[0]);
        v[1] = std::trunc(s[1]);
        return v;
    }

    static inline f32x2 floor(f32x2 s)
    {
        f32x2 v;
        v[0] = std::floor(s[0]);
        v[1] = std::floor(s[1]);
        return v;
    }

    static inline f32x2 ceil(f32x2 s)
    {
        f32x2 v;
        v[0] = std::ceil(s[0]);
        v[1] = std::ceil(s[1]);
        return v;
    }

    static inline f32x2 fract(f32x2 s)
    {
        f32x2 v;
        v[0] = s[0] - std::floor(s[0]);
        v[1] = s[1] - std::floor(s[1]);
        return v;
    }

} // namespace mango::simd
