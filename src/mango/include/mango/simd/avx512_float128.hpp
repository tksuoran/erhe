/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f32x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f32x4 shuffle(f32x4 a, f32x4 b)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        return _mm_shuffle_ps(a, b, _MM_SHUFFLE(w, z, y, x));
    }

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f32x4 shuffle(f32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        return _mm_shuffle_ps(v, v, _MM_SHUFFLE(w, z, y, x));
    }

    template <>
    inline f32x4 shuffle<0, 1, 2, 3>(f32x4 v)
    {
        // .xyzw
        return v;
    }

    // indexed access

    template <unsigned int Index>
    static inline f32x4 set_component(f32x4 a, f32 s)
    {
        static_assert(Index < 4, "Index out of range.");
        return _mm_insert_ps(a, _mm_set_ss(s), Index * 0x10);
    }

    template <int Index>
    static inline f32 get_component(f32x4 a);

    template <>
    inline f32 get_component<0>(f32x4 a)
    {
        return _mm_cvtss_f32(a);
    }

    template <>
    inline f32 get_component<1>(f32x4 a)
    {
        return _mm_cvtss_f32(shuffle<1, 1, 1, 1>(a));
    }

    template <>
    inline f32 get_component<2>(f32x4 a)
    {
        return _mm_cvtss_f32(shuffle<2, 2, 2, 2>(a));
    }

    template <>
    inline f32 get_component<3>(f32x4 a)
    {
        return _mm_cvtss_f32(shuffle<3, 3, 3, 3>(a));
    }

    static inline f32x4 f32x4_zero()
    {
        return _mm_setzero_ps();
    }

    static inline f32x4 f32x4_set(f32 s)
    {
        return _mm_set1_ps(s);
    }

    static inline f32x4 f32x4_set(f32 x, f32 y, f32 z, f32 w)
    {
        return _mm_setr_ps(x, y, z, w);
    }

    static inline f32x4 f32x4_uload(const f32* source)
    {
        return _mm_loadu_ps(source);
    }

    static inline void f32x4_ustore(f32* dest, f32x4 a)
    {
        _mm_storeu_ps(dest, a);
    }

    static inline f32x4 movelh(f32x4 a, f32x4 b)
    {
        return _mm_movelh_ps(a, b);
    }

    static inline f32x4 movehl(f32x4 a, f32x4 b)
    {
        return _mm_movehl_ps(a, b);
    }

    static inline f32x4 unpackhi(f32x4 a, f32x4 b)
    {
        return _mm_unpackhi_ps(a, b);
    }

    static inline f32x4 unpacklo(f32x4 a, f32x4 b)
    {
        return _mm_unpacklo_ps(a, b);
    }

    // bitwise

    static inline f32x4 bitwise_nand(f32x4 a, f32x4 b)
    {
        return _mm_andnot_ps(a, b);
    }

    static inline f32x4 bitwise_and(f32x4 a, f32x4 b)
    {
        return _mm_and_ps(a, b);
    }

    static inline f32x4 bitwise_or(f32x4 a, f32x4 b)
    {
        return _mm_or_ps(a, b);
    }

    static inline f32x4 bitwise_xor(f32x4 a, f32x4 b)
    {
        return _mm_xor_ps(a, b);
    }

    static inline f32x4 bitwise_not(f32x4 a)
    {
        const __m128i s = _mm_castps_si128(a);
        return _mm_castsi128_ps(_mm_ternarylogic_epi32(s, s, s, 0x01));
    }

    static inline f32x4 min(f32x4 a, f32x4 b)
    {
        return _mm_min_ps(a, b);
    }

    static inline f32x4 min(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_maskz_min_ps(mask, a, b);
    }

    static inline f32x4 min(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return _mm_mask_min_ps(value, mask, a, b);
    }

    static inline f32x4 max(f32x4 a, f32x4 b)
    {
        return _mm_max_ps(a, b);
    }

    static inline f32x4 max(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_maskz_max_ps(mask, a, b);
    }

    static inline f32x4 max(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return _mm_mask_max_ps(value, mask, a, b);
    }

    static inline f32x4 hmin(f32x4 a)
    {
        __m128 temp = _mm_min_ps(a, _mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 3, 0, 1)));
        return _mm_min_ps(temp, _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(1, 0, 3, 2)));
    }

    static inline f32x4 hmax(f32x4 a)
    {
        __m128 temp = _mm_max_ps(a, _mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 3, 0, 1)));
        return _mm_max_ps(temp, _mm_shuffle_ps(temp, temp, _MM_SHUFFLE(1, 0, 3, 2)));
    }

    static inline f32x4 abs(f32x4 a)
    {
        return _mm_max_ps(a, _mm_sub_ps(_mm_setzero_ps(), a));
    }

    static inline f32x4 neg(f32x4 a)
    {
        return _mm_sub_ps(_mm_setzero_ps(), a);
    }

    static inline f32x4 sign(f32x4 a)
    {
        __m128 sign_mask = _mm_set1_ps(-0.0f);
        __m128 value_mask = _mm_cmpneq_ps(a, _mm_setzero_ps());
        __m128 sign_bits = _mm_and_ps(a, sign_mask);
        __m128 value_bits = _mm_and_ps(_mm_set1_ps(1.0f), value_mask);
        return _mm_or_ps(value_bits, sign_bits);
    }
    
    static inline f32x4 add(f32x4 a, f32x4 b)
    {
        return _mm_add_ps(a, b);
    }

    static inline f32x4 add(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_maskz_add_ps(mask, a, b);
    }

    static inline f32x4 add(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return _mm_mask_add_ps(value, mask, a, b);
    }

    static inline f32x4 sub(f32x4 a, f32x4 b)
    {
        return _mm_sub_ps(a, b);
    }

    static inline f32x4 sub(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_maskz_sub_ps(mask, a, b);
    }

    static inline f32x4 sub(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return _mm_mask_sub_ps(value, mask, a, b);
    }

    static inline f32x4 mul(f32x4 a, f32x4 b)
    {
        return _mm_mul_ps(a, b);
    }

    static inline f32x4 mul(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_maskz_mul_ps(mask, a, b);
    }

    static inline f32x4 mul(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return _mm_mask_mul_ps(value, mask, a, b);
    }

    static inline f32x4 div(f32x4 a, f32x4 b)
    {
        return _mm_div_ps(a, b);
    }

    static inline f32x4 div(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_maskz_div_ps(mask, a, b);
    }

    static inline f32x4 div(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return _mm_mask_div_ps(value, mask, a, b);
    }

    static inline f32x4 div(f32x4 a, f32 b)
    {
        return _mm_div_ps(a, _mm_set1_ps(b));
    }

    static inline f32x4 hadd(f32x4 a, f32x4 b)
    {
	    return _mm_hadd_ps(a, b);
    }

    static inline f32x4 hsub(f32x4 a, f32x4 b)
    {
	    return _mm_hsub_ps(a, b);
    }

#if defined(MANGO_ENABLE_FMA3)

    static inline f32x4 madd(f32x4 a, f32x4 b, f32x4 c)
    {
        // a + b * c
        return _mm_fmadd_ps(b, c, a);
    }

    static inline f32x4 msub(f32x4 a, f32x4 b, f32x4 c)
    {
        // b * c - a
        return _mm_fmsub_ps(b, c, a);
    }

    static inline f32x4 nmadd(f32x4 a, f32x4 b, f32x4 c)
    {
        // a - b * c
        return _mm_fnmadd_ps(b, c, a);
    }

    static inline f32x4 nmsub(f32x4 a, f32x4 b, f32x4 c)
    {
        // -(a + b * c)
        return _mm_fnmsub_ps(b, c, a);
    }

#else

    static inline f32x4 madd(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_add_ps(a, _mm_mul_ps(b, c));
    }

    static inline f32x4 msub(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_sub_ps(_mm_mul_ps(b, c), a);
    }

    static inline f32x4 nmadd(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_sub_ps(a, _mm_mul_ps(b, c));
    }

    static inline f32x4 nmsub(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_sub_ps(_mm_setzero_ps(), _mm_add_ps(a, _mm_mul_ps(b, c)));
    }

#endif

    static inline f32x4 lerp(f32x4 a, f32x4 b, f32x4 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f32x4 rcp(f32x4 a)
    {
        return _mm_rcp14_ps(a);
    }

    static inline f32x4 rsqrt(f32x4 a)
    {
        return _mm_maskz_rsqrt14_ps(_mm_cmp_ps_mask(a, a, _CMP_EQ_OQ), a);
    }

#else

    static inline f32x4 rcp(f32x4 a)
    {
        f32x4 n = _mm_rcp_ps(a);
        f32x4 m = _mm_mul_ps(_mm_mul_ps(n, n), a);
        return _mm_sub_ps(_mm_add_ps(n, n), m);
    }

    static inline f32x4 rsqrt(f32x4 a)
    {
        f32x4 n = _mm_rsqrt_ps(a);
        f32x4 e = _mm_mul_ps(_mm_mul_ps(n, n), a);
        n = _mm_mul_ps(_mm_set_ps1(0.5f), n);
        e = _mm_sub_ps(_mm_set_ps1(3.0f), e);
        return _mm_mul_ps(n, e);
    }

#endif

    static inline f32x4 sqrt(f32x4 a)
    {
        return _mm_sqrt_ps(a);
    }

    static inline f32 dot3(f32x4 a, f32x4 b)
    {
        f32x4 s = _mm_dp_ps(a, b, 0x7f);
        return get_component<0>(s);
    }

    static inline f32 dot4(f32x4 a, f32x4 b)
    {
        f32x4 s = _mm_dp_ps(a, b, 0xff);
        return get_component<0>(s);
    }

    static inline f32x4 cross3(f32x4 a, f32x4 b)
    {
        f32x4 c = _mm_sub_ps(_mm_mul_ps(a, shuffle<1, 2, 0, 3>(b)),
                                 _mm_mul_ps(b, shuffle<1, 2, 0, 3>(a)));
        return shuffle<1, 2, 0, 3>(c);
    }

    // compare

    static inline mask32x4 compare_neq(f32x4 a, f32x4 b)
    {
        return _mm_cmp_ps_mask(a, b, _CMP_NEQ_UQ);
    }

    static inline mask32x4 compare_eq(f32x4 a, f32x4 b)
    {
        return _mm_cmp_ps_mask(a, b, _CMP_EQ_OQ);
    }

    static inline mask32x4 compare_lt(f32x4 a, f32x4 b)
    {
        return _mm_cmp_ps_mask(a, b, _CMP_LT_OS);
    }

    static inline mask32x4 compare_le(f32x4 a, f32x4 b)
    {
        return _mm_cmp_ps_mask(a, b, _CMP_LE_OS);
    }

    static inline mask32x4 compare_gt(f32x4 a, f32x4 b)
    {
        return _mm_cmp_ps_mask(b, a, _CMP_LT_OS);
    }

    static inline mask32x4 compare_ge(f32x4 a, f32x4 b)
    {
        return _mm_cmp_ps_mask(b, a, _CMP_LE_OS);
    }

    static inline f32x4 select(mask32x4 mask, f32x4 a, f32x4 b)
    {
        return _mm_mask_blend_ps(mask, b, a);
    }

    // rounding

    static inline f32x4 round(f32x4 s)
    {
        return _mm_round_ps(s, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    }

    static inline f32x4 trunc(f32x4 s)
    {
        //return _mm_roundscale_ps(s, 0x13);
        return _mm_round_ps(s, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
    }

    static inline f32x4 floor(f32x4 s)
    {
        //return _mm_roundscale_ps(s, 0x11);
        return _mm_round_ps(s, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
    }

    static inline f32x4 ceil(f32x4 s)
    {
        //return _mm_roundscale_ps(s, 0x12);
        return _mm_round_ps(s, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
    }

    static inline f32x4 fract(f32x4 s)
    {
        return sub(s, floor(s));
    }

} // namespace mango::simd
