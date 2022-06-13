/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>
#include <mango/simd/common.hpp>

namespace mango::simd
{
    namespace detail
    {

        template <int bits>
        struct reinterpret_vector;

        template <>
        struct reinterpret_vector<128>
        {
            __m128 data;

            template <typename ScalarType, int VectorSize, typename VectorType>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, VectorType> v)
                : data(v)
            {
            }

            template <typename ScalarType, int VectorSize>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, __m128i> v)
                : data(_mm_castsi128_ps(v))
            {
            }

            template <typename ScalarType, int VectorSize>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, __m128d> v)
                : data(_mm_castpd_ps(v))
            {
            }

            template <typename ScalarType, int VectorSize, typename VectorType>
            operator hardware_vector<ScalarType, VectorSize, VectorType> ()
            {
                return hardware_vector<ScalarType, VectorSize, VectorType>(data);
            }

            template <typename ScalarType, int VectorSize>
            operator hardware_vector<ScalarType, VectorSize, __m128i> ()
            {
                return hardware_vector<ScalarType, VectorSize, __m128i>(_mm_castps_si128(data));
            }

            template <typename ScalarType, int VectorSize>
            operator hardware_vector<ScalarType, VectorSize, __m128d> ()
            {
                return hardware_vector<ScalarType, VectorSize, __m128d>(_mm_castps_pd(data));
            }
        };

        template <>
        struct reinterpret_vector<256>
        {
            __m256 data;

            template <typename ScalarType, int VectorSize, typename VectorType>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, VectorType> v)
                : data(v)
            {
            }

            template <typename ScalarType, int VectorSize>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, __m256i> v)
                : data(_mm256_castsi256_ps(v))
            {
            }

            template <typename ScalarType, int VectorSize>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, __m256d> v)
                : data(_mm256_castpd_ps(v))
            {
            }

            template <typename ScalarType, int VectorSize, typename VectorType>
            operator hardware_vector<ScalarType, VectorSize, VectorType> ()
            {
                return hardware_vector<ScalarType, VectorSize, VectorType>(data);
            }

            template <typename ScalarType, int VectorSize>
            operator hardware_vector<ScalarType, VectorSize, __m256i> ()
            {
                return hardware_vector<ScalarType, VectorSize, __m256i>(_mm256_castps_si256(data));
            }

            template <typename ScalarType, int VectorSize>
            operator hardware_vector<ScalarType, VectorSize, __m256d> ()
            {
                return hardware_vector<ScalarType, VectorSize, __m256d>(_mm256_castps_pd(data));
            }
        };

        template <>
        struct reinterpret_vector<512>
        {
            __m512 data;

            template <typename ScalarType, int VectorSize, typename VectorType>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, VectorType> v)
                : data(v)
            {
            }

            template <typename ScalarType, int VectorSize>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, __m512i> v)
                : data(_mm512_castsi512_ps(v))
            {
            }

            template <typename ScalarType, int VectorSize>
            reinterpret_vector(hardware_vector<ScalarType, VectorSize, __m512d> v)
                : data(_mm512_castpd_ps(v))
            {
            }

            template <typename ScalarType, int VectorSize, typename VectorType>
            operator hardware_vector<ScalarType, VectorSize, VectorType> ()
            {
                return hardware_vector<ScalarType, VectorSize, VectorType>(data);
            }

            template <typename ScalarType, int VectorSize>
            operator hardware_vector<ScalarType, VectorSize, __m512i> ()
            {
                return hardware_vector<ScalarType, VectorSize, __m512i>(_mm512_castps_si512(data));
            }

            template <typename ScalarType, int VectorSize>
            operator hardware_vector<ScalarType, VectorSize, __m512d> ()
            {
                return hardware_vector<ScalarType, VectorSize, __m512d>(_mm512_castps_pd(data));
            }
        };

    } // namespace detail

    // -----------------------------------------------------------------
    // reinterpret
    // -----------------------------------------------------------------

	template <typename D, typename S0, int S1, typename S2>
	inline D reinterpret(hardware_vector<S0, S1, S2> s)
	{
        static_assert(sizeof(hardware_vector<S0, S1, S2>) == sizeof(D), "Vectors must be same size.");
		return D(detail::reinterpret_vector<hardware_vector<S0, S1, S2>::vector_bits>(s));
	}

    // -----------------------------------------------------------------
    // convert
    // -----------------------------------------------------------------

	template <typename D, typename S>
	inline D convert(S)
	{
		D::undefined_conversion();
	}

	template <typename D, typename S>
	inline D truncate(S)
	{
		D::undefined_conversion();
	}

    // -----------------------------------------------------------------
    // helpers
    // -----------------------------------------------------------------

#if defined(MANGO_COMPILER_GCC)

    // These intrinsics are missing with GCC (tested with 7.3 - 8.1)

    #define _mm256_set_m128(high, low) \
        _mm256_insertf128_ps(_mm256_castps128_ps256(low), high, 1)

    #define _mm256_setr_m128(low, high) \
        _mm256_insertf128_ps(_mm256_castps128_ps256(low), high, 1)

    #define _mm256_set_m128i(high, low) \
        _mm256_insertf128_si256(_mm256_castsi128_si256(low), high, 1)

    #define _mm256_setr_m128i(low, high) \
        _mm256_insertf128_si256(_mm256_castsi128_si256(low), high, 1)

#endif

    // -----------------------------------------------------------------
    // zero extend
    // -----------------------------------------------------------------

    // 128 <- 128

    static inline u16x8 extend16x8(u8x16 s)
    {
        return _mm_cvtepu8_epi16(s);
    }

    static inline u32x4 extend32x4(u8x16 s)
    {
        return _mm_cvtepu8_epi32(s);
    }

    static inline u64x2 extend64x2(u8x16 s)
    {
        return _mm_cvtepu8_epi64(s);
    }

    static inline u32x4 extend32x4(u16x8 s)
    {
        return _mm_cvtepu16_epi32(s);
    }

    static inline u64x2 extend64x2(u16x8 s)
    {
        return _mm_cvtepu16_epi64(s);
    }

    static inline u64x2 extend64x2(u32x4 s)
    {
        return _mm_cvtepu32_epi64(s);
    }

    // 256 <- 128

    static inline u16x16 extend16x16(u8x16 s)
    {
        return _mm256_cvtepu8_epi16(s);
    }

    static inline u32x8 extend32x8(u8x16 s)
    {
        return _mm256_cvtepu8_epi32(s);
    }

    static inline u64x4 extend64x4(u8x16 s)
    {
        return _mm256_cvtepu8_epi64(s);
    }

    static inline u32x8 extend32x8(u16x8 s)
    {
        return  _mm256_cvtepu16_epi32(s);
    }

    static inline u64x4 extend64x4(u16x8 s)
    {
        return _mm256_cvtepu16_epi64(s);
    }

    static inline u64x4 extend64x4(u32x4 s)
    {
        return _mm256_cvtepu32_epi64(s);
    }

    // -----------------------------------------------------------------
    // sign extend
    // -----------------------------------------------------------------

    // 128 <- 128

    static inline s16x8 extend16x8(s8x16 s)
    {
        return _mm_cvtepi8_epi16(s);
    }

    static inline s32x4 extend32x4(s8x16 s)
    {
        return _mm_cvtepi8_epi32(s);
    }

    static inline s64x2 extend64x2(s8x16 s)
    {
        return _mm_cvtepi8_epi64(s);
    }

    static inline s32x4 extend32x4(s16x8 s)
    {
        return _mm_cvtepi16_epi32(s);
    }

    static inline s64x2 extend64x2(s16x8 s)
    {
        return _mm_cvtepi16_epi64(s);
    }

    static inline s64x2 extend64x2(s32x4 s)
    {
        return _mm_cvtepi32_epi64(s);
    }

    // 256 <- 128

    static inline s16x16 extend16x16(s8x16 s)
    {
        return _mm256_cvtepi8_epi16(s);
    }

    static inline s32x8 extend32x8(s8x16 s)
    {
        return _mm256_cvtepi8_epi32(s);
    }

    static inline s64x4 extend64x4(s8x16 s)
    {
        return _mm256_cvtepi8_epi64(s);
    }

    static inline s32x8 extend32x8(s16x8 s)
    {
        return _mm256_cvtepi16_epi32(s);
    }

    static inline s64x4 extend64x4(s16x8 s)
    {
        return _mm256_cvtepi16_epi64(s);
    }

    static inline s64x4 extend64x4(s32x4 s)
    {
        return _mm256_cvtepi32_epi64(s);
    }

    // -----------------------------------------------------------------
    // narrow
    // -----------------------------------------------------------------

    static inline u8x16 narrow(u16x8 a, u16x8 b)
    {
        return _mm_packus_epi16(a, b);
    }

    static inline u16x8 narrow(u32x4 a, u32x4 b)
    {
        return _mm_packus_epi32(a, b);
    }

    static inline s8x16 narrow(s16x8 a, s16x8 b)
    {
        return _mm_packs_epi16(a, b);
    }

    static inline s16x8 narrow(s32x4 a, s32x4 b)
    {
        return _mm_packs_epi32(a, b);
    }

    // -----------------------------------------------------------------
    // u32
    // -----------------------------------------------------------------

    static inline u32x4 get_low(u32x8 a)
    {
        return _mm256_extracti128_si256(a, 0);
    }

    static inline u32x4 get_high(u32x8 a)
    {
        return _mm256_extracti128_si256(a, 1);
    }

    static inline u32x8 set_low(u32x8 a, u32x4 low)
    {
        return _mm256_inserti128_si256(a, low, 0);
    }

    static inline u32x8 set_high(u32x8 a, u32x4 high)
    {
        return _mm256_inserti128_si256(a, high, 1);
    }

    static inline u32x8 combine(u32x4 a, u32x4 b)
    {
        return _mm256_setr_m128i(a, b);
    }

    // -----------------------------------------------------------------
    // s32
    // -----------------------------------------------------------------

    static inline s32x4 get_low(s32x8 a)
    {
        return _mm256_extracti128_si256(a, 0);
    }

    static inline s32x4 get_high(s32x8 a)
    {
        return _mm256_extracti128_si256(a, 1);
    }

    static inline s32x8 set_low(s32x8 a, s32x4 low)
    {
        return _mm256_inserti128_si256(a, low, 0);
    }

    static inline s32x8 set_high(s32x8 a, s32x4 high)
    {
        return _mm256_inserti128_si256(a, high, 1);
    }

    static inline s32x8 combine(s32x4 a, s32x4 b)
    {
        return _mm256_setr_m128i(a, b);
    }

    // -----------------------------------------------------------------
    // f32
    // -----------------------------------------------------------------

    static inline f32x4 get_low(f32x8 a)
    {
        return _mm256_extractf128_ps(a, 0);
    }

    static inline f32x4 get_high(f32x8 a)
    {
        return _mm256_extractf128_ps(a, 1);
    }

    static inline f32x8 set_low(f32x8 a, f32x4 low)
    {
        return _mm256_insertf128_ps(a, low, 0);
    }

    static inline f32x8 set_high(f32x8 a, f32x4 high)
    {
        return _mm256_insertf128_ps(a, high, 1);
    }

    static inline f32x8 combine(f32x4 a, f32x4 b)
    {
        return _mm256_setr_m128(a, b);
    }

    // 128 bit convert

    template <>
    inline f32x4 convert<f32x4>(u32x4 s)
    {
        __m512 temp = _mm512_cvtepu32_ps(_mm512_castsi128_si512(s));
        return _mm512_castps512_ps128(temp);
    }

    template <>
    inline f32x4 convert<f32x4>(s32x4 s)
    {
        return _mm_cvtepi32_ps(s);
    }

    template <>
    inline u32x4 convert<u32x4>(f32x4 s)
    {
        return _mm_cvtps_epu32(s);
    }

    template <>
    inline s32x4 convert<s32x4>(f32x4 s)
    {
        return _mm_cvtps_epi32(s);
    }

    template <>
    inline s32x4 truncate<s32x4>(f32x4 s)
    {
        return _mm_cvttps_epi32(s);
    }

    // 256 bit convert

    template <>
    inline s32x8 convert<s32x8>(f32x8 s)
    {
        return _mm256_cvtps_epi32(s);
    }

    template <>
    inline f32x8 convert<f32x8>(s32x8 s)
    {
        return _mm256_cvtepi32_ps(s);
    }

    template <>
    inline u32x8 convert<u32x8>(f32x8 s)
    {
        return _mm256_cvtps_epu32(s);
    }

    template <>
    inline f32x8 convert<f32x8>(u32x8 s)
    {
        __m512 temp = _mm512_cvtepu32_ps(_mm512_castsi256_si512(s));
        return _mm512_castps512_ps256(temp);
    }

    template <>
    inline s32x8 truncate<s32x8>(f32x8 s)
    {
        return _mm256_cvttps_epi32(s);
    }

    // 512 bit convert

    template <>
    inline s32x16 convert<s32x16>(f32x16 s)
    {
        return _mm512_cvtps_epi32(s);
    }

    template <>
    inline f32x16 convert<f32x16>(s32x16 s)
    {
        return _mm512_cvtepi32_ps(s);
    }

    template <>
    inline u32x16 convert<u32x16>(f32x16 s)
    {
        return _mm512_cvtps_epu32(s);
    }

    template <>
    inline f32x16 convert<f32x16>(u32x16 s)
    {
        return _mm512_cvtepu32_ps(s);
    }

    template <>
    inline s32x16 truncate<s32x16>(f32x16 s)
    {
        return _mm512_cvttps_epi32(s);
    }

    // -----------------------------------------------------------------
    // f64
    // -----------------------------------------------------------------

    static inline f64x2 get_low(f64x4 a)
    {
        return _mm256_extractf128_pd(a, 0);
    }

    static inline f64x2 get_high(f64x4 a)
    {
        return _mm256_extractf128_pd(a, 1);
    }

    static inline f64x4 set_low(f64x4 a, f64x2 low)
    {
        return _mm256_insertf128_pd(a, low, 0);
    }

    static inline f64x4 set_high(f64x4 a, f64x2 high)
    {
        return _mm256_insertf128_pd(a, high, 1);
    }

    static inline f64x4 combine(f64x2 a, f64x2 b)
    {
        return _mm256_insertf128_pd(_mm256_castpd128_pd256(a), b, 1);
    }

    // 256 <- 128

    template <>
    inline f64x4 convert<f64x4>(s32x4 s)
    {
        return _mm256_cvtepi32_pd(s);
    }

    template <>
    inline f64x4 convert<f64x4>(u32x4 ui)
    {
        return _mm256_cvtepu32_pd(ui);
    }

    template <>
    inline f64x4 convert<f64x4>(f32x4 s)
    {
        return _mm256_cvtps_pd(s);
    }

    // 128 <- 256

    static inline s32x4 s32x4_truncate(f64x4 s)
    {
        return _mm256_cvttpd_epi32(s);
    }

    template <>
    inline s32x4 convert<s32x4>(f64x4 s)
    {
        return _mm256_cvtpd_epi32(s);
    }

    template <>
    inline u32x4 convert<u32x4>(f64x4 d)
    {
        return _mm256_cvtpd_epu32(d);
    }

    template <>
    inline f32x4 convert<f32x4>(f64x4 s)
    {
        return _mm256_cvtpd_ps(s);
    }

    template <>
    inline f32x4 convert<f32x4>(s64x4 s)
    {
        return _mm256_cvtepi64_ps(s);
    }

    template <>
    inline f32x4 convert<f32x4>(u64x4 s)
    {
        return _mm256_cvtepu64_ps(s);
    }

    // 128 <- 128

    template <>
    inline s64x2 convert<s64x2>(f64x2 v)
    {
        return _mm_cvtpd_epi64(v);
    }

    template <>
    inline u64x2 convert<u64x2>(f64x2 v)
    {
        return _mm_cvtpd_epu64(v);
    }

    template <>
    inline s64x2 truncate<s64x2>(f64x2 v)
    {
        return _mm_cvttpd_epi64(v);
    }

    template <>
    inline u64x2 truncate<u64x2>(f64x2 v)
    {
        return _mm_cvttpd_epu64(v);
    }

    template <>
    inline f64x2 convert<f64x2>(s64x2 v)
    {
        return _mm_cvtepi64_pd(v);
    }

    template <>
    inline f64x2 convert<f64x2>(u64x2 v)
    {
        return _mm_cvtepu64_pd(v);
    }

    // 256 <- 256

    template <>
    inline s64x4 convert<s64x4>(f64x4 v)
    {
        return _mm256_cvtpd_epi64(v);
    }

    template <>
    inline u64x4 convert<u64x4>(f64x4 v)
    {
        return _mm256_cvtpd_epu64(v);
    }

    template <>
    inline s64x4 truncate<s64x4>(f64x4 v)
    {
        return _mm256_cvttpd_epi64(v);
    }

    template <>
    inline u64x4 truncate<u64x4>(f64x4 v)
    {
        return _mm256_cvttpd_epu64(v);
    }

    template <>
    inline f64x4 convert<f64x4>(s64x4 v)
    {
        return _mm256_cvtepi64_pd(v);
    }

    template <>
    inline f64x4 convert<f64x4>(u64x4 v)
    {
        return _mm256_cvtepu64_pd(v);
    }

    // 512 <- 512

    template <>
    inline s64x8 convert<s64x8>(f64x8 v)
    {
        return _mm512_cvtpd_epi64(v);
    }

    template <>
    inline u64x8 convert<u64x8>(f64x8 v)
    {
        return _mm512_cvtpd_epu64(v);
    }

    template <>
    inline s64x8 truncate<s64x8>(f64x8 v)
    {
        return _mm512_cvttpd_epi64(v);
    }

    template <>
    inline u64x8 truncate<u64x8>(f64x8 v)
    {
        return _mm512_cvttpd_epu64(v);
    }

    template <>
    inline f64x8 convert<f64x8>(s64x8 v)
    {
        return _mm512_cvtepi64_pd(v);
    }

    template <>
    inline f64x8 convert<f64x8>(u64x8 v)
    {
        return _mm512_cvtepu64_pd(v);
    }

    // -----------------------------------------------------------------
    // f16
    // -----------------------------------------------------------------

#ifdef MANGO_ENABLE_F16C

    template <>
    inline f32x4 convert<f32x4>(f16x4 h)
    {
        const __m128i* p = reinterpret_cast<const __m128i *>(&h);
        return _mm_cvtph_ps(_mm_loadl_epi64(p));
    }

    template <>
    inline f16x4 convert<f16x4>(f32x4 f)
    {
        f16x4 h;
        __m128i* p = reinterpret_cast<__m128i *>(&h);
        _mm_storel_epi64(p, _mm_cvtps_ph(f, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
        return h;
    }

#else

    template <>
    inline f32x4 convert<f32x4>(f16x4 h)
    {
        const __m128i* p = reinterpret_cast<const __m128i *>(&h);
        const s32x4 u = _mm_unpacklo_epi16(_mm_loadl_epi64(p), _mm_setzero_si128());

        s32x4 no_sign  = bitwise_and(u, s32x4_set(0x7fff));
        s32x4 sign     = bitwise_and(u, s32x4_set(0x8000));
        s32x4 exponent = bitwise_and(u, s32x4_set(0x7c00));
        s32x4 mantissa = bitwise_and(u, s32x4_set(0x03ff));

        // NaN or Inf
        s32x4 a = bitwise_or(s32x4_set(0x7f800000), slli(mantissa, 13));

        // Zero or Denormal
        const s32x4 magic = s32x4_set(0x3f000000);
        s32x4 b;
        b = add(magic, mantissa);
        b = reinterpret<s32x4>(sub(reinterpret<f32x4>(b), reinterpret<f32x4>(magic)));

        // Numeric Value
        s32x4 c = add(s32x4_set(0x38000000), slli(no_sign, 13));

        // Select a, b, or c based on exponent
        mask32x4 mask;
        s32x4 result;

        mask = compare_eq(exponent, s32x4_zero());
        result = select(mask, b, c);

        mask = compare_eq(exponent, s32x4_set(0x7c00));
        result = select(mask, a, result);

        // Sign
        result = bitwise_or(result, slli(sign, 16));

        return reinterpret<f32x4>(result);
    }

    template <>
    inline f16x4 convert<f16x4>(f32x4 f)
    {
        const f32x4 magic = f32x4_set(Float(0, 15, 0).f);
        const s32x4 vinf = s32x4_set(31 << 23);

        const s32x4 u = reinterpret<s32x4>(f);
        const s32x4 sign = srli(bitwise_and(u, s32x4_set(0x80000000)), 16);

        const s32x4 vexponent = s32x4_set(0x7f800000);

        // Inf / NaN
        const mask32x4 s0 = compare_eq(bitwise_and(u, vexponent), vexponent);
        s32x4 mantissa = bitwise_and(u, s32x4_set(0x007fffff));
        mask32x4 x0 = compare_eq(mantissa, s32x4_zero());
        mantissa = select(x0, s32x4_zero(), srai(mantissa, 13));
        const s32x4 v0 = bitwise_or(s32x4_set(0x7c00), mantissa);

        s32x4 v1 = bitwise_and(u, s32x4_set(0x7ffff000));
        v1 = reinterpret<s32x4>(mul(reinterpret<f32x4>(v1), magic));
        v1 = add(v1, s32x4_set(0x1000));

        v1 = _mm_min_epi32(v1, vinf);
        v1 = srai(v1, 13);

        s32x4 v = select(s0, v0, v1);
        v = bitwise_or(v, sign);
        v = _mm_packus_epi32(v, v);

        f16x4 h;
        _mm_storel_epi64(reinterpret_cast<__m128i *>(&h), v);
        return h;
    }

#endif // MANGO_ENABLE_F16C

} // namespace mango::simd
