/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // 128 bit gather

    static inline u32x4 gather(const u32* address, s32x4 offset)
    {
        return _mm_i32gather_epi32(reinterpret_cast<const int*>(address), offset, 4);
    }

    static inline s32x4 gather(const s32* address, s32x4 offset)
    {
        return _mm_i32gather_epi32(reinterpret_cast<const int*>(address), offset, 4);
    }

    static inline f32x4 gather(const f32* address, s32x4 offset)
    {
        return _mm_i32gather_ps(reinterpret_cast<const float*>(address), offset, 4);
    }

    static inline u64x2 gather(const u64* address, s32x2 offset)
    {
        __m128i offset128 = _mm_loadl_epi64(reinterpret_cast<__m128i const *>(&offset));
        return _mm_i32gather_epi64(reinterpret_cast<const long long*>(address), offset128, 8);
    }

    static inline s64x2 gather(const s64* address, s32x2 offset)
    {
        __m128i offset128 = _mm_loadl_epi64(reinterpret_cast<__m128i const *>(&offset));
        return _mm_i32gather_epi64(reinterpret_cast<const long long*>(address), offset128, 8);
    }

    static inline f64x2 gather(const f64* address, s32x2 offset)
    {
        __m128i offset128 = _mm_loadl_epi64(reinterpret_cast<__m128i const *>(&offset));
        return _mm_i32gather_pd(reinterpret_cast<const double*>(address), offset128, 8);
    }

    // 256 bit gather

    static inline u32x8 gather(const u32* address, s32x8 offset)
    {
        return _mm256_i32gather_epi32(reinterpret_cast<const int*>(address), offset, 4);
    }

    static inline s32x8 gather(const s32* address, s32x8 offset)
    {
        return _mm256_i32gather_epi32(reinterpret_cast<const int*>(address), offset, 4);
    }

    static inline f32x8 gather(const f32* address, s32x8 offset)
    {
        return _mm256_i32gather_ps(reinterpret_cast<const float*>(address), offset, 4);
    }

    static inline u64x4 gather(const u64* address, s32x4 offset)
    {
        return _mm256_i32gather_epi64(reinterpret_cast<const long long*>(address), offset, 8);
    }

    static inline s64x4 gather(const s64* address, s32x4 offset)
    {
        return _mm256_i32gather_epi64(reinterpret_cast<const long long*>(address), offset, 8);
    }

    static inline f64x4 gather(const f64* address, s32x4 offset)
    {
        return _mm256_i32gather_pd(reinterpret_cast<const double*>(address), offset, 8);
    }

    // 512 bit gather

    static inline u32x16 gather(const u32* address, s32x16 offset)
    {
        u32x16 result;
        result.lo = gather(address, offset.lo);
        result.hi = gather(address, offset.hi);
        return result;
    }

    static inline s32x16 gather(const s32* address, s32x16 offset)
    {
        s32x16 result;
        result.lo = gather(address, offset.lo);
        result.hi = gather(address, offset.hi);
        return result;
    }

    static inline f32x16 gather(const f32* address, s32x16 offset)
    {
        f32x16 result;
        result.lo = gather(address, offset.lo);
        result.hi = gather(address, offset.hi);
        return result;
    }

    static inline u64x8 gather(const u64* address, s32x8 offset)
    {
        u64x8 result;
        result.lo = gather(address, get_low(offset));
        result.hi = gather(address, get_high(offset));
        return result;
    }

    static inline s64x8 gather(const s64* address, s32x8 offset)
    {
        s64x8 result;
        result.lo = gather(address, get_low(offset));
        result.hi = gather(address, get_high(offset));
        return result;
    }

    static inline f64x8 gather(const f64* address, s32x8 offset)
    {
        f64x8 result;
        result.lo = gather(address, get_low(offset));
        result.hi = gather(address, get_high(offset));
        return result;
    }

    // 128 bit masked gather

    static inline u32x4 gather(const u32* address, s32x4 offset, u32x4 value, mask32x4 mask)
    {
        return _mm_mask_i32gather_epi32(value, reinterpret_cast<const int*>(address), offset, mask, 4);
    }

    static inline s32x4 gather(const s32* address, s32x4 offset, s32x4 value, mask32x4 mask)
    {
        return _mm_mask_i32gather_epi32(value, reinterpret_cast<const int*>(address), offset, mask, 4);
    }

    static inline f32x4 gather(const f32* address, s32x4 offset, f32x4 value, mask32x4 mask)
    {
        return _mm_mask_i32gather_ps(value, reinterpret_cast<const float*>(address), offset, _mm_castsi128_ps(mask), 4);
    }

    static inline u64x2 gather(const u64* address, s32x2 offset, u64x2 value, mask64x2 mask)
    {
        __m128i offset128 = _mm_loadl_epi64(reinterpret_cast<__m128i const *>(&offset));
        return _mm_mask_i32gather_epi64(value, reinterpret_cast<const long long*>(address), offset128, mask, 8);
    }

    static inline s64x2 gather(const s64* address, s32x2 offset, s64x2 value, mask64x2 mask)
    {
        __m128i offset128 = _mm_loadl_epi64(reinterpret_cast<__m128i const *>(&offset));
        return _mm_mask_i32gather_epi64(value, reinterpret_cast<const long long*>(address), offset128, mask, 8);
    }

    static inline f64x2 gather(const f64* address, s32x2 offset, f64x2 value, mask64x2 mask)
    {
        __m128i offset128 = _mm_loadl_epi64(reinterpret_cast<__m128i const *>(&offset));
        return _mm_mask_i32gather_pd(value, reinterpret_cast<const double*>(address), offset128, _mm_castsi128_pd(mask), 8);
    }

    // 256 bit masked gather

    static inline u32x8 gather(const u32* address, s32x8 offset, u32x8 value, mask32x8 mask)
    {
        return _mm256_mask_i32gather_epi32(value, reinterpret_cast<const int*>(address), offset, mask, 4);
    }

    static inline s32x8 gather(const s32* address, s32x8 offset, s32x8 value, mask32x8 mask)
    {
        return _mm256_mask_i32gather_epi32(value, reinterpret_cast<const int*>(address), offset, mask, 4);
    }

    static inline f32x8 gather(const f32* address, s32x8 offset, f32x8 value, mask32x8 mask)
    {
        return _mm256_mask_i32gather_ps(value, reinterpret_cast<const float*>(address), offset, _mm256_castsi256_ps(mask), 4);
    }

    static inline u64x4 gather(const u64* address, s32x4 offset, u64x4 value, mask64x4 mask)
    {
        return _mm256_mask_i32gather_epi64(value, reinterpret_cast<const long long*>(address), offset, mask, 8);
    }

    static inline s64x4 gather(const s64* address, s32x4 offset, s64x4 value, mask64x4 mask)
    {
        return _mm256_mask_i32gather_epi64(value, reinterpret_cast<const long long*>(address), offset, mask, 8);
    }

    static inline f64x4 gather(const f64* address, s32x4 offset, f64x4 value, mask64x4 mask)
    {
        return _mm256_mask_i32gather_pd(value, reinterpret_cast<const double*>(address), offset, _mm256_castsi256_pd(mask), 8);
    }

    // 512 bit masked gather

    static inline u32x16 gather(const u32* address, s32x16 offset, u32x16 value, mask32x16 mask)
    {
        return select(mask, gather(address, offset), value);
    }

    static inline s32x16 gather(const s32* address, s32x16 offset, s32x16 value, mask32x16 mask)
    {
        return select(mask, gather(address, offset), value);
    }

    static inline f32x16 gather(const f32* address, s32x16 offset, f32x16 value, mask32x16 mask)
    {
        return select(mask, gather(address, offset), value);
    }

    static inline u64x8 gather(const u64* address, s32x8 offset, u64x8 value, mask64x8 mask)
    {
        return select(mask, gather(address, offset), value);
    }

    static inline s64x8 gather(const s64* address, s32x8 offset, s64x8 value, mask64x8 mask)
    {
        return select(mask, gather(address, offset), value);
    }

    static inline f64x8 gather(const f64* address, s32x8 offset, f64x8 value, mask64x8 mask)
    {
        return select(mask, gather(address, offset), value);
    }

} // namespace mango::simd
