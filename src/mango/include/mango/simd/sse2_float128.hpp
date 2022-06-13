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

#if defined(MANGO_ENABLE_SSE4_1)

    template <unsigned int Index>
    static inline f32x4 set_component(f32x4 a, f32 s)
    {
        static_assert(Index < 4, "Index out of range.");
        return _mm_insert_ps(a, _mm_set_ss(s), Index * 0x10);
    }

#else

    template <int Index>
    static inline f32x4 set_component(f32x4 a, f32 s);

    template <>
    inline f32x4 set_component<0>(f32x4 a, f32 x)
    {
        const __m128 b = _mm_unpacklo_ps(_mm_set_ps1(x), a);
        return _mm_shuffle_ps(b, a, _MM_SHUFFLE(3, 2, 3, 0));
    }

    template <>
    inline f32x4 set_component<1>(f32x4 a, f32 y)
    {
        const __m128 b = _mm_unpacklo_ps(_mm_set_ps1(y), a);
        return _mm_shuffle_ps(b, a, _MM_SHUFFLE(3, 2, 0, 1));
    }

    template <>
    inline f32x4 set_component<2>(f32x4 a, f32 z)
    {
        const __m128 b = _mm_unpackhi_ps(_mm_set_ps1(z), a);
        return _mm_shuffle_ps(a, b, _MM_SHUFFLE(3, 0, 1, 0));
    }

    template <>
    inline f32x4 set_component<3>(f32x4 a, f32 w)
    {
        const __m128 b = _mm_unpackhi_ps(_mm_set_ps1(w), a);
        return _mm_shuffle_ps(a, b, _MM_SHUFFLE(0, 1, 1, 0));
    }

#endif

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
        return _mm_xor_ps(a, _mm_cmpeq_ps(a, a));
    }

    static inline f32x4 min(f32x4 a, f32x4 b)
    {
        return _mm_min_ps(a, b);
    }

    static inline f32x4 max(f32x4 a, f32x4 b)
    {
        return _mm_max_ps(a, b);
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
        __m128 value_bits = _mm_and_ps(value_mask, _mm_set1_ps(1.0f));
        return _mm_or_ps(value_bits, sign_bits);
    }

    static inline f32x4 add(f32x4 a, f32x4 b)
    {
        return _mm_add_ps(a, b);
    }

    static inline f32x4 sub(f32x4 a, f32x4 b)
    {
        return _mm_sub_ps(a, b);
    }

    static inline f32x4 mul(f32x4 a, f32x4 b)
    {
        return _mm_mul_ps(a, b);
    }

    static inline f32x4 div(f32x4 a, f32x4 b)
    {
        return _mm_div_ps(a, b);
    }

    static inline f32x4 div(f32x4 a, f32 b)
    {
        return _mm_div_ps(a, _mm_set1_ps(b));
    }

#if defined(MANGO_ENABLE_SSE3)

    static inline f32x4 hadd(f32x4 a, f32x4 b)
    {
	    return _mm_hadd_ps(a, b);
    }

    static inline f32x4 hsub(f32x4 a, f32x4 b)
    {
	    return _mm_hsub_ps(a, b);
    }

#else

    static inline f32x4 hadd(f32x4 a, f32x4 b)
    {
	    return _mm_add_ps(_mm_shuffle_ps(a, b, 0x88),
	                      _mm_shuffle_ps(a, b, 0xdd));
    }

    static inline f32x4 hsub(f32x4 a, f32x4 b)
    {
	    return _mm_sub_ps(_mm_shuffle_ps(a, b, 0x88),
	                      _mm_shuffle_ps(a, b, 0xdd));
    }

#endif

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

#elif defined(MANGO_ENABLE_FMA4)

    static inline f32x4 madd(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_macc_ps(b, c, a);
    }

    static inline f32x4 msub(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_msub_ps(b, c, a);
    }

    static inline f32x4 nmadd(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_nmacc_ps(b, c, a);
    }

    static inline f32x4 nmsub(f32x4 a, f32x4 b, f32x4 c)
    {
        return _mm_nmsub_ps(b, c, a);
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
        return _mm_rcp_ps(a);
    }

    static inline f32x4 rsqrt(f32x4 a)
    {
        return _mm_rsqrt_ps(a);
    }

    static inline f32x4 sqrt(f32x4 a)
    {
        f32x4 n = _mm_rsqrt_ps(a);
        return _mm_mul_ps(a, n);
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

    static inline f32x4 sqrt(f32x4 a)
    {
        return _mm_sqrt_ps(a);
    }

#endif

    static inline f32 dot3(f32x4 a, f32x4 b)
    {
#if defined(MANGO_ENABLE_SSE4_1)
        f32x4 s = _mm_dp_ps(a, b, 0x7f);
#else
        f32x4 s = _mm_mul_ps(a, b);
        f32x4 x = shuffle<0, 0, 0, 0>(s);
        f32x4 y = shuffle<1, 1, 1, 1>(s);
        f32x4 z = shuffle<2, 2, 2, 2>(s);
        s = _mm_add_ps(x, _mm_add_ps(y, z));
#endif
        return get_component<0>(s);
    }

    static inline f32 dot4(f32x4 a, f32x4 b)
    {
        f32x4 s;
#if defined(MANGO_ENABLE_SSE4_1)
        s = _mm_dp_ps(a, b, 0xff);
#elif defined(MANGO_ENABLE_SSE3)
        s = _mm_mul_ps(a, b);
        s = _mm_hadd_ps(s, s);
        s = _mm_hadd_ps(s, s);
#else
        s = _mm_mul_ps(a, b);
        s = _mm_add_ps(s, shuffle<2, 3, 0, 1>(s));
        s = _mm_add_ps(s, shuffle<1, 0, 3, 2>(s));
#endif
        return get_component<0>(s);
    }

    static inline f32x4 cross3(f32x4 a, f32x4 b)
    {
        f32x4 u = _mm_mul_ps(a, shuffle<1, 2, 0, 3>(b));
        f32x4 v = _mm_mul_ps(b, shuffle<1, 2, 0, 3>(a));
        f32x4 c = _mm_sub_ps(u, v);
        return shuffle<1, 2, 0, 3>(c);
    }

    // compare

    static inline mask32x4 compare_neq(f32x4 a, f32x4 b)
    {
        return _mm_castps_si128(_mm_cmpneq_ps(a, b));
    }

    static inline mask32x4 compare_eq(f32x4 a, f32x4 b)
    {
        return _mm_castps_si128(_mm_cmpeq_ps(a, b));
    }

    static inline mask32x4 compare_lt(f32x4 a, f32x4 b)
    {
        return _mm_castps_si128(_mm_cmplt_ps(a, b));
    }

    static inline mask32x4 compare_le(f32x4 a, f32x4 b)
    {
        return _mm_castps_si128(_mm_cmple_ps(a, b));
    }

    static inline mask32x4 compare_gt(f32x4 a, f32x4 b)
    {
        return _mm_castps_si128(_mm_cmpgt_ps(a, b));
    }

    static inline mask32x4 compare_ge(f32x4 a, f32x4 b)
    {
        return _mm_castps_si128(_mm_cmpge_ps(a, b));
    }

#if defined(MANGO_ENABLE_SSE4_1)

    static inline f32x4 select(mask32x4 mask, f32x4 a, f32x4 b)
    {
        return _mm_blendv_ps(b, a, _mm_castsi128_ps(mask));
    }

#else

    static inline f32x4 select(mask32x4 mask, f32x4 a, f32x4 b)
    {
        return _mm_or_ps(_mm_and_ps(_mm_castsi128_ps(mask), a), _mm_andnot_ps(_mm_castsi128_ps(mask), b));
    }

#endif

    // rounding

#if defined(MANGO_ENABLE_SSE4_1)

    static inline f32x4 round(f32x4 s)
    {
        return _mm_round_ps(s, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
    }

    static inline f32x4 trunc(f32x4 s)
    {
        return _mm_round_ps(s, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
    }

    static inline f32x4 floor(f32x4 s)
    {
        return _mm_round_ps(s, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
    }

    static inline f32x4 ceil(f32x4 s)
    {
        return _mm_round_ps(s, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
    }

#else

    static inline f32x4 round(f32x4 s)
    {
        f32x4 result = _mm_cvtepi32_ps(_mm_cvtps_epi32(s));
        mask32x4 mask = _mm_castps_si128(_mm_cmple_ps(abs(s), _mm_castsi128_ps(_mm_set1_epi32(0x4b000000))));
        return select(mask, result, s);
    }

    static inline f32x4 trunc(f32x4 s)
    {
        f32x4 result = _mm_cvtepi32_ps(_mm_cvttps_epi32(s));
        mask32x4 mask = _mm_castps_si128(_mm_cmple_ps(abs(s), _mm_castsi128_ps(_mm_set1_epi32(0x4b000000))));
        return select(mask, result, s);
    }

    static inline f32x4 floor(f32x4 s)
    {
        const f32x4 temp = round(s);
        const f32x4 mask = _mm_cmplt_ps(s, temp);
        return _mm_sub_ps(temp, _mm_and_ps(mask, _mm_set1_ps(1.0f)));
    }

    static inline f32x4 ceil(f32x4 s)
    {
        const f32x4 temp = round(s);
        const f32x4 mask = _mm_cmpgt_ps(s, temp);
        return _mm_add_ps(temp, _mm_and_ps(mask, _mm_set1_ps(1.0f)));
    }

#endif

    static inline f32x4 fract(f32x4 s)
    {
        return sub(s, floor(s));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

    static inline f32x4 min(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_and_ps(_mm_castsi128_ps(mask), min(a, b));
    }

    static inline f32x4 max(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_and_ps(_mm_castsi128_ps(mask), max(a, b));
    }

    static inline f32x4 add(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_and_ps(_mm_castsi128_ps(mask), add(a, b));
    }

    static inline f32x4 sub(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_and_ps(_mm_castsi128_ps(mask), sub(a, b));
    }

    static inline f32x4 mul(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_and_ps(_mm_castsi128_ps(mask), mul(a, b));
    }

    static inline f32x4 div(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return _mm_and_ps(_mm_castsi128_ps(mask), div(a, b));
    }

#define SIMD_MASK_FLOAT128
#include <mango/simd/common_mask.hpp>
#undef SIMD_MASK_FLOAT128

} // namespace mango::simd
