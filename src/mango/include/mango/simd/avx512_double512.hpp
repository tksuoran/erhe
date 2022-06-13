/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f64x8
    // -----------------------------------------------------------------

    static inline f64x8 f64x8_zero()
    {
        return _mm512_setzero_pd();
    }

    static inline f64x8 f64x8_set(f64 s)
    {
        return _mm512_set1_pd(s);
    }

    static inline f64x8 f64x8_set(f64 s0, f64 s1, f64 s2, f64 s3, f64 s4, f64 s5, f64 s6, f64 s7)
    {
        return _mm512_setr_pd(s0, s1, s2, s3, s4, s5, s6, s7);
    }

    static inline f64x8 f64x8_uload(const f64* source)
    {
        return _mm512_loadu_pd(source);
    }

    static inline void f64x8_ustore(f64* dest, f64x8 a)
    {
        _mm512_storeu_pd(dest, a);
    }

    static inline f64x8 unpackhi(f64x8 a, f64x8 b)
    {
        return _mm512_unpackhi_pd(a, b);
    }

    static inline f64x8 unpacklo(f64x8 a, f64x8 b)
    {
        return _mm512_unpacklo_pd(a, b);
    }

    // bitwise

    static inline f64x8 bitwise_nand(f64x8 a, f64x8 b)
    {
        return _mm512_andnot_pd(a, b);
    }

    static inline f64x8 bitwise_and(f64x8 a, f64x8 b)
    {
        return _mm512_and_pd(a, b);
    }

    static inline f64x8 bitwise_or(f64x8 a, f64x8 b)
    {
        return _mm512_or_pd(a, b);
    }

    static inline f64x8 bitwise_xor(f64x8 a, f64x8 b)
    {
        return _mm512_xor_pd(a, b);
    }

    static inline f64x8 bitwise_not(f64x8 a)
    {
        const __m512i s = _mm512_castpd_si512(a);
        return _mm512_castsi512_pd(_mm512_ternarylogic_epi64(s, s, s, 0x01));
    }

    static inline f64x8 min(f64x8 a, f64x8 b)
    {
        return _mm512_min_pd(a, b);
    }

    static inline f64x8 min(f64x8 a, f64x8 b, mask64x8 mask)
    {
        return _mm512_maskz_min_pd(mask, a, b);
    }

    static inline f64x8 min(f64x8 a, f64x8 b, mask64x8 mask, f64x8 value)
    {
        return _mm512_mask_min_pd(value, mask, a, b);
    }

    static inline f64x8 max(f64x8 a, f64x8 b)
    {
        return _mm512_max_pd(a, b);
    }

    static inline f64x8 max(f64x8 a, f64x8 b, mask64x8 mask)
    {
        return _mm512_maskz_max_pd(mask, a, b);
    }

    static inline f64x8 max(f64x8 a, f64x8 b, mask64x8 mask, f64x8 value)
    {
        return _mm512_mask_max_pd(value, mask, a, b);
    }

#if __GNUC__ < 8

    static inline f64x8 abs(f64x8 a)
    {
        // gcc 7.1 compiler bug: "expects __m512 argument"
        return _mm512_max_pd(a, _mm512_sub_pd(_mm512_setzero_pd(), a));
    }

#else

    static inline f64x8 abs(f64x8 a)
    {
        return _mm512_abs_pd(a);
    }

#endif

    static inline f64x8 neg(f64x8 a)
    {
        return _mm512_sub_pd(_mm512_setzero_pd(), a);
    }

    static inline f64x8 sign(f64x8 a)
    {
        __m512d zero = _mm512_setzero_pd();
        __m512d negative = _mm512_mask_blend_pd(_mm512_cmp_pd_mask(a, zero, _CMP_LT_OS), zero, _mm512_set1_pd(-1.0));
        __m512d positive = _mm512_mask_blend_pd(_mm512_cmp_pd_mask(zero, a, _CMP_LT_OS), zero, _mm512_set1_pd( 1.0));
        return _mm512_or_pd(negative, positive);
    }

    static inline f64x8 add(f64x8 a, f64x8 b)
    {
        return _mm512_add_pd(a, b);
    }

    static inline f64x8 add(f64x8 a, f64x8 b, mask64x8 mask)
    {
        return _mm512_maskz_add_pd(mask, a, b);
    }

    static inline f64x8 add(f64x8 a, f64x8 b, mask64x8 mask, f64x8 value)
    {
        return _mm512_mask_add_pd(value, mask, a, b);
    }

    static inline f64x8 sub(f64x8 a, f64x8 b)
    {
        return _mm512_sub_pd(a, b);
    }

    static inline f64x8 sub(f64x8 a, f64x8 b, mask64x8 mask)
    {
        return _mm512_maskz_sub_pd(mask, a, b);
    }

    static inline f64x8 sub(f64x8 a, f64x8 b, mask64x8 mask, f64x8 value)
    {
        return _mm512_mask_sub_pd(value, mask, a, b);
    }

    static inline f64x8 mul(f64x8 a, f64x8 b)
    {
        return _mm512_mul_pd(a, b);
    }

    static inline f64x8 mul(f64x8 a, f64x8 b, mask64x8 mask)
    {
        return _mm512_maskz_mul_pd(mask, a, b);
    }

    static inline f64x8 mul(f64x8 a, f64x8 b, mask64x8 mask, f64x8 value)
    {
        return _mm512_mask_mul_pd(value, mask, a, b);
    }

    static inline f64x8 div(f64x8 a, f64x8 b)
    {
        return _mm512_div_pd(a, b);
    }

    static inline f64x8 div(f64x8 a, f64x8 b, mask64x8 mask)
    {
        return _mm512_maskz_div_pd(mask, a, b);
    }

    static inline f64x8 div(f64x8 a, f64x8 b, mask64x8 mask, f64x8 value)
    {
        return _mm512_mask_div_pd(value, mask, a, b);
    }

    static inline f64x8 div(f64x8 a, f64 b)
    {
        return _mm512_div_pd(a, _mm512_set1_pd(b));
    }

#if defined(MANGO_ENABLE_FMA3)

    static inline f64x8 madd(f64x8 a, f64x8 b, f64x8 c)
    {
        // a + b * c
        return _mm512_fmadd_pd(b, c, a);
    }

    static inline f64x8 msub(f64x8 a, f64x8 b, f64x8 c)
    {
        // b * c - a
        return _mm512_fmsub_pd(b, c, a);
    }

    static inline f64x8 nmadd(f64x8 a, f64x8 b, f64x8 c)
    {
        // a - b * c
        return _mm512_fnmadd_pd(b, c, a);
    }

    static inline f64x8 nmsub(f64x8 a, f64x8 b, f64x8 c)
    {
        // -(a + b * c)
        return _mm512_fnmsub_pd(b, c, a);
    }

#else

    static inline f64x8 madd(f64x8 a, f64x8 b, f64x8 c)
    {
        return _mm512_add_pd(a, _mm512_mul_pd(b, c));
    }

    static inline f64x8 msub(f64x8 a, f64x8 b, f64x8 c)
    {
        return _mm512_sub_pd(_mm512_mul_pd(b, c), a);
    }

    static inline f64x8 nmadd(f64x8 a, f64x8 b, f64x8 c)
    {
        return _mm512_sub_pd(a, _mm512_mul_pd(b, c));
    }

    static inline f64x8 nmsub(f64x8 a, f64x8 b, f64x8 c)
    {
        return _mm512_sub_pd(_mm512_setzero_pd(), _mm512_add_pd(a, _mm512_mul_pd(b, c)));
    }

#endif

    static inline f64x8 lerp(f64x8 a, f64x8 b, f64x8 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f64x8 rcp(f64x8 a)
    {
        return _mm512_rcp14_pd(a);
    }

    static inline f64x8 rsqrt(f64x8 a)
    {
        return _mm512_rsqrt14_pd(a);
    }

#else

    static inline f64x8 rcp(f64x8 a)
    {
        return _mm512_rcp28_pd(a);
    }

    static inline f64x8 rsqrt(f64x8 a)
    {
        return _mm512_rsqrt28_pd(a);
    }

#endif // MANGO_FAST_MATH

    static inline f64x8 sqrt(f64x8 a)
    {
        return _mm512_sqrt_pd(a);
    }

    // compare

    static inline mask64x8 compare_neq(f64x8 a, f64x8 b)
    {
        return _mm512_cmp_pd_mask(a, b, _CMP_NEQ_UQ);
    }

    static inline mask64x8 compare_eq(f64x8 a, f64x8 b)
    {
        return _mm512_cmp_pd_mask(a, b, _CMP_EQ_OQ);
    }

    static inline mask64x8 compare_lt(f64x8 a, f64x8 b)
    {
        return _mm512_cmp_pd_mask(a, b, _CMP_LT_OS);
    }

    static inline mask64x8 compare_le(f64x8 a, f64x8 b)
    {
        return _mm512_cmp_pd_mask(a, b, _CMP_LE_OS);
    }

    static inline mask64x8 compare_gt(f64x8 a, f64x8 b)
    {
        return _mm512_cmp_pd_mask(b, a, _CMP_LT_OS);
    }

    static inline mask64x8 compare_ge(f64x8 a, f64x8 b)
    {
        return _mm512_cmp_pd_mask(b, a, _CMP_LE_OS);
    }

    static inline f64x8 select(mask64x8 mask, f64x8 a, f64x8 b)
    {
        return _mm512_mask_blend_pd(mask, b, a);
    }

    // rounding

    static inline f64x8 round(f64x8 s)
    {
        return _mm512_add_round_pd(s, _mm512_setzero_pd(), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    }

    static inline f64x8 trunc(f64x8 s)
    {
        return _mm512_add_round_pd(s, _mm512_setzero_pd(), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
    }

    static inline f64x8 floor(f64x8 s)
    {
        return _mm512_add_round_pd(s, _mm512_setzero_pd(), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
    }

    static inline f64x8 ceil(f64x8 s)
    {
        return _mm512_add_round_pd(s, _mm512_setzero_pd(), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
    }

    static inline f64x8 fract(f64x8 s)
    {
        return _mm512_sub_pd(s, floor(s));
    }

} // namespace mango::simd
