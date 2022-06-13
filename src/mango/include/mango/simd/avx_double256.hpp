/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f64x4
    // -----------------------------------------------------------------

#ifdef MANGO_ENABLE_AVX2

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f64x4 shuffle(f64x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        return _mm256_permute4x64_pd(v, _MM_SHUFFLE(w, z, y, x));
    }

#else

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f64x4 shuffle(f64x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const int perm = ((w & 1) << 3) | ((z & 1) << 2) | ((y & 1) << 1) | (x & 1);
        const int mask = ((w & 2) << 2) | ((z & 2) << 1) | (y & 2) | ((x & 2) >> 1);
        __m256d xyxy = _mm256_permute2f128_pd(v, v, 0);
        __m256d zwzw = _mm256_permute2f128_pd(v, v, 0x11);
        xyxy = _mm256_permute_pd(xyxy, perm);
        zwzw = _mm256_permute_pd(zwzw, perm);
        return _mm256_blend_pd(xyxy, zwzw, mask);
    }

    template <>
    inline f64x4 shuffle<0, 0, 0, 0>(f64x4 v)
    {
        // .xxxx
        const __m256d xyxy = _mm256_permute2f128_pd(v, v, 0);
        return _mm256_permute_pd(xyxy, 0);
    }

    template <>
    inline f64x4 shuffle<1, 1, 1, 1>(f64x4 v)
    {
        // .yyyy
        const __m256d xyxy = _mm256_permute2f128_pd(v, v, 0);
        return _mm256_permute_pd(xyxy, 0xf);
    }

    template <>
    inline f64x4 shuffle<2, 2, 2, 2>(f64x4 v)
    {
        // .zzzz
        const __m256d zwzw = _mm256_permute2f128_pd(v, v, 0x11);
        return _mm256_permute_pd(zwzw, 0);
    }

    template <>
    inline f64x4 shuffle<3, 3, 3, 3>(f64x4 v)
    {
        // .wwww
        const __m256d zwzw = _mm256_permute2f128_pd(v, v, 0x11);
        return _mm256_permute_pd(zwzw, 0xf);
    }

#endif // MANGO_ENABLE_AVX2

    template <>
    inline f64x4 shuffle<0, 1, 2, 3>(f64x4 v)
    {
        // .xyzw
        return v;
    }

    template <>
    inline f64x4 shuffle<1, 0, 3, 2>(f64x4 v)
    {
        // .yxwz
        return _mm256_shuffle_pd(v, v, 0x05);
    }

    template <>
    inline f64x4 shuffle<2, 3, 0, 1>(f64x4 v)
    {
        // .zwxy
        return _mm256_shuffle_pd(v, v, 0x0a);
    }

    // set component

    template <int Index>
    static inline f64x4 set_component(f64x4 a, f64 s);

    template <>
    inline f64x4 set_component<0>(f64x4 a, f64 x)
    {
        __m128d xy = _mm256_extractf128_pd(a, 0);
        xy = _mm_move_sd(xy, _mm_set1_pd(x));
        return _mm256_insertf128_pd(a, xy, 0);
    }

    template <>
    inline f64x4 set_component<1>(f64x4 a, f64 y)
    {
        __m128d xy = _mm256_extractf128_pd(a, 0);
        xy = _mm_move_sd(_mm_set1_pd(y), xy);
        return _mm256_insertf128_pd(a, xy, 0);
    }

    template <>
    inline f64x4 set_component<2>(f64x4 a, f64 z)
    {
        __m128d zw = _mm256_extractf128_pd(a, 1);
        zw = _mm_move_sd(zw, _mm_set1_pd(z));
        return _mm256_insertf128_pd(a, zw, 1);
    }

    template <>
    inline f64x4 set_component<3>(f64x4 a, f64 w)
    {
        __m128d zw = _mm256_extractf128_pd(a, 1);
        zw = _mm_move_sd(_mm_set1_pd(w), zw);
        return _mm256_insertf128_pd(a, zw, 1);
    }

    // get component

    template <int Index>
    static inline f64 get_component(f64x4 a);

    template <>
    inline f64 get_component<0>(f64x4 a)
    {
        const __m128d xy = _mm256_extractf128_pd(a, 0);
        return _mm_cvtsd_f64(xy);
    }

    template <>
    inline f64 get_component<1>(f64x4 a)
    {
        const __m128d xy = _mm256_extractf128_pd(a, 0);
        const __m128d yy = _mm_unpackhi_pd(xy, xy);
        return _mm_cvtsd_f64(yy);
    }

    template <>
    inline f64 get_component<2>(f64x4 a)
    {
        const __m128d zw = _mm256_extractf128_pd(a, 1);
        return _mm_cvtsd_f64(zw);
    }

    template <>
    inline f64 get_component<3>(f64x4 a)
    {
        const __m128d zw = _mm256_extractf128_pd(a, 1);
        const __m128d ww = _mm_unpackhi_pd(zw, zw);
        return _mm_cvtsd_f64(ww);
    }

    static inline f64x4 f64x4_zero()
    {
        return _mm256_setzero_pd();
    }

    static inline f64x4 f64x4_set(f64 s)
    {
        return _mm256_set1_pd(s);
    }

    static inline f64x4 f64x4_set(f64 x, f64 y, f64 z, f64 w)
    {
        return _mm256_setr_pd(x, y, z, w);
    }

    static inline f64x4 f64x4_uload(const f64* source)
    {
        return _mm256_loadu_pd(source);
    }

    static inline void f64x4_ustore(f64* dest, f64x4 a)
    {
        _mm256_storeu_pd(dest, a);
    }

    static inline f64x4 movelh(f64x4 a, f64x4 b)
    {
        return _mm256_permute2f128_pd(a, b, 0x20);
    }

    static inline f64x4 movehl(f64x4 a, f64x4 b)
    {
        return _mm256_permute2f128_pd(a, b, 0x13);
    }

    static inline f64x4 unpackhi(f64x4 a, f64x4 b)
    {
        return _mm256_unpackhi_pd(a, b);
    }

    static inline f64x4 unpacklo(f64x4 a, f64x4 b)
    {
        return _mm256_unpacklo_pd(a, b);
    }

    // bitwise

    static inline f64x4 bitwise_nand(f64x4 a, f64x4 b)
    {
        return _mm256_andnot_pd(a, b);
    }

    static inline f64x4 bitwise_and(f64x4 a, f64x4 b)
    {
        return _mm256_and_pd(a, b);
    }

    static inline f64x4 bitwise_or(f64x4 a, f64x4 b)
    {
        return _mm256_or_pd(a, b);
    }

    static inline f64x4 bitwise_xor(f64x4 a, f64x4 b)
    {
        return _mm256_xor_pd(a, b);
    }

    static inline f64x4 bitwise_not(f64x4 a)
    {
        return _mm256_xor_pd(a, _mm256_cmp_pd(a, a, _CMP_EQ_OQ));
    }

    static inline f64x4 min(f64x4 a, f64x4 b)
    {
        return _mm256_min_pd(a, b);
    }

    static inline f64x4 max(f64x4 a, f64x4 b)
    {
        return _mm256_max_pd(a, b);
    }

    static inline f64x4 hmin(f64x4 a)
    {
        const __m256d temp = _mm256_min_pd(a, _mm256_shuffle_pd(a, a, 0x05));
        return _mm256_min_pd(temp, _mm256_shuffle_pd(temp, temp, 0x0a));
    }

    static inline f64x4 hmax(f64x4 a)
    {
        const __m256d temp = _mm256_max_pd(a, _mm256_shuffle_pd(a, a, 0x05));
        return _mm256_max_pd(temp, _mm256_shuffle_pd(temp, temp, 0x0a));
    }

    static inline f64x4 abs(f64x4 a)
    {
        return _mm256_max_pd(a, _mm256_sub_pd(_mm256_setzero_pd(), a));
    }

    static inline f64x4 neg(f64x4 a)
    {
        return _mm256_sub_pd(_mm256_setzero_pd(), a);
    }

    static inline f64x4 sign(f64x4 a)
    {
        __m256d sign_mask = _mm256_set1_pd(-0.0);
        __m256d value_mask = _mm256_cmp_pd(a, _mm256_setzero_pd(), _CMP_NEQ_UQ);
        __m256d sign_bits = _mm256_and_pd(a, sign_mask);
        __m256d value_bits = _mm256_and_pd(_mm256_set1_pd(1.0), value_mask);
        return _mm256_or_pd(value_bits, sign_bits);
    }

    static inline f64x4 add(f64x4 a, f64x4 b)
    {
        return _mm256_add_pd(a, b);
    }

    static inline f64x4 sub(f64x4 a, f64x4 b)
    {
        return _mm256_sub_pd(a, b);
    }

    static inline f64x4 mul(f64x4 a, f64x4 b)
    {
        return _mm256_mul_pd(a, b);
    }

    static inline f64x4 div(f64x4 a, f64x4 b)
    {
        return _mm256_div_pd(a, b);
    }

    static inline f64x4 div(f64x4 a, f64 b)
    {
        return _mm256_div_pd(a, _mm256_set1_pd(b));
    }

    static inline f64x4 hadd(f64x4 a, f64x4 b)
    {
        return _mm256_hadd_pd(a, b);
    }

    static inline f64x4 hsub(f64x4 a, f64x4 b)
    {
        return _mm256_hsub_pd(a, b);
    }

#if defined(MANGO_ENABLE_FMA3)

    static inline f64x4 madd(f64x4 a, f64x4 b, f64x4 c)
    {
        // a + b * c
        return _mm256_fmadd_pd(b, c, a);
    }

    static inline f64x4 msub(f64x4 a, f64x4 b, f64x4 c)
    {
        // b * c - a
        return _mm256_fmsub_pd(b, c, a);
    }

    static inline f64x4 nmadd(f64x4 a, f64x4 b, f64x4 c)
    {
        // a - b * c
        return _mm256_fnmadd_pd(b, c, a);
    }

    static inline f64x4 nmsub(f64x4 a, f64x4 b, f64x4 c)
    {
        // -(a + b * c)
        return _mm256_fnmsub_pd(b, c, a);
    }

#elif defined(MANGO_ENABLE_FMA4)

    static inline f64x4 madd(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_macc_pd(b, c, a);
    }

    static inline f64x4 msub(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_msub_pd(b, c, a);
    }

    static inline f64x4 nmadd(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_nmacc_pd(b, c, a);
    }

    static inline f64x4 nmsub(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_nmsub_pd(b, c, a);
    }

#else

    static inline f64x4 madd(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_add_pd(a, _mm256_mul_pd(b, c));
    }

    static inline f64x4 msub(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_sub_pd(_mm256_mul_pd(b, c), a);
    }

    static inline f64x4 nmadd(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_sub_pd(a, _mm256_mul_pd(b, c));
    }

    static inline f64x4 nmsub(f64x4 a, f64x4 b, f64x4 c)
    {
        return _mm256_sub_pd(_mm256_setzero_pd(), _mm256_add_pd(a, _mm256_mul_pd(b, c)));
    }

#endif

    static inline f64x4 lerp(f64x4 a, f64x4 b, f64x4 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

    static inline f64x4 rcp(f64x4 a)
    {
        const __m256d one = _mm256_set1_pd(1.0);
        return _mm256_div_pd(one, a);
    }

    static inline f64x4 rsqrt(f64x4 a)
    {
        const __m256d one = _mm256_set1_pd(1.0);
        return _mm256_div_pd(one, _mm256_sqrt_pd(a));
    }

    static inline f64x4 sqrt(f64x4 a)
    {
        return _mm256_sqrt_pd(a);
    }

    static inline f64 dot4(f64x4 a, f64x4 b)
    {
        const __m256d prod = _mm256_mul_pd(a, b);
        const __m256d zwxy = _mm256_permute2f128_pd(prod, prod, 0x01);
        const __m256d n = _mm256_hadd_pd(prod, zwxy);
        f64x4 s = _mm256_hadd_pd(n, n);
        return get_component<0>(s);
    }

    // compare

    static inline mask64x4 compare_neq(f64x4 a, f64x4 b)
    {
        return _mm256_castpd_si256(_mm256_cmp_pd(a, b, _CMP_NEQ_UQ));
    }

    static inline mask64x4 compare_eq(f64x4 a, f64x4 b)
    {
        return _mm256_castpd_si256(_mm256_cmp_pd(a, b, _CMP_EQ_OQ));
    }

    static inline mask64x4 compare_lt(f64x4 a, f64x4 b)
    {
        return _mm256_castpd_si256(_mm256_cmp_pd(a, b, _CMP_LT_OS));
    }

    static inline mask64x4 compare_le(f64x4 a, f64x4 b)
    {
        return _mm256_castpd_si256(_mm256_cmp_pd(a, b, _CMP_LE_OS));
    }

    static inline mask64x4 compare_gt(f64x4 a, f64x4 b)
    {
        return _mm256_castpd_si256(_mm256_cmp_pd(b, a, _CMP_LT_OS));
    }

    static inline mask64x4 compare_ge(f64x4 a, f64x4 b)
    {
        return _mm256_castpd_si256(_mm256_cmp_pd(b, a, _CMP_LE_OS));
    }

    static inline f64x4 select(mask64x4 mask, f64x4 a, f64x4 b)
    {
        return _mm256_blendv_pd(b, a, _mm256_castsi256_pd(mask));
    }

    // rounding

    static inline f64x4 round(f64x4 s)
    {
        return _mm256_round_pd(s, _MM_FROUND_TO_NEAREST_INT |_MM_FROUND_NO_EXC);
    }

    static inline f64x4 trunc(f64x4 s)
    {
        return _mm256_round_pd(s, _MM_FROUND_TO_ZERO |_MM_FROUND_NO_EXC);
    }

    static inline f64x4 floor(f64x4 s)
    {
        return _mm256_floor_pd(s);
    }

    static inline f64x4 ceil(f64x4 s)
    {
        return _mm256_ceil_pd(s);
    }

    static inline f64x4 fract(f64x4 s)
    {
        return _mm256_sub_pd(s, _mm256_floor_pd(s));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

    // zeromask

    static inline f64x4 min(f64x4 a, f64x4 b, mask64x4 mask)
    {
        return _mm256_and_pd(_mm256_castsi256_pd(mask), min(a, b));
    }

    static inline f64x4 max(f64x4 a, f64x4 b, mask64x4 mask)
    {
        return _mm256_and_pd(_mm256_castsi256_pd(mask), max(a, b));
    }

    static inline f64x4 add(f64x4 a, f64x4 b, mask64x4 mask)
    {
        return _mm256_and_pd(_mm256_castsi256_pd(mask), add(a, b));
    }

    static inline f64x4 sub(f64x4 a, f64x4 b, mask64x4 mask)
    {
        return _mm256_and_pd(_mm256_castsi256_pd(mask), sub(a, b));
    }

    static inline f64x4 mul(f64x4 a, f64x4 b, mask64x4 mask)
    {
        return _mm256_and_pd(_mm256_castsi256_pd(mask), mul(a, b));
    }

    static inline f64x4 div(f64x4 a, f64x4 b, mask64x4 mask)
    {
        return _mm256_and_pd(_mm256_castsi256_pd(mask), div(a, b));
    }

    // mask

    static inline f64x4 min(f64x4 a, f64x4 b, mask64x4 mask, f64x4 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline f64x4 max(f64x4 a, f64x4 b, mask64x4 mask, f64x4 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline f64x4 add(f64x4 a, f64x4 b, mask64x4 mask, f64x4 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline f64x4 sub(f64x4 a, f64x4 b, mask64x4 mask, f64x4 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline f64x4 mul(f64x4 a, f64x4 b, mask64x4 mask, f64x4 value)
    {
        return select(mask, mul(a, b), value);
    }

    static inline f64x4 div(f64x4 a, f64x4 b, mask64x4 mask, f64x4 value)
    {
        return select(mask, div(a, b), value);
    }

} // namespace mango::simd
