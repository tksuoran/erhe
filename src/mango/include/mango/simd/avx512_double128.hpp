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
        return _mm_shuffle_pd(v, v, y * 2 + x);
    }

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 a, f64x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        return _mm_shuffle_pd(a, b, y * 2 + x);
    }

    // set component

    template <int Index>
    static inline f64x2 set_component(f64x2 a, double s);

    template <>
    inline f64x2 set_component<0>(f64x2 a, double x)
    {
        return _mm_move_sd(a, _mm_set1_pd(x));
    }

    template <>
    inline f64x2 set_component<1>(f64x2 a, double y)
    {
        return _mm_move_sd(_mm_set1_pd(y), a);
    }

    // get component

    template <int Index>
    static inline double get_component(f64x2 a);

    template <>
    inline double get_component<0>(f64x2 a)
    {
        return _mm_cvtsd_f64(a);
    }

    template <>
    inline double get_component<1>(f64x2 a)
    {
        const __m128d yy = _mm_unpackhi_pd(a, a);
        return _mm_cvtsd_f64(yy);
    }

    static inline f64x2 f64x2_zero()
    {
        return _mm_setzero_pd();
    }

    static inline f64x2 f64x2_set(double s)
    {
        return _mm_set1_pd(s);
    }

    static inline f64x2 f64x2_set(double x, double y)
    {
        return _mm_setr_pd(x, y);
    }

    static inline f64x2 f64x2_uload(const double* source)
    {
        return _mm_loadu_pd(source);
    }

    static inline void f64x2_ustore(double* dest, f64x2 a)
    {
        _mm_storeu_pd(dest, a);
    }

    static inline f64x2 unpackhi(f64x2 a, f64x2 b)
    {
        return _mm_unpackhi_pd(a, b);
    }

    static inline f64x2 unpacklo(f64x2 a, f64x2 b)
    {
        return _mm_unpacklo_pd(a, b);
    }

    // bitwise

    static inline f64x2 bitwise_nand(f64x2 a, f64x2 b)
    {
        return _mm_andnot_pd(a, b);
    }

    static inline f64x2 bitwise_and(f64x2 a, f64x2 b)
    {
        return _mm_and_pd(a, b);
    }

    static inline f64x2 bitwise_or(f64x2 a, f64x2 b)
    {
        return _mm_or_pd(a, b);
    }

    static inline f64x2 bitwise_xor(f64x2 a, f64x2 b)
    {
        return _mm_xor_pd(a, b);
    }

    static inline f64x2 bitwise_not(f64x2 a)
    {
        const __m128i s = _mm_castpd_si128(a);
        return _mm_castsi128_pd(_mm_ternarylogic_epi64(s, s, s, 0x01));
    }

    static inline f64x2 min(f64x2 a, f64x2 b)
    {
        return _mm_min_pd(a, b);
    }

    static inline f64x2 min(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_maskz_min_pd(mask, a, b);
    }

    static inline f64x2 min(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return _mm_mask_min_pd(value, mask, a, b);
    }

    static inline f64x2 max(f64x2 a, f64x2 b)
    {
        return _mm_max_pd(a, b);
    }

    static inline f64x2 max(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_maskz_max_pd(mask, a, b);
    }

    static inline f64x2 max(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return _mm_mask_max_pd(value, mask, a, b);
    }

    static inline f64x2 abs(f64x2 a)
    {
        return _mm_max_pd(a, _mm_sub_pd(_mm_setzero_pd(), a));
    }

    static inline f64x2 neg(f64x2 a)
    {
        return _mm_sub_pd(_mm_setzero_pd(), a);
    }

    static inline f64x2 sign(f64x2 a)
    {
        __m128d sign_mask = _mm_set1_pd(-0.0);
        __m128d value_mask = _mm_cmpneq_pd(a, _mm_setzero_pd());
        __m128d sign_bits = _mm_and_pd(a, sign_mask);
        __m128d value_bits = _mm_and_pd(_mm_set1_pd(1.0), value_mask);
        return _mm_or_pd(value_bits, sign_bits);
    }
    
    static inline f64x2 add(f64x2 a, f64x2 b)
    {
        return _mm_add_pd(a, b);
    }

    static inline f64x2 add(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_maskz_add_pd(mask, a, b);
    }

    static inline f64x2 add(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return _mm_mask_add_pd(value, mask, a, b);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b)
    {
        return _mm_sub_pd(a, b);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_maskz_sub_pd(mask, a, b);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return _mm_mask_sub_pd(value, mask, a, b);
    }

    static inline f64x2 mul(f64x2 a, f64x2 b)
    {
        return _mm_mul_pd(a, b);
    }
    static inline f64x2 mul(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_maskz_mul_pd(mask, a, b);
    }

    static inline f64x2 mul(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return _mm_mask_mul_pd(value, mask, a, b);
    }

    static inline f64x2 div(f64x2 a, f64x2 b)
    {
        return _mm_div_pd(a, b);
    }

    static inline f64x2 div(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_maskz_div_pd(mask, a, b);
    }

    static inline f64x2 div(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return _mm_mask_div_pd(value, mask, a, b);
    }

    static inline f64x2 div(f64x2 a, double b)
    {
        return _mm_div_pd(a, _mm_set1_pd(b));
    }

    static inline f64x2 hadd(f64x2 a, f64x2 b)
    {
        return _mm_hadd_pd(a, b);
    }

    static inline f64x2 hsub(f64x2 a, f64x2 b)
    {
        return _mm_hsub_pd(a, b);
    }

#if defined(MANGO_ENABLE_FMA3)

    static inline f64x2 madd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a + b * c
        return _mm_fmadd_pd(b, c, a);
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        // b * c - a
        return _mm_fmsub_pd(b, c, a);
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a - b * c
        return _mm_fnmadd_pd(b, c, a);
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        // -(a + b * c)
        return _mm_fnmsub_pd(b, c, a);
    }

#else

    static inline f64x2 madd(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_add_pd(a, _mm_mul_pd(b, c));
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_sub_pd(_mm_mul_pd(b, c), a);
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_sub_pd(a, _mm_mul_pd(b, c));
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_sub_pd(_mm_setzero_pd(), _mm_add_pd(a, _mm_mul_pd(b, c)));
    }

#endif

    static inline f64x2 lerp(f64x2 a, f64x2 b, f64x2 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f64x2 rcp(f64x2 a)
    {
        return _mm_rcp14_pd(a);
    }

#else

    static inline f64x2 rcp(f64x2 a)
    {
        return _mm_div_pd(_mm_set1_pd(1.0), a);
    }

#endif // MANGO_FAST_MATH

    static inline f64x2 rsqrt(f64x2 a)
    {
        return _mm_div_pd(_mm_set1_pd(1.0), _mm_sqrt_pd(a));
    }

    static inline f64x2 sqrt(f64x2 a)
    {
        return _mm_sqrt_pd(a);
    }

    static inline double dot2(f64x2 a, f64x2 b)
    {
        const __m128d xy = _mm_mul_pd(a, b);
        const __m128d yx = _mm_shuffle_pd(xy, xy, 0x01);
        f64x2 s = _mm_add_pd(xy, yx);
        return get_component<0>(s);
    }

    // compare

    static inline mask64x2 compare_neq(f64x2 a, f64x2 b)
    {
        return _mm_cmp_pd_mask(a, b, _CMP_NEQ_UQ);
    }

    static inline mask64x2 compare_eq(f64x2 a, f64x2 b)
    {
        return _mm_cmp_pd_mask(a, b, _CMP_EQ_OQ);
    }

    static inline mask64x2 compare_lt(f64x2 a, f64x2 b)
    {
        return _mm_cmp_pd_mask(a, b, _CMP_LT_OS);
    }

    static inline mask64x2 compare_le(f64x2 a, f64x2 b)
    {
        return _mm_cmp_pd_mask(a, b, _CMP_LE_OS);
    }

    static inline mask64x2 compare_gt(f64x2 a, f64x2 b)
    {
        return _mm_cmp_pd_mask(b, a, _CMP_LT_OS);
    }

    static inline mask64x2 compare_ge(f64x2 a, f64x2 b)
    {
        return _mm_cmp_pd_mask(b, a, _CMP_LE_OS);
    }

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        return _mm_mask_blend_pd(mask, b, a);
    }

    // rounding

    static inline f64x2 round(f64x2 s)
    {
        return _mm_round_pd(s, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    }

    static inline f64x2 trunc(f64x2 s)
    {
        return _mm_round_pd(s, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
    }

    static inline f64x2 floor(f64x2 s)
    {
        return _mm_round_pd(s, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
    }

    static inline f64x2 ceil(f64x2 s)
    {
        return _mm_round_pd(s, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
    }

    static inline f64x2 fract(f64x2 s)
    {
        return sub(s, floor(s));
    }

} // namespace mango::simd
