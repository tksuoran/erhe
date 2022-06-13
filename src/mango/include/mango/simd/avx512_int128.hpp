/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // helpers
    // -----------------------------------------------------------------

#define simd128_shuffle_epi32(a, b, mask) \
    _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b), mask))

#define simd128_shuffle_epi64(a, b, mask) \
    _mm_castpd_si128(_mm_shuffle_pd(_mm_castsi128_pd(a), _mm_castsi128_pd(b), mask))

    namespace detail
    {

        static inline __m128i simd128_not_si128(__m128i a)
        {
            // 3 bit index will be either 000 or 111 as same 'a' is used for all bits
            return _mm_ternarylogic_epi32(a, a, a, 0x01);
        }

        static inline __m128i simd128_select_si128(__m128i mask, __m128i a, __m128i b)
        {
            return _mm_blendv_epi8(b, a, mask);
        }

#if defined(__x86_64__)

        /*
        static inline __m128i simd128_cvtsi64_si128(s64 a)
        {
            return _mm_cvtsi64_si128(a);
        }

        static inline s64 simd128_cvtsi128_si64(__m128i a)
        {
            return _mm_cvtsi128_si64(a);
        }
        */

#else

        /*
        static inline __m128i simd128_cvtsi64_si128(s64 a)
        {
            return _mm_set_epi64x(0, a);
        }

        static inline s64 simd128_cvtsi128_si64(__m128i a)
        {
            u64 value = _mm_cvtsi128_si32(a);
            value |= u64(_mm_cvtsi128_si32(simd128_shuffle_epi32(a, a, 0xee))) << 32;
            return value;
        }
        */

#endif

        static inline __m128i simd128_srli1_epi8(__m128i a)
        {
            a = _mm_srli_epi16(a, 1);
            return _mm_and_si128(a, _mm_set1_epi32(0x7f7f7f7f));
        }

#if 0
        static inline __m128i simd128_srli7_epi8(__m128i a)
        {
            a = _mm_srli_epi16(a, 7);
            return _mm_and_si128(a, _mm_set1_epi32(0x01010101));
        }
#endif

        static inline __m128i simd128_srai1_epi8(__m128i a)
        {
            __m128i b = _mm_slli_epi16(a, 8);
            a = _mm_srai_epi16(a, 1);
            b = _mm_srai_epi16(b, 1);
            a = _mm_and_si128(a, _mm_set1_epi32(0xff00ff00));
            b = _mm_srli_epi16(b, 8);
            return _mm_or_si128(a, b);
        }

        static inline __m128i simd128_srai1_epi64(__m128i a)
        {
            __m128i sign = _mm_and_si128(a, _mm_set1_epi64x(0x8000000000000000ull));
            a = _mm_or_si128(sign, _mm_srli_epi64(a, 1));
            return a;
        }

    } // namespace detail

    // -----------------------------------------------------------------
    // u8x16
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u8x16 set_component(u8x16 a, u8 s)
    {
        static_assert(Index < 16, "Index out of range.");
        return _mm_insert_epi8(a, s, Index);
    }

    template <unsigned int Index>
    static inline u8 get_component(u8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return _mm_extract_epi8(a, Index);
    }

    static inline u8x16 u8x16_zero()
    {
        return _mm_setzero_si128();
    }

    static inline u8x16 u8x16_set(u8 s)
    {
        return _mm_set1_epi8(s);
    }

    static inline u8x16 u8x16_set(
        u8 s0, u8 s1, u8 s2, u8 s3, u8 s4, u8 s5, u8 s6, u8 s7,
        u8 s8, u8 s9, u8 s10, u8 s11, u8 s12, u8 s13, u8 s14, u8 s15)
    {
        return _mm_setr_epi8(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15);
    }

    static inline u8x16 u8x16_uload(const u8* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void u8x16_ustore(u8* dest, u8x16 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline u8x16 u8x16_load_low(const u8* source)
    {
        return _mm_loadl_epi64(reinterpret_cast<__m128i const *>(source));
    }

    static inline void u8x16_store_low(u8* dest, u8x16 a)
    {
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dest), a);
    }

    static inline u8x16 unpacklo(u8x16 a, u8x16 b)
    {
        return _mm_unpacklo_epi8(a, b);
    }

    static inline u8x16 unpackhi(u8x16 a, u8x16 b)
    {
        return _mm_unpackhi_epi8(a, b);
    }

    static inline u8x16 add(u8x16 a, u8x16 b)
    {
        return _mm_add_epi8(a, b);
    }

    static inline u8x16 add(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return _mm_maskz_add_epi8(mask, a, b);
    }

    static inline u8x16 add(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return _mm_mask_add_epi8(value, mask, a, b);
    }

    static inline u8x16 sub(u8x16 a, u8x16 b)
    {
        return _mm_sub_epi8(a, b);
    }

    static inline u8x16 sub(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return _mm_maskz_sub_epi8(mask, a, b);
    }

    static inline u8x16 sub(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return _mm_mask_sub_epi8(value, mask, a, b);
    }

    static inline u8x16 adds(u8x16 a, u8x16 b)
    {
        return _mm_adds_epu8(a, b);
    }

    static inline u8x16 adds(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return _mm_maskz_adds_epu8(mask, a, b);
    }

    static inline u8x16 adds(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return _mm_mask_adds_epu8(value, mask, a, b);
    }

    static inline u8x16 subs(u8x16 a, u8x16 b)
    {
        return _mm_subs_epu8(a, b);
    }

    static inline u8x16 subs(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return _mm_maskz_subs_epu8(mask, a, b);
    }

    static inline u8x16 subs(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return _mm_mask_subs_epu8(value, mask, a, b);
    }

    static inline u8x16 avg(u8x16 a, u8x16 b)
    {
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi8(_mm_and_si128(a, b), detail::simd128_srli1_epi8(axb));
        return temp;
    }

    static inline u8x16 avg_round(u8x16 a, u8x16 b)
    {
        return _mm_avg_epu8(a, b);
    }

    // bitwise

    static inline u8x16 bitwise_nand(u8x16 a, u8x16 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline u8x16 bitwise_and(u8x16 a, u8x16 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline u8x16 bitwise_or(u8x16 a, u8x16 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline u8x16 bitwise_xor(u8x16 a, u8x16 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline u8x16 bitwise_not(u8x16 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask8x16 compare_eq(u8x16 a, u8x16 b)
    {
        return _mm_cmp_epu8_mask(a, b, _MM_CMPINT_EQ);
    }

    static inline mask8x16 compare_gt(u8x16 a, u8x16 b)
    {
        return _mm_cmp_epu8_mask(b, a, _MM_CMPINT_LT);
    }

    static inline mask8x16 compare_neq(u8x16 a, u8x16 b)
    {
        return _mm_cmp_epu8_mask(a, b, 4);
    }

    static inline mask8x16 compare_lt(u8x16 a, u8x16 b)
    {
        return _mm_cmp_epu8_mask(a, b, _MM_CMPINT_LT);
    }

    static inline mask8x16 compare_le(u8x16 a, u8x16 b)
    {
        return _mm_cmp_epu8_mask(a, b, _MM_CMPINT_LE);
    }

    static inline mask8x16 compare_ge(u8x16 a, u8x16 b)
    {
        return _mm_cmp_epu8_mask(b, a, _MM_CMPINT_LE);
    }

    static inline u8x16 select(mask8x16 mask, u8x16 a, u8x16 b)
    {
        return _mm_mask_blend_epi8(mask, b, a);
    }

    static inline u8x16 min(u8x16 a, u8x16 b)
    {
        return _mm_min_epu8(a, b);
    }

    static inline u8x16 min(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return _mm_maskz_min_epu8(mask, a, b);
    }

    static inline u8x16 min(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return _mm_mask_min_epu8(value, mask, a, b);
    }

    static inline u8x16 max(u8x16 a, u8x16 b)
    {
        return _mm_max_epu8(a, b);
    }

    static inline u8x16 max(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return _mm_maskz_max_epu8(mask, a, b);
    }

    static inline u8x16 max(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return _mm_mask_max_epu8(value, mask, a, b);
    }

    // -----------------------------------------------------------------
    // u16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u16x8 set_component(u16x8 a, u16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        return _mm_insert_epi16(a, s, Index);
    }

    template <unsigned int Index>
    static inline u16 get_component(u16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return _mm_extract_epi16(a, Index);
    }

    static inline u16x8 u16x8_zero()
    {
        return _mm_setzero_si128();
    }

    static inline u16x8 u16x8_set(u16 s)
    {
        return _mm_set1_epi16(s);
    }

    static inline u16x8 u16x8_set(u16 s0, u16 s1, u16 s2, u16 s3, u16 s4, u16 s5, u16 s6, u16 s7)
    {
        return _mm_setr_epi16(s0, s1, s2, s3, s4, s5, s6, s7);
    }

    static inline u16x8 u16x8_uload(const u16* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void u16x8_ustore(u16* dest, u16x8 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline u16x8 u16x8_load_low(const u16* source)
    {
        return _mm_loadl_epi64(reinterpret_cast<__m128i const *>(source));
    }

    static inline void u16x8_store_low(u16* dest, u16x8 a)
    {
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dest), a);
    }

    static inline u16x8 unpacklo(u16x8 a, u16x8 b)
    {
        return _mm_unpacklo_epi16(a, b);
    }

    static inline u16x8 unpackhi(u16x8 a, u16x8 b)
    {
        return _mm_unpackhi_epi16(a, b);
    }

    static inline u16x8 add(u16x8 a, u16x8 b)
    {
        return _mm_add_epi16(a, b);
    }

    static inline u16x8 add(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return _mm_maskz_add_epi16(mask, a, b);
    }

    static inline u16x8 add(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return _mm_mask_add_epi16(value, mask, a, b);
    }

    static inline u16x8 sub(u16x8 a, u16x8 b)
    {
        return _mm_sub_epi16(a, b);
    }

    static inline u16x8 sub(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return _mm_maskz_sub_epi16(mask, a, b);
    }

    static inline u16x8 sub(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return _mm_mask_sub_epi16(value, mask, a, b);
    }

    static inline u16x8 adds(u16x8 a, u16x8 b)
    {
        return _mm_adds_epu16(a, b);
    }

    static inline u16x8 adds(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return _mm_maskz_adds_epu16(mask, a, b);
    }

    static inline u16x8 adds(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return _mm_mask_adds_epu16(value, mask, a, b);
    }

    static inline u16x8 subs(u16x8 a, u16x8 b)
    {
        return _mm_subs_epu16(a, b);
    }

    static inline u16x8 subs(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return _mm_maskz_subs_epu16(mask, a, b);
    }

    static inline u16x8 subs(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return _mm_mask_subs_epu16(value, mask, a, b);
    }

    static inline u16x8 avg(u16x8 a, u16x8 b)
    {
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi16(_mm_and_si128(a, b), _mm_srli_epi16(axb, 1));
        return temp;
    }

    static inline u16x8 avg_round(u16x8 a, u16x8 b)
    {
        return _mm_avg_epu16(a, b);
    }

    static inline u16x8 mullo(u16x8 a, u16x8 b)
    {
        return _mm_mullo_epi16(a, b);
    }

    // bitwise

    static inline u16x8 bitwise_nand(u16x8 a, u16x8 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline u16x8 bitwise_and(u16x8 a, u16x8 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline u16x8 bitwise_or(u16x8 a, u16x8 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline u16x8 bitwise_xor(u16x8 a, u16x8 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline u16x8 bitwise_not(u16x8 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask16x8 compare_eq(u16x8 a, u16x8 b)
    {
        return _mm_cmp_epu16_mask(a, b, _MM_CMPINT_EQ);
    }

    static inline mask16x8 compare_gt(u16x8 a, u16x8 b)
    {
        return _mm_cmp_epu16_mask(b, a, _MM_CMPINT_LT);
    }

    static inline mask16x8 compare_neq(u16x8 a, u16x8 b)
    {
        return _mm_cmp_epu16_mask(a, b, 4);
    }

    static inline mask16x8 compare_lt(u16x8 a, u16x8 b)
    {
        return _mm_cmp_epu16_mask(a, b, _MM_CMPINT_LT);
    }

    static inline mask16x8 compare_le(u16x8 a, u16x8 b)
    {
        return _mm_cmp_epu16_mask(a, b, _MM_CMPINT_LE);
    }

    static inline mask16x8 compare_ge(u16x8 a, u16x8 b)
    {
        return _mm_cmp_epu16_mask(b, a, _MM_CMPINT_LE);
    }

    static inline u16x8 select(mask16x8 mask, u16x8 a, u16x8 b)
    {
        return _mm_mask_blend_epi16(mask, b, a);
    }

    static inline u16x8 min(u16x8 a, u16x8 b)
    {
        return _mm_min_epu16(a, b);
    }

    static inline u16x8 min(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return _mm_maskz_min_epu16(mask, a, b);
    }

    static inline u16x8 min(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return _mm_mask_min_epu16(value, mask, a, b);
    }

    static inline u16x8 max(u16x8 a, u16x8 b)
    {
        return _mm_max_epu16(a, b);
    }

    static inline u16x8 max(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return _mm_maskz_max_epu16(mask, a, b);
    }

    static inline u16x8 max(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return _mm_mask_max_epu16(value, mask, a, b);
    }

    // shift by constant

    template <int Count>
    static inline u16x8 slli(u16x8 a)
    {
        return _mm_slli_epi16(a, Count);
    }

    template <int Count>
    static inline u16x8 srli(u16x8 a)
    {
        return _mm_srli_epi16(a, Count);
    }

    template <int Count>
    static inline u16x8 srai(u16x8 a)
    {
        return _mm_srai_epi16(a, Count);
    }

    // shift by scalar

    static inline u16x8 sll(u16x8 a, int count)
    {
        return _mm_sll_epi16(a, _mm_cvtsi32_si128(count));
    }

    static inline u16x8 srl(u16x8 a, int count)
    {
        return _mm_srl_epi16(a, _mm_cvtsi32_si128(count));
    }

    static inline u16x8 sra(u16x8 a, int count)
    {
        return _mm_sra_epi16(a, _mm_cvtsi32_si128(count));
    }
    
    // -----------------------------------------------------------------
    // u32x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline u32x4 shuffle(u32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        return _mm_shuffle_epi32(v, _MM_SHUFFLE(w, z, y, x));
    }

    template <>
    inline u32x4 shuffle<0, 1, 2, 3>(u32x4 v)
    {
        // .xyzw
        return v;
    }

    // indexed access

    template <unsigned int Index>
    static inline u32x4 set_component(u32x4 a, u32 s)
    {
        static_assert(Index < 4, "Index out of range.");
        return _mm_insert_epi32(a, s, Index);
    }

    template <unsigned int Index>
    static inline u32 get_component(u32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return _mm_extract_epi32(a, Index);
    }

    static inline u32x4 u32x4_zero()
    {
        return _mm_setzero_si128();
    }

    static inline u32x4 u32x4_set(u32 s)
    {
        return _mm_set1_epi32(s);
    }

    static inline u32x4 u32x4_set(u32 x, u32 y, u32 z, u32 w)
    {
        return _mm_setr_epi32(x, y, z, w);
    }

    static inline u32x4 u32x4_uload(const u32* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void u32x4_ustore(u32* dest, u32x4 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline u32x4 u32x4_load_low(const u32* source)
    {
        return _mm_loadl_epi64(reinterpret_cast<__m128i const *>(source));
    }

    static inline void u32x4_store_low(u32* dest, u32x4 a)
    {
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dest), a);
    }

    static inline u32x4 unpacklo(u32x4 a, u32x4 b)
    {
        return _mm_unpacklo_epi32(a, b);
    }

    static inline u32x4 unpackhi(u32x4 a, u32x4 b)
    {
        return _mm_unpackhi_epi32(a, b);
    }

    static inline u32x4 add(u32x4 a, u32x4 b)
    {
        return _mm_add_epi32(a, b);
    }

    static inline u32x4 add(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return _mm_maskz_add_epi32(mask, a, b);
    }

    static inline u32x4 add(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return _mm_mask_add_epi32(value, mask, a, b);
    }

    static inline u32x4 sub(u32x4 a, u32x4 b)
    {
        return _mm_sub_epi32(a, b);
    }

    static inline u32x4 sub(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return _mm_maskz_sub_epi32(mask, a, b);
    }

    static inline u32x4 sub(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return _mm_mask_sub_epi32(value, mask, a, b);
    }

    static inline u32x4 adds(u32x4 a, u32x4 b)
    {
  	    const __m128i temp = _mm_add_epi32(a, b);
  	    return _mm_or_si128(temp, _mm_cmplt_epi32(temp, a));
    }

    static inline u32x4 adds(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return _mm_maskz_mov_epi32(mask, adds(a, b));
    }

    static inline u32x4 adds(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return _mm_mask_mov_epi32(value, mask, adds(a, b));
    }

    static inline u32x4 subs(u32x4 a, u32x4 b)
    {
  	    const __m128i temp = _mm_sub_epi32(a, b);
  	    return _mm_and_si128(temp, _mm_cmpgt_epi32(a, temp));
    }

    static inline u32x4 subs(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return _mm_maskz_mov_epi32(mask, subs(a, b));
    }

    static inline u32x4 subs(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return _mm_mask_mov_epi32(value, mask, subs(a, b));
    }

    static inline u32x4 avg(u32x4 a, u32x4 b)
    {
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi32(_mm_and_si128(a, b), _mm_srli_epi32(axb, 1));
        return temp;
    }

    static inline u32x4 avg_round(u32x4 a, u32x4 b)
    {
        __m128i one = _mm_set1_epi32(1);
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_and_si128(a, b);
        temp = _mm_add_epi32(temp, _mm_srli_epi32(axb, 1));
        temp = _mm_add_epi32(temp, _mm_and_si128(axb, one));
        return temp;
    }

    static inline u32x4 mullo(u32x4 a, u32x4 b)
    {
        return _mm_mullo_epi32(a, b);
    }

    // bitwise

    static inline u32x4 bitwise_nand(u32x4 a, u32x4 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline u32x4 bitwise_and(u32x4 a, u32x4 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline u32x4 bitwise_or(u32x4 a, u32x4 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline u32x4 bitwise_xor(u32x4 a, u32x4 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline u32x4 bitwise_not(u32x4 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask32x4 compare_eq(u32x4 a, u32x4 b)
    {
        return _mm_cmp_epu32_mask(a, b, _MM_CMPINT_EQ);
    }

    static inline mask32x4 compare_gt(u32x4 a, u32x4 b)
    {
        return _mm_cmp_epu32_mask(b, a, _MM_CMPINT_LT);
    }

    static inline mask32x4 compare_neq(u32x4 a, u32x4 b)
    {
        return _mm_cmp_epu32_mask(a, b, 4);
    }

    static inline mask32x4 compare_lt(u32x4 a, u32x4 b)
    {
        return _mm_cmp_epu32_mask(a, b, _MM_CMPINT_LT);
    }

    static inline mask32x4 compare_le(u32x4 a, u32x4 b)
    {
        return _mm_cmp_epu32_mask(a, b, _MM_CMPINT_LE);
    }

    static inline mask32x4 compare_ge(u32x4 a, u32x4 b)
    {
        return _mm_cmp_epu32_mask(b, a, _MM_CMPINT_LE);
    }

    static inline u32x4 select(mask32x4 mask, u32x4 a, u32x4 b)
    {
        return _mm_mask_blend_epi32(mask, b, a);
    }

    static inline u32x4 min(u32x4 a, u32x4 b)
    {
        return _mm_min_epu32(a, b);
    }

    static inline u32x4 min(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return _mm_maskz_min_epu32(mask, a, b);
    }

    static inline u32x4 min(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return _mm_mask_min_epu32(value, mask, a, b);
    }

    static inline u32x4 max(u32x4 a, u32x4 b)
    {
        return _mm_max_epu32(a, b);
    }

    static inline u32x4 max(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return _mm_maskz_max_epu32(mask, a, b);
    }

    static inline u32x4 max(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return _mm_mask_max_epu32(value, mask, a, b);
    }

    // shift by constant

    template <int Count>
    static inline u32x4 slli(u32x4 a)
    {
        return _mm_slli_epi32(a, Count);
    }

    template <int Count>
    static inline u32x4 srli(u32x4 a)
    {
        return _mm_srli_epi32(a, Count);
    }

    template <int Count>
    static inline u32x4 srai(u32x4 a)
    {
        return _mm_srai_epi32(a, Count);
    }

    // shift by scalar

    static inline u32x4 sll(u32x4 a, int count)
    {
        return _mm_sll_epi32(a, _mm_cvtsi32_si128(count));
    }

    static inline u32x4 srl(u32x4 a, int count)
    {
        return _mm_srl_epi32(a, _mm_cvtsi32_si128(count));
    }

    static inline u32x4 sra(u32x4 a, int count)
    {
        return _mm_sra_epi32(a, _mm_cvtsi32_si128(count));
    }

    // shift by vector

    static inline u32x4 sll(u32x4 a, u32x4 count)
    {
        return _mm_sllv_epi32(a, count);
    }

    static inline u32x4 srl(u32x4 a, u32x4 count)
    {
        return _mm_srlv_epi32(a, count);
    }

    static inline u32x4 sra(u32x4 a, u32x4 count)
    {
        return _mm_srav_epi32(a, count);
    }

    // -----------------------------------------------------------------
    // u64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u64x2 set_component(u64x2 a, u64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return _mm_insert_epi64(a, s, Index);
    }

    template <unsigned int Index>
    static inline u64 get_component(u64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return _mm_extract_epi64(a, Index);
    }

    static inline u64x2 u64x2_zero()
    {
        return _mm_setzero_si128();
    }

    static inline u64x2 u64x2_set(u64 s)
    {
        return _mm_set1_epi64x(s);
    }

    static inline u64x2 u64x2_set(u64 x, u64 y)
    {
        return _mm_set_epi64x(y, x);
    }

    static inline u64x2 u64x2_uload(const u64* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void u64x2_ustore(u64* dest, u64x2 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline u64x2 unpacklo(u64x2 a, u64x2 b)
    {
        return _mm_unpacklo_epi64(a, b);
    }

    static inline u64x2 unpackhi(u64x2 a, u64x2 b)
    {
        return _mm_unpackhi_epi64(a, b);
    }

    static inline u64x2 add(u64x2 a, u64x2 b)
    {
        return _mm_add_epi64(a, b);
    }

    static inline u64x2 add(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return _mm_maskz_add_epi64(mask, a, b);
    }

    static inline u64x2 add(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return _mm_mask_add_epi64(value, mask, a, b);
    }

    static inline u64x2 sub(u64x2 a, u64x2 b)
    {
        return _mm_sub_epi64(a, b);
    }

    static inline u64x2 sub(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return _mm_maskz_sub_epi64(mask, a, b);
    }

    static inline u64x2 sub(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return _mm_mask_sub_epi64(value, mask, a, b);
    }

    static inline u64x2 avg(u64x2 a, u64x2 b)
    {
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi64(_mm_and_si128(a, b), _mm_srli_epi64(axb, 1));
        return temp;
    }

    static inline u64x2 avg_round(u64x2 a, u64x2 b)
    {
        __m128i one = _mm_set1_epi64x(1);
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_and_si128(a, b);
        temp = _mm_add_epi64(temp, _mm_srli_epi64(axb, 1));
        temp = _mm_add_epi64(temp, _mm_and_si128(axb, one));
        return temp;
    }

    // bitwise

    static inline u64x2 bitwise_nand(u64x2 a, u64x2 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline u64x2 bitwise_and(u64x2 a, u64x2 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline u64x2 bitwise_or(u64x2 a, u64x2 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline u64x2 bitwise_xor(u64x2 a, u64x2 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline u64x2 bitwise_not(u64x2 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask64x2 compare_eq(u64x2 a, u64x2 b)
    {
        return _mm_cmp_epu64_mask(a, b, 0);
    }

    static inline mask64x2 compare_gt(u64x2 a, u64x2 b)
    {
        return _mm_cmp_epu64_mask(b, a, 1);
    }

    static inline mask64x2 compare_neq(u64x2 a, u64x2 b)
    {
        return _mm_cmp_epu64_mask(a, b, 4);
    }

    static inline mask64x2 compare_lt(u64x2 a, u64x2 b)
    {
        return _mm_cmp_epu64_mask(a, b, 1);
    }

    static inline mask64x2 compare_le(u64x2 a, u64x2 b)
    {
        return _mm_cmp_epu64_mask(a, b, 2);
    }

    static inline mask64x2 compare_ge(u64x2 a, u64x2 b)
    {
        return _mm_cmp_epu64_mask(b, a, 2);
    }

    static inline u64x2 select(mask64x2 mask, u64x2 a, u64x2 b)
    {
        return _mm_mask_blend_epi64(mask, b, a);
    }

    static inline u64x2 min(u64x2 a, u64x2 b)
    {
        return _mm_min_epu64(a, b);
    }

    static inline u64x2 min(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return _mm_maskz_min_epu64(mask, a, b);
    }

    static inline u64x2 min(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return _mm_mask_min_epu64(value, mask, a, b);
    }

    static inline u64x2 max(u64x2 a, u64x2 b)
    {
        return _mm_max_epu64(a, b);
    }

    static inline u64x2 max(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return _mm_maskz_max_epu64(mask, a, b);
    }

    static inline u64x2 max(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return _mm_mask_max_epu64(value, mask, a, b);
    }

    // shift by constant

    template <int Count>
    static inline u64x2 slli(u64x2 a)
    {
        return _mm_slli_epi64(a, Count);
    }

    template <int Count>
    static inline u64x2 srli(u64x2 a)
    {
        return _mm_srli_epi64(a, Count);
    }

    // shift by scalar

    static inline u64x2 sll(u64x2 a, int count)
    {
        return _mm_sll_epi64(a, _mm_cvtsi32_si128(count));
    }

    static inline u64x2 srl(u64x2 a, int count)
    {
        return _mm_srl_epi64(a, _mm_cvtsi32_si128(count));
    }

    // -----------------------------------------------------------------
    // s8x16
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s8x16 set_component(s8x16 a, s8 s)
    {
        static_assert(Index < 16, "Index out of range.");
        return _mm_insert_epi8(a, s, Index);
    }

    template <unsigned int Index>
    static inline s8 get_component(s8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return _mm_extract_epi8(a, Index);
    }

    static inline s8x16 s8x16_zero()
    {
        return _mm_setzero_si128();
    }

    static inline s8x16 s8x16_set(s8 s)
    {
        return _mm_set1_epi8(s);
    }

    static inline s8x16 s8x16_set(
        s8 v0, s8 v1, s8 v2, s8 v3, s8 v4, s8 v5, s8 v6, s8 v7,
        s8 v8, s8 v9, s8 v10, s8 v11, s8 v12, s8 v13, s8 v14, s8 v15)
    {
        return _mm_setr_epi8(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15);
    }

    static inline s8x16 s8x16_uload(const s8* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void s8x16_ustore(s8* dest, s8x16 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline s8x16 s8x16_load_low(const s8* source)
    {
        return _mm_loadl_epi64(reinterpret_cast<__m128i const *>(source));
    }

    static inline void s8x16_store_low(s8* dest, s8x16 a)
    {
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dest), a);
    }

    static inline s8x16 unpacklo(s8x16 a, s8x16 b)
    {
        return _mm_unpacklo_epi8(a, b);
    }

    static inline s8x16 unpackhi(s8x16 a, s8x16 b)
    {
        return _mm_unpackhi_epi8(a, b);
    }

    static inline s8x16 add(s8x16 a, s8x16 b)
    {
        return _mm_add_epi8(a, b);
    }

    static inline s8x16 add(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return _mm_maskz_add_epi8(mask, a, b);
    }

    static inline s8x16 add(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return _mm_mask_add_epi8(value, mask, a, b);
    }

    static inline s8x16 sub(s8x16 a, s8x16 b)
    {
        return _mm_sub_epi8(a, b);
    }

    static inline s8x16 sub(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return _mm_maskz_sub_epi8(mask, a, b);
    }

    static inline s8x16 sub(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return _mm_mask_sub_epi8(value, mask, a, b);
    }

    static inline s8x16 adds(s8x16 a, s8x16 b)
    {
        return _mm_adds_epi8(a, b);
    }

    static inline s8x16 adds(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return _mm_maskz_adds_epi8(mask, a, b);
    }

    static inline s8x16 adds(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return _mm_mask_adds_epi8(value, mask, a, b);
    }

    static inline s8x16 subs(s8x16 a, s8x16 b)
    {
        return _mm_subs_epi8(a, b);
    }

    static inline s8x16 subs(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return _mm_maskz_subs_epi8(mask, a, b);
    }

    static inline s8x16 subs(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return _mm_mask_subs_epi8(value, mask, a, b);
    }

    static inline s8x16 avg(s8x16 a, s8x16 b)
    {
		const __m128i sign = _mm_set1_epi8(0x80u);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned average
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi8(_mm_and_si128(a, b), detail::simd128_srai1_epi8(axb));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    static inline s8x16 avg_round(s8x16 a, s8x16 b)
    {
		const __m128i sign = _mm_set1_epi8(0x80u);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned rounded average
        __m128i one = _mm_set1_epi8(1);
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_and_si128(a, b);
        temp = _mm_add_epi8(temp, detail::simd128_srai1_epi8(axb));
        temp = _mm_add_epi8(temp, _mm_and_si128(axb, one));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    static inline s8x16 abs(s8x16 a)
    {
        return _mm_abs_epi8(a);
    }

    static inline s8x16 abs(s8x16 a, mask8x16 mask)
    {
        return _mm_maskz_abs_epi8(mask, a);
    }

    static inline s8x16 abs(s8x16 a, mask8x16 mask, s8x16 value)
    {
        return _mm_mask_abs_epi8(value, mask, a);
    }

    static inline s8x16 neg(s8x16 a)
    {
        return _mm_sub_epi8(_mm_setzero_si128(), a);
    }

    // bitwise

    static inline s8x16 bitwise_nand(s8x16 a, s8x16 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline s8x16 bitwise_and(s8x16 a, s8x16 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline s8x16 bitwise_or(s8x16 a, s8x16 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline s8x16 bitwise_xor(s8x16 a, s8x16 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline s8x16 bitwise_not(s8x16 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask8x16 compare_eq(s8x16 a, s8x16 b)
    {
        return _mm_cmp_epi8_mask(a, b, _MM_CMPINT_EQ);
    }

    static inline mask8x16 compare_gt(s8x16 a, s8x16 b)
    {
        return _mm_cmp_epi8_mask(b, a, _MM_CMPINT_LT);
    }

    static inline mask8x16 compare_neq(s8x16 a, s8x16 b)
    {
        return _mm_cmp_epi8_mask(a, b, 4);
    }

    static inline mask8x16 compare_lt(s8x16 a, s8x16 b)
    {
        return _mm_cmp_epi8_mask(a, b, _MM_CMPINT_LT);
    }

    static inline mask8x16 compare_le(s8x16 a, s8x16 b)
    {
        return _mm_cmp_epi8_mask(a, b, _MM_CMPINT_LE);
    }

    static inline mask8x16 compare_ge(s8x16 a, s8x16 b)
    {
        return _mm_cmp_epi8_mask(b, a, _MM_CMPINT_LE);
    }

    static inline s8x16 select(mask8x16 mask, s8x16 a, s8x16 b)
    {
        return _mm_mask_blend_epi8(mask, b, a);
    }

    static inline s8x16 min(s8x16 a, s8x16 b)
    {
        return _mm_min_epi8(a, b);
    }

    static inline s8x16 min(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return _mm_maskz_min_epi8(mask, a, b);
    }

    static inline s8x16 min(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return _mm_mask_min_epi8(value, mask, a, b);
    }

    static inline s8x16 max(s8x16 a, s8x16 b)
    {
        return _mm_max_epi8(a, b);
    }

    static inline s8x16 max(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return _mm_maskz_max_epi8(mask, a, b);
    }

    static inline s8x16 max(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return _mm_mask_max_epi8(value, mask, a, b);
    }

    // -----------------------------------------------------------------
    // s16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s16x8 set_component(s16x8 a, s16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        return _mm_insert_epi16(a, s, Index);
    }

    template <unsigned int Index>
    static inline s16 get_component(s16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return _mm_extract_epi16(a, Index);
    }

    static inline s16x8 s16x8_zero()
    {
        return _mm_setzero_si128();
    }

    static inline s16x8 s16x8_set(s16 s)
    {
        return _mm_set1_epi16(s);
    }

    static inline s16x8 s16x8_set(s16 s0, s16 s1, s16 s2, s16 s3, s16 s4, s16 s5, s16 s6, s16 s7)
    {
        return _mm_setr_epi16(s0, s1, s2, s3, s4, s5, s6, s7);
    }

    static inline s16x8 s16x8_uload(const s16* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void s16x8_ustore(s16* dest, s16x8 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline s16x8 s16x8_load_low(const s16* source)
    {
        return _mm_loadl_epi64(reinterpret_cast<__m128i const *>(source));
    }

    static inline void s16x8_store_low(s16* dest, s16x8 a)
    {
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dest), a);
    }

    static inline s16x8 unpacklo(s16x8 a, s16x8 b)
    {
        return _mm_unpacklo_epi16(a, b);
    }

    static inline s16x8 unpackhi(s16x8 a, s16x8 b)
    {
        return _mm_unpackhi_epi16(a, b);
    }

    static inline s16x8 add(s16x8 a, s16x8 b)
    {
        return _mm_add_epi16(a, b);
    }

    static inline s16x8 add(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return _mm_maskz_add_epi16(mask, a, b);
    }

    static inline s16x8 add(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return _mm_mask_add_epi16(value, mask, a, b);
    }

    static inline s16x8 sub(s16x8 a, s16x8 b)
    {
        return _mm_sub_epi16(a, b);
    }

    static inline s16x8 sub(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return _mm_maskz_sub_epi16(mask, a, b);
    }

    static inline s16x8 sub(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return _mm_mask_sub_epi16(value, mask, a, b);
    }

    static inline s16x8 adds(s16x8 a, s16x8 b)
    {
        return _mm_adds_epi16(a, b);
    }

    static inline s16x8 adds(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return _mm_maskz_adds_epi16(mask, a, b);
    }

    static inline s16x8 adds(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return _mm_mask_adds_epi16(value, mask, a, b);
    }

    static inline s16x8 subs(s16x8 a, s16x8 b)
    {
        return _mm_subs_epi16(a, b);
    }

    static inline s16x8 subs(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return _mm_maskz_subs_epi16(mask, a, b);
    }

    static inline s16x8 subs(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return _mm_mask_subs_epi16(value, mask, a, b);
    }

    static inline s16x8 hadd(s16x8 a, s16x8 b)
    {
        return _mm_hadd_epi16(a, b);
    }

    static inline s16x8 hsub(s16x8 a, s16x8 b)
    {
        return _mm_hsub_epi16(a, b);
    }

    static inline s16x8 hadds(s16x8 a, s16x8 b)
    {
        return _mm_hadds_epi16(a, b);
    }

    static inline s16x8 hsubs(s16x8 a, s16x8 b)
    {
        return _mm_hsubs_epi16(a, b);
    }

    static inline s16x8 avg(s16x8 a, s16x8 b)
    {
        const __m128i sign = _mm_set1_epi16(0x8000u);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned average
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi16(_mm_and_si128(a, b), _mm_srai_epi16(axb, 1));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    static inline s16x8 avg_round(s16x8 a, s16x8 b)
    {
        const __m128i sign = _mm_set1_epi16(0x8000u);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned rounded average
        __m128i one = _mm_set1_epi16(1);
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_and_si128(a, b);
        temp = _mm_add_epi16(temp, _mm_srli_epi16(axb, 1));
        temp = _mm_add_epi16(temp, _mm_and_si128(axb, one));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    static inline s16x8 mullo(s16x8 a, s16x8 b)
    {
        return _mm_mullo_epi16(a, b);
    }

    static inline s32x4 madd(s16x8 a, s16x8 b)
    {
        return _mm_madd_epi16(a, b);
    }

    static inline s32x4 madd(s16x8 a, s16x8 b, mask32x4 mask)
    {
        return _mm_maskz_madd_epi16(mask, a, b);
    }

    static inline s32x4 madd(s16x8 a, s16x8 b, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_madd_epi16(value, mask, a, b);
    }

    static inline s16x8 abs(s16x8 a)
    {
        return _mm_abs_epi16(a);
    }

    static inline s16x8 abs(s16x8 a, mask16x8 mask)
    {
        return _mm_maskz_abs_epi16(mask, a);
    }

    static inline s16x8 abs(s16x8 a, mask16x8 mask, s16x8 value)
    {
        return _mm_mask_abs_epi16(value, mask, a);
    }

    static inline s16x8 neg(s16x8 a)
    {
        return _mm_sub_epi16(_mm_setzero_si128(), a);
    }

    // bitwise

    static inline s16x8 bitwise_nand(s16x8 a, s16x8 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline s16x8 bitwise_and(s16x8 a, s16x8 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline s16x8 bitwise_or(s16x8 a, s16x8 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline s16x8 bitwise_xor(s16x8 a, s16x8 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline s16x8 bitwise_not(s16x8 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask16x8 compare_eq(s16x8 a, s16x8 b)
    {
        return _mm_cmp_epi16_mask(a, b, _MM_CMPINT_EQ);
    }

    static inline mask16x8 compare_gt(s16x8 a, s16x8 b)
    {
        return _mm_cmp_epi16_mask(b, a, _MM_CMPINT_LT);
    }

    static inline mask16x8 compare_neq(s16x8 a, s16x8 b)
    {
        return _mm_cmp_epi16_mask(a, b, 4);
    }

    static inline mask16x8 compare_lt(s16x8 a, s16x8 b)
    {
        return _mm_cmp_epi16_mask(a, b, _MM_CMPINT_LT);
    }

    static inline mask16x8 compare_le(s16x8 a, s16x8 b)
    {
        return _mm_cmp_epi16_mask(a, b, _MM_CMPINT_LE);
    }

    static inline mask16x8 compare_ge(s16x8 a, s16x8 b)
    {
        return _mm_cmp_epi16_mask(b, a, _MM_CMPINT_LE);
    }

    static inline s16x8 select(mask16x8 mask, s16x8 a, s16x8 b)
    {
        return _mm_mask_blend_epi16(mask, b, a);
    }

    static inline s16x8 min(s16x8 a, s16x8 b)
    {
        return _mm_min_epi16(a, b);
    }

    static inline s16x8 min(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return _mm_maskz_min_epi16(mask, a, b);
    }

    static inline s16x8 min(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return _mm_mask_min_epi16(value, mask, a, b);
    }

    static inline s16x8 max(s16x8 a, s16x8 b)
    {
        return _mm_max_epi16(a, b);
    }

    static inline s16x8 max(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return _mm_maskz_max_epi16(mask, a, b);
    }

    static inline s16x8 max(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return _mm_mask_max_epi16(value, mask, a, b);
    }

    // shift by constant

    template <int Count>
    static inline s16x8 slli(s16x8 a)
    {
        return _mm_slli_epi16(a, Count);
    }

    template <int Count>
    static inline s16x8 srli(s16x8 a)
    {
        return _mm_srli_epi16(a, Count);
    }

    template <int Count>
    static inline s16x8 srai(s16x8 a)
    {
        return _mm_srai_epi16(a, Count);
    }

    // shift by scalar

    static inline s16x8 sll(s16x8 a, int count)
    {
        return _mm_sll_epi16(a, _mm_cvtsi32_si128(count));
    }

    static inline s16x8 srl(s16x8 a, int count)
    {
        return _mm_srl_epi16(a, _mm_cvtsi32_si128(count));
    }

    static inline s16x8 sra(s16x8 a, int count)
    {
        return _mm_sra_epi16(a, _mm_cvtsi32_si128(count));
    }

    // -----------------------------------------------------------------
    // s32x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline s32x4 shuffle(s32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        return _mm_shuffle_epi32(v, _MM_SHUFFLE(w, z, y, x));
    }

    template <>
    inline s32x4 shuffle<0, 1, 2, 3>(s32x4 v)
    {
        // .xyzw
        return v;
    }

    // indexed access

    template <unsigned int Index>
    static inline s32x4 set_component(s32x4 a, s32 s)
    {
        static_assert(Index < 4, "Index out of range.");
        return _mm_insert_epi32(a, s, Index);
    }

    template <unsigned int Index>
    static inline s32 get_component(s32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return _mm_extract_epi32(a, Index);
    }

    static inline s32x4 s32x4_zero()
    {
        return _mm_setzero_si128();
    }

    static inline s32x4 s32x4_set(s32 s)
    {
        return _mm_set1_epi32(s);
    }

    static inline s32x4 s32x4_set(s32 x, s32 y, s32 z, s32 w)
    {
        return _mm_setr_epi32(x, y, z, w);
    }

    static inline s32x4 s32x4_uload(const s32* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void s32x4_ustore(s32* dest, s32x4 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline s32x4 s32x4_load_low(const s32* source)
    {
        return _mm_loadl_epi64(reinterpret_cast<__m128i const *>(source));
    }

    static inline void s32x4_store_low(s32* dest, s32x4 a)
    {
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dest), a);
    }

    static inline s32x4 unpacklo(s32x4 a, s32x4 b)
    {
        return _mm_unpacklo_epi32(a, b);
    }

    static inline s32x4 unpackhi(s32x4 a, s32x4 b)
    {
        return _mm_unpackhi_epi32(a, b);
    }

    static inline s32x4 abs(s32x4 a)
    {
        return _mm_abs_epi32(a);
    }

    static inline s32x4 abs(s32x4 a, mask32x4 mask)
    {
        return _mm_maskz_abs_epi32(mask, a);
    }

    static inline s32x4 abs(s32x4 a, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_abs_epi32(value, mask, a);
    }

    static inline s32x4 neg(s32x4 a)
    {
        return _mm_sub_epi32(_mm_setzero_si128(), a);
    }

    static inline s32x4 add(s32x4 a, s32x4 b)
    {
        return _mm_add_epi32(a, b);
    }

    static inline s32x4 add(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return _mm_maskz_add_epi32(mask, a, b);
    }

    static inline s32x4 add(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_add_epi32(value, mask, a, b);
    }

    static inline s32x4 sub(s32x4 a, s32x4 b)
    {
        return _mm_sub_epi32(a, b);
    }

    static inline s32x4 sub(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return _mm_maskz_sub_epi32(mask, a, b);
    }

    static inline s32x4 sub(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_sub_epi32(value, mask, a, b);
    }

    static inline s32x4 adds(s32x4 a, s32x4 b)
    {
        const __m128i v = _mm_add_epi32(a, b);
        a = _mm_srai_epi32(a, 31);
        __m128i temp = _mm_xor_si128(b, v);
        temp = _mm_xor_si128(temp, _mm_cmpeq_epi32(temp, temp));
        temp = _mm_or_si128(temp, _mm_xor_si128(a, b));
        return detail::simd128_select_si128(_mm_cmpgt_epi32(_mm_setzero_si128(), temp), v, a);
    }

    static inline s32x4 adds(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return _mm_maskz_mov_epi32(mask, adds(a, b));
    }

    static inline s32x4 adds(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_mov_epi32(value, mask, adds(a, b));
    }

    static inline s32x4 subs(s32x4 a, s32x4 b)
    {
        const __m128i v = _mm_sub_epi32(a, b);
        a = _mm_srai_epi32(a, 31);
        __m128i temp = _mm_and_si128(_mm_xor_si128(a, b), _mm_xor_si128(a, v));
        return detail::simd128_select_si128(_mm_cmpgt_epi32(_mm_setzero_si128(), temp), a, v);
    }

    static inline s32x4 subs(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return _mm_maskz_mov_epi32(mask, subs(a, b));
    }

    static inline s32x4 subs(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_mov_epi32(value, mask, subs(a, b));
    }

    static inline s32x4 hadd(s32x4 a, s32x4 b)
    {
        return _mm_hadd_epi32(a, b);
    }

    static inline s32x4 hsub(s32x4 a, s32x4 b)
    {
        return _mm_hsub_epi32(a, b);
    }

    static inline s32x4 avg(s32x4 a, s32x4 b)
    {
        const __m128i sign = _mm_set1_epi32(0x80000000);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned average
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi32(_mm_and_si128(a, b), _mm_srai_epi32(axb, 1));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    static inline s32x4 avg_round(s32x4 a, s32x4 b)
    {
        const __m128i sign = _mm_set1_epi32(0x80000000);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned rounded average
        __m128i one = _mm_set1_epi32(1);
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_and_si128(a, b);
        temp = _mm_add_epi32(temp, _mm_srli_epi32(axb, 1));
        temp = _mm_add_epi32(temp, _mm_and_si128(axb, one));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    static inline s32x4 mullo(s32x4 a, s32x4 b)
    {
        return _mm_mullo_epi32(a, b);
    }

    // bitwise

    static inline s32x4 bitwise_nand(s32x4 a, s32x4 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline s32x4 bitwise_and(s32x4 a, s32x4 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline s32x4 bitwise_or(s32x4 a, s32x4 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline s32x4 bitwise_xor(s32x4 a, s32x4 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline s32x4 bitwise_not(s32x4 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask32x4 compare_eq(s32x4 a, s32x4 b)
    {
        return _mm_cmp_epi32_mask(a, b, _MM_CMPINT_EQ);
    }

    static inline mask32x4 compare_gt(s32x4 a, s32x4 b)
    {
        return _mm_cmp_epi32_mask(b, a, _MM_CMPINT_LT);
    }

    static inline mask32x4 compare_neq(s32x4 a, s32x4 b)
    {
        return _mm_cmp_epi32_mask(a, b, 4);
    }

    static inline mask32x4 compare_lt(s32x4 a, s32x4 b)
    {
        return _mm_cmp_epi32_mask(a, b, _MM_CMPINT_LT);
    }

    static inline mask32x4 compare_le(s32x4 a, s32x4 b)
    {
        return _mm_cmp_epi32_mask(a, b, _MM_CMPINT_LE);
    }

    static inline mask32x4 compare_ge(s32x4 a, s32x4 b)
    {
        return _mm_cmp_epi32_mask(b, a, _MM_CMPINT_LE);
    }

    static inline s32x4 select(mask32x4 mask, s32x4 a, s32x4 b)
    {
        return _mm_mask_blend_epi32(mask, b, a);
    }

    static inline s32x4 min(s32x4 a, s32x4 b)
    {
        return _mm_min_epi32(a, b);
    }

    static inline s32x4 min(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return _mm_maskz_min_epi32(mask, a, b);
    }

    static inline s32x4 min(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_min_epi32(value, mask, a, b);
    }

    static inline s32x4 max(s32x4 a, s32x4 b)
    {
        return _mm_max_epi32(a, b);
    }

    static inline s32x4 max(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return _mm_maskz_max_epi32(mask, a, b);
    }

    static inline s32x4 max(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return _mm_mask_max_epi32(value, mask, a, b);
    }

    // shift by constant

    template <int Count>
    static inline s32x4 slli(s32x4 a)
    {
        return _mm_slli_epi32(a, Count);
    }

    template <int Count>
    static inline s32x4 srli(s32x4 a)
    {
        return _mm_srli_epi32(a, Count);
    }

    template <int Count>
    static inline s32x4 srai(s32x4 a)
    {
        return _mm_srai_epi32(a, Count);
    }

    // shift by scalar

    static inline s32x4 sll(s32x4 a, int count)
    {
        return _mm_sll_epi32(a, _mm_cvtsi32_si128(count));
    }

    static inline s32x4 srl(s32x4 a, int count)
    {
        return _mm_srl_epi32(a, _mm_cvtsi32_si128(count));
    }

    static inline s32x4 sra(s32x4 a, int count)
    {
        return _mm_sra_epi32(a, _mm_cvtsi32_si128(count));
    }

    // shift by vector

    static inline s32x4 sll(s32x4 a, u32x4 count)
    {
        return _mm_sllv_epi32(a, count);
    }

    static inline s32x4 srl(s32x4 a, u32x4 count)
    {
        return _mm_srlv_epi32(a, count);
    }

    static inline s32x4 sra(s32x4 a, u32x4 count)
    {
        return _mm_srav_epi32(a, count);
    }

    static inline u32 pack(s32x4 s)
    {
        __m128i s16 = _mm_packs_epi32(s, s);
        __m128i s8 = _mm_packus_epi16(s16, s16);
        return _mm_cvtsi128_si32(s8);
    }

    static inline s32x4 unpack(u32 s)
    {
        const __m128i i = _mm_cvtsi32_si128(s);
        return _mm_cvtepu8_epi32(i);
    }

    // -----------------------------------------------------------------
    // s64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s64x2 set_component(s64x2 a, s64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return _mm_insert_epi64(a, s, Index);
    }

    template <unsigned int Index>
    static inline s64 get_component(s64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return _mm_extract_epi64(a, Index);
    }

    static inline s64x2 s64x2_zero()
    {
        return _mm_setzero_si128();
    }

    static inline s64x2 s64x2_set(s64 s)
    {
        return _mm_set1_epi64x(s);
    }

    static inline s64x2 s64x2_set(s64 x, s64 y)
    {
        return _mm_set_epi64x(y, x);
    }

    static inline s64x2 s64x2_uload(const s64* source)
    {
        return _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
    }

    static inline void s64x2_ustore(s64* dest, s64x2 a)
    {
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dest), a);
    }

    static inline s64x2 unpacklo(s64x2 a, s64x2 b)
    {
        return _mm_unpacklo_epi64(a, b);
    }

    static inline s64x2 unpackhi(s64x2 a, s64x2 b)
    {
        return _mm_unpackhi_epi64(a, b);
    }

    static inline s64x2 add(s64x2 a, s64x2 b)
    {
        return _mm_add_epi64(a, b);
    }

    static inline s64x2 add(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return _mm_maskz_add_epi64(mask, a, b);
    }

    static inline s64x2 add(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return _mm_mask_add_epi64(value, mask, a, b);
    }

    static inline s64x2 sub(s64x2 a, s64x2 b)
    {
        return _mm_sub_epi64(a, b);
    }

    static inline s64x2 sub(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return _mm_maskz_sub_epi64(mask, a, b);
    }

    static inline s64x2 sub(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return _mm_mask_sub_epi64(value, mask, a, b);
    }

    static inline s64x2 avg(s64x2 a, s64x2 b)
    {
        const __m128i sign = _mm_set1_epi64x(0x8000000000000000ull);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned average
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_add_epi64(_mm_and_si128(a, b), detail::simd128_srai1_epi64(axb));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    static inline s64x2 avg_round(s64x2 a, s64x2 b)
    {
        const __m128i sign = _mm_set1_epi64x(0x8000000000000000ull);
        a = _mm_xor_si128(a, sign);
        b = _mm_xor_si128(b, sign);

        // unsigned rounded average
        __m128i one = _mm_set1_epi64x(1);
        __m128i axb = _mm_xor_si128(a, b);
        __m128i temp = _mm_and_si128(a, b);
        temp = _mm_add_epi64(temp, _mm_srli_epi64(axb, 1));
        temp = _mm_add_epi64(temp, _mm_and_si128(axb, one));

        temp = _mm_xor_si128(temp, sign);
        return temp;
    }

    // bitwise

    static inline s64x2 bitwise_nand(s64x2 a, s64x2 b)
    {
        return _mm_andnot_si128(a, b);
    }

    static inline s64x2 bitwise_and(s64x2 a, s64x2 b)
    {
        return _mm_and_si128(a, b);
    }

    static inline s64x2 bitwise_or(s64x2 a, s64x2 b)
    {
        return _mm_or_si128(a, b);
    }

    static inline s64x2 bitwise_xor(s64x2 a, s64x2 b)
    {
        return _mm_xor_si128(a, b);
    }

    static inline s64x2 bitwise_not(s64x2 a)
    {
        return detail::simd128_not_si128(a);
    }

    // compare

    static inline mask64x2 compare_eq(s64x2 a, s64x2 b)
    {
        return _mm_cmp_epi64_mask(a, b, 0);
    }

    static inline mask64x2 compare_gt(s64x2 a, s64x2 b)
    {
        return _mm_cmp_epi64_mask(b, a, 1);
    }

    static inline mask64x2 compare_neq(s64x2 a, s64x2 b)
    {
        return _mm_cmp_epi64_mask(a, b, 4);
    }

    static inline mask64x2 compare_lt(s64x2 a, s64x2 b)
    {
        return _mm_cmp_epi64_mask(a, b, 1);
    }

    static inline mask64x2 compare_le(s64x2 a, s64x2 b)
    {
        return _mm_cmp_epi64_mask(a, b, 2);
    }

    static inline mask64x2 compare_ge(s64x2 a, s64x2 b)
    {
        return _mm_cmp_epi64_mask(b, a, 2);
    }

    static inline s64x2 select(mask64x2 mask, s64x2 a, s64x2 b)
    {
        return _mm_mask_blend_epi64(mask, b, a);
    }

    static inline s64x2 min(s64x2 a, s64x2 b)
    {
        return _mm_min_epi64(a, b);
    }

    static inline s64x2 min(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return _mm_maskz_min_epi64(mask, a, b);
    }

    static inline s64x2 min(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return _mm_mask_min_epi64(value, mask, a, b);
    }

    static inline s64x2 max(s64x2 a, s64x2 b)
    {
        return _mm_max_epi64(a, b);
    }

    static inline s64x2 max(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return _mm_maskz_max_epi64(mask, a, b);
    }

    static inline s64x2 max(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return _mm_mask_max_epi64(value, mask, a, b);
    }

    // shift by constant

    template <int Count>
    static inline s64x2 slli(s64x2 a)
    {
        return _mm_slli_epi64(a, Count);
    }

    template <int Count>
    static inline s64x2 srli(s64x2 a)
    {
        return _mm_srli_epi64(a, Count);
    }

    // shift by scalar

    static inline s64x2 sll(s64x2 a, int count)
    {
        return _mm_sll_epi64(a, _mm_cvtsi32_si128(count));
    }

    static inline s64x2 srl(s64x2 a, int count)
    {
        return _mm_srl_epi64(a, _mm_cvtsi32_si128(count));
    }

    // -----------------------------------------------------------------
    // mask8x16
    // -----------------------------------------------------------------

#if !defined(MANGO_COMPILER_MICROSOFT)

	static inline mask8x16 operator & (mask8x16 a, mask8x16 b)
    {
        return _mm512_kand(a, b);
    }

    static inline mask8x16 operator | (mask8x16 a, mask8x16 b)
    {
        return _mm512_kor(a, b);
    }

    static inline mask8x16 operator ^ (mask8x16 a, mask8x16 b)
    {
        return _mm512_kxor(a, b);
    }

    static inline mask8x16 operator ! (mask8x16 a)
    {
        return _mm512_knot(a);
    }

#endif

    static inline u32 get_mask(mask8x16 a)
    {
        return u32(a);
    }

    static inline bool none_of(mask8x16 a)
    {
        return get_mask(a) == 0;
    }

    static inline bool any_of(mask8x16 a)
    {
        return get_mask(a) != 0;
    }

    static inline bool all_of(mask8x16 a)
    {
        return get_mask(a) == 0xffff;
    }

    // -----------------------------------------------------------------
    // mask16x8
    // -----------------------------------------------------------------

#if !defined(MANGO_COMPILER_MICROSOFT)

    static inline mask16x8 operator & (mask16x8 a, mask16x8 b)
    {
        return _mm512_kand(a, b);
    }

    static inline mask16x8 operator | (mask16x8 a, mask16x8 b)
    {
        return _mm512_kor(a, b);
    }

    static inline mask16x8 operator ^ (mask16x8 a, mask16x8 b)
    {
        return _mm512_kxor(a, b);
    }

    static inline mask16x8 operator ! (mask16x8 a)
    {
        return _mm512_knot(a);
    }

#endif

    static inline u32 get_mask(mask16x8 a)
    {
        return u32(a);
    }

    static inline bool none_of(mask16x8 a)
    {
        return get_mask(a) == 0;
    }

    static inline bool any_of(mask16x8 a)
    {
        return get_mask(a) != 0;
    }

    static inline bool all_of(mask16x8 a)
    {
        return get_mask(a) == 0xff;
    }

    // -----------------------------------------------------------------
    // mask32x4
    // -----------------------------------------------------------------

#if !defined(MANGO_COMPILER_MICROSOFT)
	
	static inline mask32x4 operator & (mask32x4 a, mask32x4 b)
    {
        return _mm512_kand(a, b);
    }

    static inline mask32x4 operator | (mask32x4 a, mask32x4 b)
    {
        return _mm512_kor(a, b);
    }

    static inline mask32x4 operator ^ (mask32x4 a, mask32x4 b)
    {
        return _mm512_kxor(a, b);
    }

    static inline mask32x4 operator ! (mask32x4 a)
    {
        return _mm512_knot(a);
    }

#endif

    static inline u32 get_mask(mask32x4 a)
    {
        return u32(a);
    }

    static inline bool none_of(mask32x4 a)
    {
        return get_mask(a) == 0;
    }

    static inline bool any_of(mask32x4 a)
    {
        return get_mask(a) != 0;
    }

    static inline bool all_of(mask32x4 a)
    {
        return get_mask(a) == 0xf;
    }

    // -----------------------------------------------------------------
    // mask64x2
    // -----------------------------------------------------------------

#if !defined(MANGO_COMPILER_MICROSOFT)

	static inline mask64x2 operator & (mask64x2 a, mask64x2 b)
    {
        return _mm512_kand(a, b);
    }

    static inline mask64x2 operator | (mask64x2 a, mask64x2 b)
    {
        return _mm512_kor(a, b);
    }

    static inline mask64x2 operator ^ (mask64x2 a, mask64x2 b)
    {
        return _mm512_kxor(a, b);
    }

    static inline mask64x2 operator ! (mask64x2 a)
    {
        return _mm512_knot(a);
    }

#endif

    static inline u32 get_mask(mask64x2 a)
    {
        return u32(a);
    }

    static inline bool none_of(mask64x2 a)
    {
        return get_mask(a) == 0;
    }

    static inline bool any_of(mask64x2 a)
    {
        return get_mask(a) != 0;
    }

    static inline bool all_of(mask64x2 a)
    {
        return get_mask(a) == 0x3;
    }

#undef simd128_shuffle_epi32
#undef simd128_shuffle_epi64

} // namespace mango::simd
