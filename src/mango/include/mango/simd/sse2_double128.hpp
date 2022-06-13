/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-202§ Twilight Finland 3D Oy Ltd. All rights reserved.
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
    static inline f64x2 set_component(f64x2 a, f64 s);

    template <>
    inline f64x2 set_component<0>(f64x2 a, f64 x)
    {
        return _mm_move_sd(a, _mm_set1_pd(x));
    }

    template <>
    inline f64x2 set_component<1>(f64x2 a, f64 y)
    {
        return _mm_move_sd(_mm_set1_pd(y), a);
    }

    // get component

    template <int Index>
    static inline f64 get_component(f64x2 a);

    template <>
    inline f64 get_component<0>(f64x2 a)
    {
        return _mm_cvtsd_f64(a);
    }

    template <>
    inline f64 get_component<1>(f64x2 a)
    {
        const __m128d yy = _mm_unpackhi_pd(a, a);
        return _mm_cvtsd_f64(yy);
    }

    static inline f64x2 f64x2_zero()
    {
        return _mm_setzero_pd();
    }

    static inline f64x2 f64x2_set(f64 s)
    {
        return _mm_set1_pd(s);
    }

    static inline f64x2 f64x2_set(f64 x, f64 y)
    {
        return _mm_setr_pd(x, y);
    }

    static inline f64x2 f64x2_uload(const f64* source)
    {
        return _mm_loadu_pd(source);
    }

    static inline void f64x2_ustore(f64* dest, f64x2 a)
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
        return _mm_xor_pd(a, _mm_cmpeq_pd(a, a));
    }

    static inline f64x2 min(f64x2 a, f64x2 b)
    {
        return _mm_min_pd(a, b);
    }

    static inline f64x2 max(f64x2 a, f64x2 b)
    {
        return _mm_max_pd(a, b);
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
        __m128d value_bits = _mm_and_pd(value_mask, _mm_set1_pd(1.0));
        return _mm_or_pd(value_bits, sign_bits);
    }

    static inline f64x2 add(f64x2 a, f64x2 b)
    {
        return _mm_add_pd(a, b);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b)
    {
        return _mm_sub_pd(a, b);
    }

    static inline f64x2 mul(f64x2 a, f64x2 b)
    {
        return _mm_mul_pd(a, b);
    }

    static inline f64x2 div(f64x2 a, f64x2 b)
    {
        return _mm_div_pd(a, b);
    }

    static inline f64x2 div(f64x2 a, f64 b)
    {
        return _mm_div_pd(a, _mm_set1_pd(b));
    }

#if defined(MANGO_ENABLE_SSE3)

    static inline f64x2 hadd(f64x2 a, f64x2 b)
    {
	    return _mm_hadd_pd(a, b);
    }

    static inline f64x2 hsub(f64x2 a, f64x2 b)
    {
	    return _mm_hsub_pd(a, b);
    }

#else

    static inline f64x2 hadd(f64x2 a, f64x2 b)
    {
        return _mm_add_pd(_mm_unpacklo_pd(a, b), _mm_unpackhi_pd(a, b));
    }

    static inline f64x2 hsub(f64x2 a, f64x2 b)
    {
        return _mm_sub_pd(_mm_unpacklo_pd(a, b), _mm_unpackhi_pd(a, b));
    }

#endif

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

#elif defined(MANGO_ENABLE_FMA4)

    static inline f64x2 madd(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_macc_pd(b, c, a);
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_msub_pd(b, c, a);
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_nmacc_pd(b, c, a);
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        return _mm_nmsub_pd(b, c, a);
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

    static inline f64x2 rcp(f64x2 a)
    {
        return _mm_div_pd(_mm_set1_pd(1.0), a);
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        return _mm_div_pd(_mm_set1_pd(1.0), _mm_sqrt_pd(a));
    }

    static inline f64x2 sqrt(f64x2 a)
    {
        return _mm_sqrt_pd(a);
    }

    static inline f64 dot2(f64x2 a, f64x2 b)
    {
        __m128d xy = _mm_mul_pd(a, b);
        __m128d yx = _mm_shuffle_pd(xy, xy, 0x01);
        f64x2 s = _mm_add_pd(xy, yx);
        return get_component<0>(s);
    }

    // compare

    static inline mask64x2 compare_neq(f64x2 a, f64x2 b)
    {
        return _mm_castpd_si128(_mm_cmpneq_pd(a, b));
    }

    static inline mask64x2 compare_eq(f64x2 a, f64x2 b)
    {
        return _mm_castpd_si128(_mm_cmpeq_pd(a, b));
    }

    static inline mask64x2 compare_lt(f64x2 a, f64x2 b)
    {
        return _mm_castpd_si128(_mm_cmplt_pd(a, b));
    }

    static inline mask64x2 compare_le(f64x2 a, f64x2 b)
    {
        return _mm_castpd_si128(_mm_cmple_pd(a, b));
    }

    static inline mask64x2 compare_gt(f64x2 a, f64x2 b)
    {
        return _mm_castpd_si128(_mm_cmpgt_pd(a, b));
    }

    static inline mask64x2 compare_ge(f64x2 a, f64x2 b)
    {
        return _mm_castpd_si128(_mm_cmpge_pd(a, b));
    }

#if defined(MANGO_ENABLE_SSE4_1)

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        return _mm_blendv_pd(b, a, _mm_castsi128_pd(mask));
    }

#else

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        return _mm_or_pd(_mm_and_pd(_mm_castsi128_pd(mask), a), _mm_andnot_pd(_mm_castsi128_pd(mask), b));
    }

#endif

    // rounding

#if defined(MANGO_ENABLE_SSE4_1)

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

#else

    static inline f64x2 round(f64x2 s)
    {
        return _mm_cvtepi32_pd(_mm_cvtpd_epi32(s));
    }

    static inline f64x2 trunc(f64x2 s)
    {
        return _mm_cvtepi32_pd(_mm_cvttpd_epi32(s));
    }

    static inline f64x2 floor(f64x2 s)
    {
        const __m128d temp = round(s);
        const __m128d mask = _mm_cmplt_pd(s, temp);
        return _mm_sub_pd(temp, _mm_and_pd(mask, _mm_set1_pd(1.0)));
    }

    static inline f64x2 ceil(f64x2 s)
    {
        const __m128d temp = round(s);
        const __m128d mask = _mm_cmpgt_pd(s, temp);
        return _mm_add_pd(temp, _mm_and_pd(mask, _mm_set1_pd(1.0)));
    }

#endif

    static inline f64x2 fract(f64x2 s)
    {
        return sub(s, floor(s));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

    static inline f64x2 min(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_and_pd(_mm_castsi128_pd(mask), min(a, b));
    }

    static inline f64x2 max(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_and_pd(_mm_castsi128_pd(mask), max(a, b));
    }

    static inline f64x2 add(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_and_pd(_mm_castsi128_pd(mask), add(a, b));
    }

    static inline f64x2 sub(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_and_pd(_mm_castsi128_pd(mask), sub(a, b));
    }

    static inline f64x2 mul(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_and_pd(_mm_castsi128_pd(mask), mul(a, b));
    }

    static inline f64x2 div(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return _mm_and_pd(_mm_castsi128_pd(mask), div(a, b));
    }

#define SIMD_MASK_DOUBLE128
#include <mango/simd/common_mask.hpp>
#undef SIMD_MASK_DOUBLE128

} // namespace mango::simd
