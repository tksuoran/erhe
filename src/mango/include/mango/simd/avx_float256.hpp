/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f32x8
    // -----------------------------------------------------------------

    static inline f32x8 f32x8_zero()
    {
        return _mm256_setzero_ps();
    }

    static inline f32x8 f32x8_set(f32 s)
    {
        return _mm256_set1_ps(s);
    }

    static inline f32x8 f32x8_set(f32 s0, f32 s1, f32 s2, f32 s3, f32 s4, f32 s5, f32 s6, f32 s7)
    {
        return _mm256_setr_ps(s0, s1, s2, s3, s4, s5, s6, s7);
    }

    static inline f32x8 f32x8_uload(const f32* source)
    {
        return _mm256_loadu_ps(source);
    }

    static inline void f32x8_ustore(f32* dest, f32x8 a)
    {
        _mm256_storeu_ps(dest, a);
    }

    static inline f32x8 unpackhi(f32x8 a, f32x8 b)
    {
        return _mm256_unpackhi_ps(a, b);
    }

    static inline f32x8 unpacklo(f32x8 a, f32x8 b)
    {
        return _mm256_unpacklo_ps(a, b);
    }

    // bitwise

    static inline f32x8 bitwise_nand(f32x8 a, f32x8 b)
    {
        return _mm256_andnot_ps(a, b);
    }

    static inline f32x8 bitwise_and(f32x8 a, f32x8 b)
    {
         return _mm256_and_ps(a, b);
    }

    static inline f32x8 bitwise_or(f32x8 a, f32x8 b)
    {
         return _mm256_or_ps(a, b);
    }

    static inline f32x8 bitwise_xor(f32x8 a, f32x8 b)
    {
         return _mm256_xor_ps(a, b);
    }

    static inline f32x8 bitwise_not(f32x8 a)
    {
         return _mm256_xor_ps(a, _mm256_cmp_ps(a, a, _CMP_EQ_OQ));
    }

    static inline f32x8 min(f32x8 a, f32x8 b)
    {
        return _mm256_min_ps(a, b);
    }

    static inline f32x8 max(f32x8 a, f32x8 b)
    {
        return _mm256_max_ps(a, b);
    }

    static inline f32x8 abs(f32x8 a)
    {
        return _mm256_max_ps(a, _mm256_sub_ps(_mm256_setzero_ps(), a));
    }

    static inline f32x8 neg(f32x8 a)
    {
        return _mm256_sub_ps(_mm256_setzero_ps(), a);
    }

    static inline f32x8 sign(f32x8 a)
    {
        __m256 sign_mask = _mm256_set1_ps(-0.0f);
        __m256 value_mask = _mm256_cmp_ps(a, _mm256_setzero_ps(), _CMP_NEQ_UQ);
        __m256 sign_bits = _mm256_and_ps(a, sign_mask);
        __m256 value_bits = _mm256_and_ps(_mm256_set1_ps(1.0f), value_mask);
        return _mm256_or_ps(value_bits, sign_bits);
    }

    static inline f32x8 add(f32x8 a, f32x8 b)
    {
        return _mm256_add_ps(a, b);
    }

    static inline f32x8 sub(f32x8 a, f32x8 b)
    {
        return _mm256_sub_ps(a, b);
    }

    static inline f32x8 mul(f32x8 a, f32x8 b)
    {
        return _mm256_mul_ps(a, b);
    }

    static inline f32x8 div(f32x8 a, f32x8 b)
    {
        return _mm256_div_ps(a, b);
    }

    static inline f32x8 div(f32x8 a, f32 b)
    {
        return _mm256_div_ps(a, _mm256_set1_ps(b));
    }

    static inline f32x8 hadd(f32x8 a, f32x8 b)
    {
        return _mm256_hadd_ps(a, b);
    }

    static inline f32x8 hsub(f32x8 a, f32x8 b)
    {
        return _mm256_hsub_ps(a, b);
    }

#if defined(MANGO_ENABLE_FMA3)

    static inline f32x8 madd(f32x8 a, f32x8 b, f32x8 c)
    {
        // a + b * c
        return _mm256_fmadd_ps(b, c, a);
    }

    static inline f32x8 msub(f32x8 a, f32x8 b, f32x8 c)
    {
        // b * c - a
        return _mm256_fmsub_ps(b, c, a);
    }

    static inline f32x8 nmadd(f32x8 a, f32x8 b, f32x8 c)
    {
        // a - b * c
        return _mm256_fnmadd_ps(b, c, a);
    }

    static inline f32x8 nmsub(f32x8 a, f32x8 b, f32x8 c)
    {
        // -(a + b * c)
        return _mm256_fnmsub_ps(b, c, a);
    }

#else

    static inline f32x8 madd(f32x8 a, f32x8 b, f32x8 c)
    {
        return _mm256_add_ps(a, _mm256_mul_ps(b, c));
    }

    static inline f32x8 msub(f32x8 a, f32x8 b, f32x8 c)
    {
        return _mm256_sub_ps(_mm256_mul_ps(b, c), a);
    }

    static inline f32x8 nmadd(f32x8 a, f32x8 b, f32x8 c)
    {
        return _mm256_sub_ps(a, _mm256_mul_ps(b, c));
    }

    static inline f32x8 nmsub(f32x8 a, f32x8 b, f32x8 c)
    {
        return _mm256_sub_ps(_mm256_setzero_ps(), _mm256_add_ps(a, _mm256_mul_ps(b, c)));
    }

#endif

    static inline f32x8 lerp(f32x8 a, f32x8 b, f32x8 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f32x8 rcp(f32x8 a)
    {
        return _mm256_rcp_ps(a);
    }

    static inline f32x8 rsqrt(f32x8 a)
    {
        return _mm256_rsqrt_ps(a);
    }

    static inline f32x8 sqrt(f32x8 a)
    {
        return _mm256_sqrt_ps(a);
    }

#else

    static inline f32x8 rcp(f32x8 a)
    {
        const __m256 n = _mm256_rcp_ps(a);
        const __m256 m = _mm256_mul_ps(_mm256_mul_ps(n, n), a);
        return _mm256_sub_ps(_mm256_add_ps(n, n), m);
    }

    static inline f32x8 rsqrt(f32x8 a)
    {
        __m256 n = _mm256_rsqrt_ps(a);
        __m256 e = _mm256_mul_ps(_mm256_mul_ps(n, n), a);
        n = _mm256_mul_ps(_mm256_set1_ps(0.5f), n);
        e = _mm256_sub_ps(_mm256_set1_ps(3.0f), e);
        return _mm256_mul_ps(n, e);
    }

    static inline f32x8 sqrt(f32x8 a)
    {
        return _mm256_sqrt_ps(a);
    }

#endif

    // compare

    static inline mask32x8 compare_neq(f32x8 a, f32x8 b)
    {
        return _mm256_castps_si256(_mm256_cmp_ps(a, b, _CMP_NEQ_UQ));
    }

    static inline mask32x8 compare_eq(f32x8 a, f32x8 b)
    {
        return _mm256_castps_si256(_mm256_cmp_ps(a, b, _CMP_EQ_OQ));
    }

    static inline mask32x8 compare_lt(f32x8 a, f32x8 b)
    {
        return _mm256_castps_si256(_mm256_cmp_ps(a, b, _CMP_LT_OQ));
    }

    static inline mask32x8 compare_le(f32x8 a, f32x8 b)
    {
        return _mm256_castps_si256(_mm256_cmp_ps(a, b, _CMP_LE_OQ));
    }

    static inline mask32x8 compare_gt(f32x8 a, f32x8 b)
    {
        return _mm256_castps_si256(_mm256_cmp_ps(b, a, _CMP_LT_OQ));
    }

    static inline mask32x8 compare_ge(f32x8 a, f32x8 b)
    {
        return _mm256_castps_si256(_mm256_cmp_ps(b, a, _CMP_LE_OQ));
    }

    static inline f32x8 select(mask32x8 mask, f32x8 a, f32x8 b)
    {
        return _mm256_blendv_ps(b, a, _mm256_castsi256_ps(mask));
    }

    // rounding

    static inline f32x8 round(f32x8 s)
    {
        return _mm256_round_ps(s, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    }

    static inline f32x8 trunc(f32x8 s)
    {
        return _mm256_round_ps(s, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
    }

    static inline f32x8 floor(f32x8 s)
    {
        return _mm256_round_ps(s, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
    }

    static inline f32x8 ceil(f32x8 s)
    {
        return _mm256_round_ps(s, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
    }

    static inline f32x8 fract(f32x8 s)
    {
        return _mm256_sub_ps(s, floor(s));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

    // zeromask

    static inline f32x8 min(f32x8 a, f32x8 b, mask32x8 mask)
    {
        return _mm256_and_ps(_mm256_castsi256_ps(mask), min(a, b));
    }

    static inline f32x8 max(f32x8 a, f32x8 b, mask32x8 mask)
    {
        return _mm256_and_ps(_mm256_castsi256_ps(mask), max(a, b));
    }

    static inline f32x8 add(f32x8 a, f32x8 b, mask32x8 mask)
    {
        return _mm256_and_ps(_mm256_castsi256_ps(mask), add(a, b));
    }

    static inline f32x8 sub(f32x8 a, f32x8 b, mask32x8 mask)
    {
        return _mm256_and_ps(_mm256_castsi256_ps(mask), sub(a, b));
    }

    static inline f32x8 mul(f32x8 a, f32x8 b, mask32x8 mask)
    {
        return _mm256_and_ps(_mm256_castsi256_ps(mask), mul(a, b));
    }

    static inline f32x8 div(f32x8 a, f32x8 b, mask32x8 mask)
    {
        return _mm256_and_ps(_mm256_castsi256_ps(mask), div(a, b));
    }

    // mask

    static inline f32x8 min(f32x8 a, f32x8 b, mask32x8 mask, f32x8 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline f32x8 max(f32x8 a, f32x8 b, mask32x8 mask, f32x8 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline f32x8 add(f32x8 a, f32x8 b, mask32x8 mask, f32x8 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline f32x8 sub(f32x8 a, f32x8 b, mask32x8 mask, f32x8 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline f32x8 mul(f32x8 a, f32x8 b, mask32x8 mask, f32x8 value)
    {
        return select(mask, mul(a, b), value);
    }

    static inline f32x8 div(f32x8 a, f32x8 b, mask32x8 mask, f32x8 value)
    {
        return select(mask, div(a, b), value);
    }

} // namespace mango::simd
