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

#ifdef __aarch64__

#ifdef MANGO_COMPILER_CLANG

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 v)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return __builtin_shufflevector(v.data, v.data, x, y + 2);
    }

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 a, f64x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return __builtin_shufflevector(a.data, b.data, x, y + 2);
    }

#else

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 v)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        float64x2_t result;
	    result = vmovq_n_f64(vgetq_lane_f64(v, x));
	    result = vsetq_lane_f64(vgetq_lane_f64(v, y), result, 1);
        return result;
    }

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 a, f64x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        float64x2_t result;
	    result = vmovq_n_f64(vgetq_lane_f64(a, x));
	    result = vsetq_lane_f64(vgetq_lane_f64(b, y), result, 1);
        return result;
    }

#endif // MANGO_COMPILER_CLANG

    // indexed access

    template <unsigned int Index>
    static inline f64x2 set_component(f64x2 a, f64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return vsetq_lane_f64(s, a, Index);
    }

    template <unsigned int Index>
    static inline f64 get_component(f64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return vgetq_lane_f64(a, Index);
    }

    static inline f64x2 f64x2_zero()
    {
        return vdupq_n_f64(0.0);
    }

    static inline f64x2 f64x2_set(f64 s)
    {
        return vdupq_n_f64(s);
    }

    static inline f64x2 f64x2_set(f64 x, f64 y)
    {
        float64x2_t temp = { x, y };
        return temp;
    }

#if defined(MANGO_COMPILER_GCC)

    static inline f64x2 f64x2_uload(const f64* source)
    {
        f64x2 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void f64x2_ustore(f64* dest, f64x2 a)
    {
        std::memcpy(dest, &a, sizeof(a));
    }

#else

    static inline f64x2 f64x2_uload(const f64* source)
    {
        return vld1q_f64(source);
    }

    static inline void f64x2_ustore(f64* dest, f64x2 a)
    {
        vst1q_f64(dest, a);
    }

#endif // MANGO_COMPILER_GCC

    static inline f64x2 unpackhi(f64x2 a, f64x2 b)
    {
        return vcombine_f64(vget_high_f64(a), vget_high_f64(b));
    }

    static inline f64x2 unpacklo(f64x2 a, f64x2 b)
    {
        return vcombine_f64(vget_low_f64(a), vget_low_f64(b));
    }

    // bitwise

    static inline f64x2 bitwise_nand(f64x2 a, f64x2 b)
    {
        return vreinterpretq_f64_s64(vbicq_s64(vreinterpretq_s64_f64(a), vreinterpretq_s64_f64(b)));
    }

    static inline f64x2 bitwise_and(f64x2 a, f64x2 b)
    {
        return vreinterpretq_f64_s64(vandq_s64(vreinterpretq_s64_f64(a), vreinterpretq_s64_f64(b)));
    }

    static inline f64x2 bitwise_or(f64x2 a, f64x2 b)
    {
        return vreinterpretq_f64_s64(vorrq_s64(vreinterpretq_s64_f64(a), vreinterpretq_s64_f64(b)));
    }

    static inline f64x2 bitwise_xor(f64x2 a, f64x2 b)
    {
        return vreinterpretq_f64_s64(veorq_s64(vreinterpretq_s64_f64(a), vreinterpretq_s64_f64(b)));
    }

    static inline f64x2 bitwise_not(f64x2 a)
    {
        return vreinterpretq_f64_u64(veorq_u64(vreinterpretq_u64_f64(a), vceqq_f64(a, a)));
    }

    static inline f64x2 min(f64x2 a, f64x2 b)
    {
        return vminq_f64(a, b);
    }

    static inline f64x2 max(f64x2 a, f64x2 b)
    {
        return vmaxq_f64(a, b);
    }

    static inline f64x2 abs(f64x2 a)
    {
        return vabsq_f64(a);
    }

    static inline f64x2 neg(f64x2 a)
    {
        return vnegq_f64(a);
    }

    static inline f64x2 sign(f64x2 a)
    {
        f64 x = vgetq_lane_f64(a, 0);
        f64 y = vgetq_lane_f64(a, 1);
        x = x < 0 ? -1.0 : (x > 0 ? 1.0 : 0.0);
        y = y < 0 ? -1.0 : (y > 0 ? 1.0 : 0.0);
        return f64x2_set(x, y);
    }

    static inline f64x2 add(f64x2 a, f64x2 b)
    {
        return vaddq_f64(a, b);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b)
    {
        return vsubq_f64(a, b);
    }

    static inline f64x2 mul(f64x2 a, f64x2 b)
    {
        return vmulq_f64(a, b);
    }

    static inline f64x2 div(f64x2 a, f64x2 b)
    {
        return vdivq_f64(a, b);
    }

    static inline f64x2 div(f64x2 a, f64 b)
    {
        f64x2 s = vdupq_n_f64(b);
        return vdivq_f64(a, s);
    }

    static inline f64x2 hadd(f64x2 a, f64x2 b)
    {
        return vpaddq_f64(a, b);
    }

    static inline f64x2 hsub(f64x2 a, f64x2 b)
    {
        b = vnegq_f64(b);
        return vpaddq_f64(a, b);
    }

    static inline f64x2 madd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a + b * c
        return vmlaq_f64(a, b, c);
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        // b * c - a
        return vnegq_f64(vmlsq_f64(a, b, c));
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a - b * c
        return vmlsq_f64(a, b, c);
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        // -(a + b * c)
        return vnegq_f64(vmlaq_f64(a, b, c));
    }

    static inline f64x2 lerp(f64x2 a, f64x2 b, f64x2 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f64x2 rcp(f64x2 a)
    {
        f64x2 e = vrecpeq_f64(a);
        e = vmulq_f64(vrecpsq_f64(a, e), e);
        return e;
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        f64x2 e = vrsqrteq_f64(a);
        e = vmulq_f64(vrsqrtsq_f64(vmulq_f64(a, e), e), e);
        return e;
    }

#else

    static inline f64x2 rcp(f64x2 a)
    {
        f64x2 e = vrecpeq_f64(a);
        e = vmulq_f64(vrecpsq_f64(a, e), e);
        e = vmulq_f64(vrecpsq_f64(a, e), e);
        return e;
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        f64x2 e = vrsqrteq_f64(a);
        e = vmulq_f64(vrsqrtsq_f64(vmulq_f64(a, e), e), e);
        e = vmulq_f64(vrsqrtsq_f64(vmulq_f64(a, e), e), e);
        return e;
    }

#endif // MANGO_FAST_MATH

    static inline f64x2 sqrt(f64x2 a)
    {
        return vsqrtq_f64(a);
    }

    static inline f64 dot2(f64x2 a, f64x2 b)
    {
        const float64x2_t prod = vmulq_f64(a, b);
        return vaddvq_f64(prod);
    }

    // compare

    static inline mask64x2 compare_neq(f64x2 a, f64x2 b)
    {
        return veorq_u64(vceqq_f64(a, b), vceqq_f64(a, a));
    }

    static inline mask64x2 compare_eq(f64x2 a, f64x2 b)
    {
        return vceqq_f64(a, b);
    }

    static inline mask64x2 compare_lt(f64x2 a, f64x2 b)
    {
        return vcltq_f64(a, b);
    }

    static inline mask64x2 compare_le(f64x2 a, f64x2 b)
    {
        return vcleq_f64(a, b);
    }

    static inline mask64x2 compare_gt(f64x2 a, f64x2 b)
    {
        return vcgtq_f64(a, b);
    }

    static inline mask64x2 compare_ge(f64x2 a, f64x2 b)
    {
        return vcgeq_f64(a, b);
    }

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        return vbslq_f64(mask, a, b);
    }

    // rounding

    static inline f64x2 round(f64x2 s)
    {
        return vrndaq_f64(s);
    }

    static inline f64x2 trunc(f64x2 s)
    {
        return vrndq_f64(s);
    }

    static inline f64x2 floor(f64x2 s)
    {
        return vrndmq_f64(s);
    }

    static inline f64x2 ceil(f64x2 s)
    {
        return vrndpq_f64(s);
    }

    static inline f64x2 fract(f64x2 s)
    {
        return sub(s, floor(s));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

    static inline f64x2 min(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return vreinterpretq_f64_u64(vandq_u64(mask, vreinterpretq_u64_f64(min(a, b))));
    }

    static inline f64x2 max(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return vreinterpretq_f64_u64(vandq_u64(mask, vreinterpretq_u64_f64(max(a, b))));
    }

    static inline f64x2 add(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return vreinterpretq_f64_u64(vandq_u64(mask, vreinterpretq_u64_f64(add(a, b))));
    }

    static inline f64x2 sub(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return vreinterpretq_f64_u64(vandq_u64(mask, vreinterpretq_u64_f64(sub(a, b))));
    }

    static inline f64x2 mul(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return vreinterpretq_f64_u64(vandq_u64(mask, vreinterpretq_u64_f64(mul(a, b))));
    }

    static inline f64x2 div(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return vreinterpretq_f64_u64(vandq_u64(mask, vreinterpretq_u64_f64(div(a, b))));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

#define SIMD_MASK_DOUBLE128
#include "common_mask.hpp"
#undef SIMD_MASK_DOUBLE128

#else // __aarch64__

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 v)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return {{ v.data[x], v.data[y] }};
    }

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 a, f64x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return {{ a.data[x], b.data[y] }};
    }

    // indexed access

    template <unsigned int Index>
    static inline f64x2 set_component(f64x2 a, f64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        a.data[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline f64 get_component(f64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return a.data[Index];
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
        dest[0] = a.data[0];
        dest[1] = a.data[1];
    }

    static inline f64x2 unpackhi(f64x2 a, f64x2 b)
    {
        return f64x2_set(a.data[1], b.data[1]);
    }

    static inline f64x2 unpacklo(f64x2 a, f64x2 b)
    {
        return f64x2_set(a.data[0], b.data[0]);
    }

    // bitwise

    static inline f64x2 bitwise_nand(f64x2 a, f64x2 b)
    {
        const Double x(~Double(a.data[0]).u & Double(b.data[0]).u);
        const Double y(~Double(a.data[1]).u & Double(b.data[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_and(f64x2 a, f64x2 b)
    {
        const Double x(Double(a.data[0]).u & Double(b.data[0]).u);
        const Double y(Double(a.data[1]).u & Double(b.data[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_or(f64x2 a, f64x2 b)
    {
        const Double x(Double(a.data[0]).u | Double(b.data[0]).u);
        const Double y(Double(a.data[1]).u | Double(b.data[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_xor(f64x2 a, f64x2 b)
    {
        const Double x(Double(a.data[0]).u ^ Double(b.data[0]).u);
        const Double y(Double(a.data[1]).u ^ Double(b.data[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 bitwise_not(f64x2 a)
    {
        const Double x(~Double(a.data[0]).u);
        const Double y(~Double(a.data[1]).u);
        return f64x2_set(x, y);
    }

    static inline f64x2 min(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v.data[0] = std::min(a.data[0], b.data[0]);
        v.data[1] = std::min(a.data[1], b.data[1]);
        return v;
    }

    static inline f64x2 max(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v.data[0] = std::max(a.data[0], b.data[0]);
        v.data[1] = std::max(a.data[1], b.data[1]);
        return v;
    }

    static inline f64x2 abs(f64x2 a)
    {
        f64x2 v;
        v.data[0] = std::abs(a.data[0]);
        v.data[1] = std::abs(a.data[1]);
        return v;
    }

    static inline f64x2 neg(f64x2 a)
    {
        return f64x2_set(-a.data[0], -a.data[1]);
    }

    static inline f64x2 sign(f64x2 a)
    {
        f64x2 v;
        v.data[0] = a.data[0] < 0 ? -1.0f : (a.data[0] > 0 ? 1.0f : 0.0f);
        v.data[1] = a.data[1] < 0 ? -1.0f : (a.data[1] > 0 ? 1.0f : 0.0f);
        return v;
    }

    static inline f64x2 add(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v.data[0] = a.data[0] + b.data[0];
        v.data[1] = a.data[1] + b.data[1];
        return v;
    }

    static inline f64x2 sub(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v.data[0] = a.data[0] - b.data[0];
        v.data[1] = a.data[1] - b.data[1];
        return v;
    }

    static inline f64x2 mul(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v.data[0] = a.data[0] * b.data[0];
        v.data[1] = a.data[1] * b.data[1];
        return v;
    }

    static inline f64x2 div(f64x2 a, f64x2 b)
    {
        f64x2 v;
        v.data[0] = a.data[0] / b.data[0];
        v.data[1] = a.data[1] / b.data[1];
        return v;
    }

    static inline f64x2 div(f64x2 a, f64 b)
    {
        f64x2 v;
        v.data[0] = a.data[0] / b;
        v.data[1] = a.data[1] / b;
        return v;
    }

    static inline f64x2 hadd(f64x2 a, f64x2 b)
    {
	    f64x2 v;
	    v.data[0] = a.data[0] + a.data[1];
	    v.data[1] = b.data[0] + b.data[1];
	    return v;
    }

    static inline f64x2 hsub(f64x2 a, f64x2 b)
    {
	    f64x2 v;
	    v.data[0] = a.data[0] - a.data[1];
	    v.data[1] = b.data[0] - b.data[1];
	    return v;
    }

    static inline f64x2 madd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a + b * c
        f64x2 v;
        v.data[0] = a.data[0] + b.data[0] * c.data[0];
        v.data[1] = a.data[1] + b.data[1] * c.data[1];
        return v;
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        // b * c - a
        f64x2 v;
        v.data[0] = b.data[0] * c.data[0] - a.data[0];
        v.data[1] = b.data[1] * c.data[1] - a.data[1];
        return v;
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a - b * c
        f64x2 v;
        v.data[0] = a.data[0] - b.data[0] * c.data[0];
        v.data[1] = a.data[1] - b.data[1] * c.data[1];
        return v;
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        // -(a + b * c)
        f64x2 v;
        v.data[0] = -(a.data[0] + b.data[0] * c.data[0]);
        v.data[1] = -(a.data[1] + b.data[1] * c.data[1]);
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
        v.data[0] = 1.0 / a.data[0];
        v.data[1] = 1.0 / a.data[1];
        return v;
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        f64x2 v;
        v.data[0] = 1.0 / std::sqrt(a.data[0]);
        v.data[1] = 1.0 / std::sqrt(a.data[1]);
        return v;
    }

    static inline f64x2 sqrt(f64x2 a)
    {
        f64x2 v;
        v.data[0] = std::sqrt(a.data[0]);
        v.data[1] = std::sqrt(a.data[1]);
        return v;
    }

    static inline f64 dot2(f64x2 a, f64x2 b)
    {
        return a.data[0] * b.data[0] + a.data[1] * b.data[1];
    }

    // compare

    static inline mask64x2 compare_neq(f64x2 a, f64x2 b)
    {
        u64 x = a.data[0] != b.data[0] ? ~0 : 0;
        u64 y = a.data[1] != b.data[1] ? ~0 : 0;
        uint64x2_t mask = { x, y };
        return mask;
    }

    static inline mask64x2 compare_eq(f64x2 a, f64x2 b)
    {
        u64 x = a.data[0] == b.data[0] ? ~0 : 0;
        u64 y = a.data[1] == b.data[1] ? ~0 : 0;
        uint64x2_t mask = { x, y };
        return mask;
    }

    static inline mask64x2 compare_lt(f64x2 a, f64x2 b)
    {
        u64 x = a.data[0] < b.data[0] ? ~0 : 0;
        u64 y = a.data[1] < b.data[1] ? ~0 : 0;
        uint64x2_t mask = { x, y };
        return mask;
    }

    static inline mask64x2 compare_le(f64x2 a, f64x2 b)
    {
        u64 x = a.data[0] <= b.data[0] ? ~0 : 0;
        u64 y = a.data[1] <= b.data[1] ? ~0 : 0;
        uint64x2_t mask = { x, y };
        return mask;
    }

    static inline mask64x2 compare_gt(f64x2 a, f64x2 b)
    {
        u64 x = a.data[0] > b.data[0] ? ~0 : 0;
        u64 y = a.data[1] > b.data[1] ? ~0 : 0;
        uint64x2_t mask = { x, y };
        return mask;
    }

    static inline mask64x2 compare_ge(f64x2 a, f64x2 b)
    {
        u64 x = a.data[0] >= b.data[0] ? ~0 : 0;
        u64 y = a.data[1] >= b.data[1] ? ~0 : 0;
        uint64x2_t mask = { x, y };
        return mask;
    }

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        f64x2 m;
        m.data[0] = vgetq_lane_u64(mask, 0);
        m.data[1] = vgetq_lane_u64(mask, 1);
        return bitwise_or(bitwise_and(m, a), bitwise_nand(m, b));
    }

    // rounding

    static inline f64x2 round(f64x2 s)
    {
        f64x2 v;
        v.data[0] = std::round(s.data[0]);
        v.data[1] = std::round(s.data[1]);
        return v;
    }

    static inline f64x2 trunc(f64x2 s)
    {
        f64x2 v;
        v.data[0] = std::trunc(s.data[0]);
        v.data[1] = std::trunc(s.data[1]);
        return v;
    }

    static inline f64x2 floor(f64x2 s)
    {
        f64x2 v;
        v.data[0] = std::floor(s.data[0]);
        v.data[1] = std::floor(s.data[1]);
        return v;
    }

    static inline f64x2 ceil(f64x2 s)
    {
        f64x2 v;
        v.data[0] = std::ceil(s.data[0]);
        v.data[1] = std::ceil(s.data[1]);
        return v;
    }

    static inline f64x2 fract(f64x2 s)
    {
        f64x2 v;
        v.data[0] = s.data[0] - std::floor(s.data[0]);
        v.data[1] = s.data[1] - std::floor(s.data[1]);
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

#endif // __aarch64__

} // namespace mango::simd
