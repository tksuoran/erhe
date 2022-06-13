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

            template <typename T>
            reinterpret_vector(composite_vector<T> v)
            {
                std::memcpy(this, &v, 32);
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

            template <typename T>
            operator composite_vector<T> ()
            {
                composite_vector<T> temp;
                std::memcpy(&temp, this, 32);
                return temp;
            }
        };

        template <>
        struct reinterpret_vector<512>
        {
            reinterpret_vector<256> lo;
            reinterpret_vector<256> hi;

            template <typename T>
            reinterpret_vector(composite_vector<T> v)
                : lo(v.lo)
                , hi(v.hi)
            {
            }

            template <typename T>
            operator composite_vector<T> ()
            {
                return composite_vector<T>(lo, hi);
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

	template <typename D, typename S>
	inline D reinterpret(composite_vector<S> s)
	{
        static_assert(sizeof(composite_vector<S>) == sizeof(D), "Vectors must be same size.");
		return D(detail::reinterpret_vector<composite_vector<S>::vector_bits>(s));
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

#if defined(MANGO_ENABLE_AVX2)

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

#else

    static inline u16x16 extend16x16(u8x16 s)
    {
        u8x16 high = _mm_unpackhi_epi64(s, s);
        u16x16 result;
        result.lo = extend16x8(s);
        result.hi = extend16x8(high);
        return result;
    }

    static inline u32x8 extend32x8(u8x16 s)
    {
        u8x16 high = _mm_unpackhi_epi64(s, s);
        u32x8 result;
        result.lo = extend32x4(s);
        result.hi = extend32x4(high);
        return result;
    }

    static inline u64x4 extend64x4(u8x16 s)
    {
        u8x16 high = _mm_unpackhi_epi64(s, s);
        u64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

    static inline u32x8 extend32x8(u16x8 s)
    {
        u16x8 high = _mm_unpackhi_epi64(s, s);
        u32x8 result;
        result.lo = extend32x4(s);
        result.hi = extend32x4(high);
        return result;
    }

    static inline u64x4 extend64x4(u16x8 s)
    {
        u16x8 high = _mm_unpackhi_epi64(s, s);
        u64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

    static inline u64x4 extend64x4(u32x4 s)
    {
        u32x4 high = _mm_unpackhi_epi64(s, s);
        u64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

#endif

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

#if defined(MANGO_ENABLE_AVX2)

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

#else

    static inline s16x16 extend16x16(s8x16 s)
    {
        s8x16 high = _mm_unpackhi_epi64(s, s);
        s16x16 result;
        result.lo = extend16x8(s);
        result.hi = extend16x8(high);
        return result;
    }

    static inline s32x8 extend32x8(s8x16 s)
    {
        s8x16 high = _mm_unpackhi_epi64(s, s);
        s32x8 result;
        result.lo = extend32x4(s);
        result.hi = extend32x4(high);
        return result;
    }

    static inline s64x4 extend64x4(s8x16 s)
    {
        s8x16 high = _mm_unpackhi_epi64(s, s);
        s64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

    static inline s32x8 extend32x8(s16x8 s)
    {
        s16x8 high = _mm_unpackhi_epi64(s, s);
        s32x8 result;
        result.lo = extend32x4(s);
        result.hi = extend32x4(high);
        return result;
    }

    static inline s64x4 extend64x4(s16x8 s)
    {
        s16x8 high = _mm_unpackhi_epi64(s, s);
        s64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

    static inline s64x4 extend64x4(s32x4 s)
    {
        s32x4 high = _mm_unpackhi_epi64(s, s);
        s64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

#endif

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

#if defined(MANGO_ENABLE_AVX2)

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

#else

    static inline u32x4 get_low(u32x8 a)
    {
        return a.lo;
    }
    
    static inline u32x4 get_high(u32x8 a)
    {
        return a.hi;
    }
    
    static inline u32x8 set_low(u32x8 a, u32x4 low)
    {
        a.lo = low;
        return a;
    }
    
    static inline u32x8 set_high(u32x8 a, u32x4 high)
    {
        a.hi = high;
        return a;
    }

    static inline u32x8 combine(u32x4 a, u32x4 b)
    {
        return u32x8(a, b);
    }

#endif

    // -----------------------------------------------------------------
    // s32
    // -----------------------------------------------------------------

#if defined(MANGO_ENABLE_AVX2)

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

#else

    static inline s32x4 get_low(s32x8 a)
    {
        return a.lo;
    }
    
    static inline s32x4 get_high(s32x8 a)
    {
        return a.hi;
    }
    
    static inline s32x8 set_low(s32x8 a, s32x4 low)
    {
        a.lo = low;
        return a;
    }
    
    static inline s32x8 set_high(s32x8 a, s32x4 high)
    {
        a.hi = high;
        return a;
    }

    static inline s32x8 combine(s32x4 a, s32x4 b)
    {
        return s32x8(a, b);
    }

#endif

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
        const __m128 lo = _mm_cvtepi32_ps(_mm_and_si128(s, _mm_set1_epi32(0xffff)));
        const __m128 hi = _mm_cvtepi32_ps(_mm_srli_epi32(s, 16));
        return _mm_add_ps(lo, _mm_mul_ps(hi, _mm_set1_ps(65536.0f)));
    }

    template <>
    inline f32x4 convert<f32x4>(s32x4 s)
    {
        return _mm_cvtepi32_ps(s);
    }

    template <>
    inline u32x4 convert<u32x4>(f32x4 s)
    {
	    __m128 x2 = _mm_castsi128_ps(_mm_set1_epi32(0x4f000000));
	    __m128 x1 = _mm_cmple_ps(x2, s);
  	    __m128i x0 = _mm_cvtps_epi32(_mm_sub_ps(s, _mm_and_ps(x2, x1)));
  	    return _mm_or_si128(x0, _mm_slli_epi32(_mm_castps_si128(x1), 31));
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

#if defined(MANGO_ENABLE_AVX2)

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
	    __m256 x2 = _mm256_castsi256_ps(_mm256_set1_epi32(0x4f000000));
	    __m256 x1 = _mm256_cmp_ps(x2, s, _CMP_LE_OS);
  	    __m256i x0 = _mm256_cvtps_epi32(_mm256_sub_ps(s, _mm256_and_ps(x2, x1)));
  	    return _mm256_or_si256(x0, _mm256_slli_epi32(_mm256_castps_si256(x1), 31));
    }

    template <>
    inline f32x8 convert<f32x8>(u32x8 s)
    {
        const __m256i mask = _mm256_set1_epi32(0x0000ffff);
        const __m256i onep39 = _mm256_set1_epi32(0x53000000);
        const __m256i x0 = _mm256_or_si256(_mm256_srli_epi32(s, 16), onep39);
        const __m256i x1 = _mm256_and_si256(s, mask);
        const __m256 f1 = _mm256_cvtepi32_ps(x1);
        const __m256 f0 = _mm256_sub_ps(_mm256_castsi256_ps(x0), _mm256_castsi256_ps(onep39));
        return _mm256_add_ps(f0, f1);
    }

    template <>
    inline s32x8 truncate<s32x8>(f32x8 s)
    {
        return _mm256_cvttps_epi32(s);
    }

#else

    template <>
    inline s32x8 convert<s32x8>(f32x8 s)
    {
        __m256i temp = _mm256_cvtps_epi32(s);
        s32x8 result;
        result.lo = _mm256_extractf128_si256(temp, 0);
        result.hi = _mm256_extractf128_si256(temp, 1);
        return result;
    }

    template <>
    inline f32x8 convert<f32x8>(s32x8 s)
    {
        __m256i temp = _mm256_setr_m128i(s.lo, s.hi);
        return _mm256_cvtepi32_ps(temp);
    }

    template <>
    inline u32x8 convert<u32x8>(f32x8 s)
    {
        u32x8 result;
        f32x4 lo = _mm256_extractf128_ps(s, 0);
        f32x4 hi = _mm256_extractf128_ps(s, 1);
        result.lo = convert<u32x4>(lo);
        result.hi = convert<u32x4>(hi);
        return result;
    }

    template <>
    inline f32x8 convert<f32x8>(u32x8 s)
    {
        __m128 lo = convert<f32x4>(s.lo);
        __m128 hi = convert<f32x4>(s.hi);
        return _mm256_setr_m128(lo, hi);
    }

    template <>
    inline s32x8 truncate<s32x8>(f32x8 s)
    {
        __m256i temp = _mm256_cvttps_epi32(s);
        s32x8 result;
        result.lo = _mm256_extractf128_si256(temp, 0);
        result.hi = _mm256_extractf128_si256(temp, 1);
        return result;
    }

#endif

    // 512 bit convert

    template <>
    inline s32x16 convert<s32x16>(f32x16 s)
    {
        s32x16 result;
        result.lo = convert<s32x8>(s.lo);
        result.hi = convert<s32x8>(s.hi);
        return result;
    }

    template <>
    inline f32x16 convert<f32x16>(s32x16 s)
    {
        f32x16 result;
        result.lo = convert<f32x8>(s.lo);
        result.hi = convert<f32x8>(s.hi);
        return result;
    }

    template <>
    inline u32x16 convert<u32x16>(f32x16 s)
    {
        u32x16 result;
        result.lo = convert<u32x8>(s.lo);
        result.hi = convert<u32x8>(s.hi);
        return result;
    }

    template <>
    inline f32x16 convert<f32x16>(u32x16 s)
    {
        f32x16 result;
        result.lo = convert<f32x8>(s.lo);
        result.hi = convert<f32x8>(s.hi);
        return result;
    }

    template <>
    inline s32x16 truncate<s32x16>(f32x16 s)
    {
        s32x16 result;
        result.lo = truncate<s32x8>(s.lo);
        result.hi = truncate<s32x8>(s.hi);
        return result;
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
        const __m256d bias = _mm256_set1_pd((1ll << 52) * 1.5);
        const __m256i xyzw = _mm256_cvtepu32_epi64(ui);
        __m256d v = _mm256_castsi256_pd(xyzw);
        v = _mm256_or_pd(v, bias);
        v = _mm256_sub_pd(v, bias);
        return v;
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
        const __m256d bias = _mm256_set1_pd((1ll << 52) * 1.5);
        const __m256d v = _mm256_add_pd(d, bias);
        const __m128d xxyy = _mm256_castpd256_pd128(v);
        const __m128d zzww = _mm256_extractf128_pd(v, 1);
        __m128 xyzw = _mm_shuffle_ps(_mm_castpd_ps(xxyy), _mm_castpd_ps(zzww), 0x88);
        return _mm_castps_si128(xyzw);
    }

    template <>
    inline f32x4 convert<f32x4>(f64x4 s)
    {
        return _mm256_cvtpd_ps(s);
    }

    template <>
    inline f32x4 convert<f32x4>(s64x4 s)
    {
        f32 x = f32(get_component<0>(s));
        f32 y = f32(get_component<1>(s));
        f32 z = f32(get_component<0>(s));
        f32 w = f32(get_component<1>(s));
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(u64x4 s)
    {
        f32 x = f32(get_component<0>(s));
        f32 y = f32(get_component<1>(s));
        f32 z = f32(get_component<0>(s));
        f32 w = f32(get_component<1>(s));
        return f32x4_set(x, y, z, w);
    }

    // 128 <- 128

    template <>
    inline s64x2 convert<s64x2>(f64x2 v)
    {
        s64 x = s64(get_component<0>(v) + 0.5);
        s64 y = s64(get_component<1>(v) + 0.5);
        return s64x2_set(x, y);
    }

    template <>
    inline u64x2 convert<u64x2>(f64x2 v)
    {
        u64 x = u64(get_component<0>(v) + 0.5);
        u64 y = u64(get_component<1>(v) + 0.5);
        return u64x2_set(x, y);
    }

    template <>
    inline s64x2 truncate<s64x2>(f64x2 v)
    {
        v = trunc(v);
        s64 x = s64(get_component<0>(v));
        s64 y = s64(get_component<1>(v));
        return s64x2_set(x, y);
    }

    template <>
    inline u64x2 truncate<u64x2>(f64x2 v)
    {
        v = trunc(v);
        u64 x = u64(get_component<0>(v));
        u64 y = u64(get_component<1>(v));
        return u64x2_set(x, y);
    }

    template <>
    inline f64x2 convert<f64x2>(s64x2 v)
    {
        f64 x = f64(get_component<0>(v));
        f64 y = f64(get_component<1>(v));
        return f64x2_set(x, y);
    }

    template <>
    inline f64x2 convert<f64x2>(u64x2 v)
    {
        f64 x = f64(get_component<0>(v));
        f64 y = f64(get_component<1>(v));
        return f64x2_set(x, y);
    }

    // 256 <- 256

    template <>
    inline s64x4 convert<s64x4>(f64x4 v)
    {
        s64 x = s64(get_component<0>(v) + 0.5);
        s64 y = s64(get_component<1>(v) + 0.5);
        s64 z = s64(get_component<2>(v) + 0.5);
        s64 w = s64(get_component<3>(v) + 0.5);
        return s64x4_set(x, y, z, w);
    }

    template <>
    inline u64x4 convert<u64x4>(f64x4 v)
    {
        u64 x = u64(get_component<0>(v) + 0.5);
        u64 y = u64(get_component<1>(v) + 0.5);
        u64 z = u64(get_component<2>(v) + 0.5);
        u64 w = u64(get_component<3>(v) + 0.5);
        return u64x4_set(x, y, z, w);
    }

    template <>
    inline s64x4 truncate<s64x4>(f64x4 v)
    {
        v = trunc(v);
        s64 x = s64(get_component<0>(v));
        s64 y = s64(get_component<1>(v));
        s64 z = s64(get_component<2>(v));
        s64 w = s64(get_component<3>(v));
        return s64x4_set(x, y, z, w);
    }

    template <>
    inline u64x4 truncate<u64x4>(f64x4 v)
    {
        v = trunc(v);
        u64 x = u64(get_component<0>(v));
        u64 y = u64(get_component<1>(v));
        u64 z = u64(get_component<2>(v));
        u64 w = u64(get_component<3>(v));
        return u64x4_set(x, y, z, w);
    }

    template <>
    inline f64x4 convert<f64x4>(s64x4 v)
    {
        f64 x = f64(get_component<0>(v));
        f64 y = f64(get_component<1>(v));
        f64 z = f64(get_component<2>(v));
        f64 w = f64(get_component<3>(v));
        return f64x4_set(x, y, z, w);
    }

    template <>
    inline f64x4 convert<f64x4>(u64x4 v)
    {
        f64 x = f64(get_component<0>(v));
        f64 y = f64(get_component<1>(v));
        f64 z = f64(get_component<2>(v));
        f64 w = f64(get_component<3>(v));
        return f64x4_set(x, y, z, w);
    }

    // 512 <- 512

    template <>
    inline s64x8 convert<s64x8>(f64x8 v)
    {
        auto lo = convert<s64x4>(v.lo);
        auto hi = convert<s64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline u64x8 convert<u64x8>(f64x8 v)
    {
        auto lo = convert<u64x4>(v.lo);
        auto hi = convert<u64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline s64x8 truncate<s64x8>(f64x8 v)
    {
        auto lo = truncate<s64x4>(v.lo);
        auto hi = truncate<s64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline u64x8 truncate<u64x8>(f64x8 v)
    {
        auto lo = truncate<u64x4>(v.lo);
        auto hi = truncate<u64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline f64x8 convert<f64x8>(s64x8 v)
    {
        auto lo = convert<f64x4>(v.lo);
        auto hi = convert<f64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline f64x8 convert<f64x8>(u64x8 v)
    {
        auto lo = convert<f64x4>(v.lo);
        auto hi = convert<f64x4>(v.hi);
        return { lo, hi };
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
