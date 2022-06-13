/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f32x16
    // -----------------------------------------------------------------

    static inline f32x16 f32x16_zero()
    {
        return _mm512_setzero_ps();
    }

    static inline f32x16 f32x16_set(f32 s)
    {
        return _mm512_set1_ps(s);
    }

    static inline f32x16 f32x16_set(
        f32 s0, f32 s1, f32 s2, f32 s3, f32 s4, f32 s5, f32 s6, f32 s7,
        f32 s8, f32 s9, f32 s10, f32 s11, f32 s12, f32 s13, f32 s14, f32 s15)
    {
        return _mm512_setr_ps(s0, s1, s2, s3, s4, s5, s6, s7,
            s8, s9, s10, s11, s12, s13, s14, s15);
    }

    static inline f32x16 f32x16_uload(const f32* source)
    {
        return _mm512_loadu_ps(source);
    }

    static inline void f32x16_ustore(f32* dest, f32x16 a)
    {
        _mm512_storeu_ps(dest, a);
    }

    static inline f32x16 unpackhi(f32x16 a, f32x16 b)
    {
        return _mm512_unpackhi_ps(a, b);
    }

    static inline f32x16 unpacklo(f32x16 a, f32x16 b)
    {
        return _mm512_unpacklo_ps(a, b);
    }

    // bitwise

    static inline f32x16 bitwise_nand(f32x16 a, f32x16 b)
    {
        return _mm512_andnot_ps(a, b);
    }

    static inline f32x16 bitwise_and(f32x16 a, f32x16 b)
    {
         return _mm512_and_ps(a, b);
    }

    static inline f32x16 bitwise_or(f32x16 a, f32x16 b)
    {
         return _mm512_or_ps(a, b);
    }

    static inline f32x16 bitwise_xor(f32x16 a, f32x16 b)
    {
         return _mm512_xor_ps(a, b);
    }

    static inline f32x16 bitwise_not(f32x16 a)
    {
        const __m512i s = _mm512_castps_si512(a);
        return _mm512_castsi512_ps(_mm512_ternarylogic_epi32(s, s, s, 0x01));
    }

    static inline f32x16 min(f32x16 a, f32x16 b)
    {
        return _mm512_min_ps(a, b);
    }

    static inline f32x16 min(f32x16 a, f32x16 b, mask32x16 mask)
    {
        return _mm512_maskz_min_ps(mask, a, b);
    }

    static inline f32x16 min(f32x16 a, f32x16 b, mask32x16 mask, f32x16 value)
    {
        return _mm512_mask_min_ps(value, mask, a, b);
    }

    static inline f32x16 max(f32x16 a, f32x16 b)
    {
        return _mm512_max_ps(a, b);
    }

    static inline f32x16 max(f32x16 a, f32x16 b, mask32x16 mask)
    {
        return _mm512_maskz_max_ps(mask, a, b);
    }

    static inline f32x16 max(f32x16 a, f32x16 b, mask32x16 mask, f32x16 value)
    {
        return _mm512_mask_max_ps(value, mask, a, b);
    }

    static inline f32x16 abs(f32x16 a)
    {
        return _mm512_abs_ps(a);
    }

    static inline f32x16 neg(f32x16 a)
    {
        return _mm512_sub_ps(_mm512_setzero_ps(), a);
    }

    static inline f32x16 sign(f32x16 a)
    {
        __m512 zero = _mm512_setzero_ps();
        __m512 negative = _mm512_mask_blend_ps(_mm512_cmp_ps_mask(a, zero, _CMP_LT_OS), zero, _mm512_set1_ps(-1.0f));
        __m512 positive = _mm512_mask_blend_ps(_mm512_cmp_ps_mask(zero, a, _CMP_LT_OS), zero, _mm512_set1_ps( 1.0f));
        return _mm512_or_ps(negative, positive);
    }

    static inline f32x16 add(f32x16 a, f32x16 b)
    {
        return _mm512_add_ps(a, b);
    }

    static inline f32x16 add(f32x16 a, f32x16 b, mask32x16 mask)
    {
        return _mm512_maskz_add_ps(mask, a, b);
    }

    static inline f32x16 add(f32x16 a, f32x16 b, mask32x16 mask, f32x16 value)
    {
        return _mm512_mask_add_ps(value, mask, a, b);
    }

    static inline f32x16 sub(f32x16 a, f32x16 b)
    {
        return _mm512_sub_ps(a, b);
    }

    static inline f32x16 sub(f32x16 a, f32x16 b, mask32x16 mask)
    {
        return _mm512_maskz_sub_ps(mask, a, b);
    }

    static inline f32x16 sub(f32x16 a, f32x16 b, mask32x16 mask, f32x16 value)
    {
        return _mm512_mask_sub_ps(value, mask, a, b);
    }

    static inline f32x16 mul(f32x16 a, f32x16 b)
    {
        return _mm512_mul_ps(a, b);
    }

    static inline f32x16 mul(f32x16 a, f32x16 b, mask32x16 mask)
    {
        return _mm512_maskz_mul_ps(mask, a, b);
    }

    static inline f32x16 mul(f32x16 a, f32x16 b, mask32x16 mask, f32x16 value)
    {
        return _mm512_mask_mul_ps(value, mask, a, b);
    }

    static inline f32x16 div(f32x16 a, f32x16 b)
    {
        return _mm512_div_ps(a, b);
    }

    static inline f32x16 div(f32x16 a, f32x16 b, mask32x16 mask)
    {
        return _mm512_maskz_div_ps(mask, a, b);
    }

    static inline f32x16 div(f32x16 a, f32x16 b, mask32x16 mask, f32x16 value)
    {
        return _mm512_mask_div_ps(value, mask, a, b);
    }

    static inline f32x16 div(f32x16 a, f32 b)
    {
        return _mm512_div_ps(a, _mm512_set1_ps(b));
    }

#if defined(MANGO_ENABLE_FMA3)

    static inline f32x16 madd(f32x16 a, f32x16 b, f32x16 c)
    {
        // a + b * c
        return _mm512_fmadd_ps(b, c, a);
    }

    static inline f32x16 msub(f32x16 a, f32x16 b, f32x16 c)
    {
        // b * c - a
        return _mm512_fmsub_ps(b, c, a);
    }

    static inline f32x16 nmadd(f32x16 a, f32x16 b, f32x16 c)
    {
        // a - b * c
        return _mm512_fnmadd_ps(b, c, a);
    }

    static inline f32x16 nmsub(f32x16 a, f32x16 b, f32x16 c)
    {
        // -(a + b * c)
        return _mm512_fnmsub_ps(b, c, a);
    }

#else

    static inline f32x16 madd(f32x16 a, f32x16 b, f32x16 c)
    {
        return _mm512_add_ps(a, _mm512_mul_ps(b, c));
    }

    static inline f32x16 msub(f32x16 a, f32x16 b, f32x16 c)
    {
        return _mm512_sub_ps(_mm512_mul_ps(b, c), a);
    }

    static inline f32x16 nmadd(f32x16 a, f32x16 b, f32x16 c)
    {
        return _mm512_sub_ps(a, _mm512_mul_ps(b, c));
    }

    static inline f32x16 nmsub(f32x16 a, f32x16 b, f32x16 c)
    {
        return _mm512_sub_ps(_mm512_setzero_ps(), _mm512_add_ps(a, _mm512_mul_ps(b, c)));
    }

#endif

    static inline f32x16 lerp(f32x16 a, f32x16 b, f32x16 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f32x16 rcp(f32x16 a)
    {
        return _mm512_rcp14_ps(a);
    }

    static inline f32x16 rsqrt(f32x16 a)
    {
        return _mm512_rsqrt14_ps(a);
    }

#else

    static inline f32x16 rcp(f32x16 a)
    {
        return _mm512_rcp28_ps(a);
    }

    static inline f32x16 rsqrt(f32x16 a)
    {
        return _mm512_rsqrt28_ps(a);
    }

#endif

    static inline f32x16 sqrt(f32x16 a)
    {
        return _mm512_sqrt_ps(a);
    }

    // compare

    static inline mask32x16 compare_neq(f32x16 a, f32x16 b)
    {
        return _mm512_cmp_ps_mask(a, b, _CMP_NEQ_UQ);
    }

    static inline mask32x16 compare_eq(f32x16 a, f32x16 b)
    {
        return _mm512_cmp_ps_mask(a, b, _CMP_EQ_OQ);
    }

    static inline mask32x16 compare_lt(f32x16 a, f32x16 b)
    {
        return _mm512_cmp_ps_mask(a, b, _CMP_LT_OS);
    }

    static inline mask32x16 compare_le(f32x16 a, f32x16 b)
    {
        return _mm512_cmp_ps_mask(a, b, _CMP_LE_OS);
    }

    static inline mask32x16 compare_gt(f32x16 a, f32x16 b)
    {
        return _mm512_cmp_ps_mask(b, a, _CMP_LT_OS);
    }

    static inline mask32x16 compare_ge(f32x16 a, f32x16 b)
    {
        return _mm512_cmp_ps_mask(b, a, _CMP_LE_OS);
    }

    static inline f32x16 select(mask32x16 mask, f32x16 a, f32x16 b)
    {
        return _mm512_mask_blend_ps(mask, b, a);
    }

    // rounding

    static inline f32x16 round(f32x16 s)
    {
        return _mm512_add_round_ps(s, _mm512_setzero_ps(), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    }

    static inline f32x16 trunc(f32x16 s)
    {
        //return _mm512_roundscale_ps(s, 0x13);
        return _mm512_add_round_ps(s, _mm512_setzero_ps(), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
    }

    static inline f32x16 floor(f32x16 s)
    {
        //return _mm512_roundscale_ps(s, 0x11);
        return _mm512_add_round_ps(s, _mm512_setzero_ps(), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
    }

    static inline f32x16 ceil(f32x16 s)
    {
        //return _mm512_roundscale_ps(s, 0x12);
        return _mm512_add_round_ps(s, _mm512_setzero_ps(), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
    }

    static inline f32x16 fract(f32x16 s)
    {
        return _mm512_sub_ps(s, floor(s));
    }

} // namespace mango::simd
