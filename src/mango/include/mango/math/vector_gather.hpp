/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    // 128 bit gather

    static inline uint32x4 gather(const u32* address, int32x4 offset)
    {
        return simd::gather(address, offset);
    }

    static inline int32x4 gather(const s32* address, int32x4 offset)
    {
        return simd::gather(address, offset);
    }

    static inline float32x4 gather(const float* address, int32x4 offset)
    {
        return simd::gather(address, offset);
    }

    static inline uint64x2 gather(const u64* address, int32x2 offset)
    {
        return simd::gather(address, simd::s32x2_set(offset.x, offset.y));
    }

    static inline int64x2 gather(const s64* address, int32x2 offset)
    {
        return simd::gather(address, simd::s32x2_set(offset.x, offset.y));
    }

    static inline float64x2 gather(const double* address, int32x2 offset)
    {
        return simd::gather(address, simd::s32x2_set(offset.x, offset.y));
    }

    // 256 bit gather

    static inline uint32x8 gather(const u32* address, int32x8 offset)
    {
        return simd::gather(address, offset);
    }

    static inline int32x8 gather(const s32* address, int32x8 offset)
    {
        return simd::gather(address, offset);
    }

    static inline float32x8 gather(const float* address, int32x8 offset)
    {
        return simd::gather(address, offset);
    }

    static inline uint64x4 gather(const u64* address, int32x4 offset)
    {
        return simd::gather(address, offset);
    }

    static inline int64x4 gather(const s64* address, int32x4 offset)
    {
        return simd::gather(address, offset);
    }

    static inline float64x4 gather(const double* address, int32x4 offset)
    {
        return simd::gather(address, offset);
    }

    // 512 bit gather

    static inline uint32x16 gather(const u32* address, int32x16 offset)
    {
        return simd::gather(address, offset);
    }

    static inline int32x16 gather(const s32* address, int32x16 offset)
    {
        return simd::gather(address, offset);
    }

    static inline float32x16 gather(const float* address, int32x16 offset)
    {
        return simd::gather(address, offset);
    }

    static inline uint64x8 gather(const u64* address, int32x8 offset)
    {
        return simd::gather(address, offset);
    }

    static inline int64x8 gather(const s64* address, int32x8 offset)
    {
        return simd::gather(address, offset);
    }

    static inline float64x8 gather(const double* address, int32x8 offset)
    {
        return simd::gather(address, offset);
    }

    // 128 bit masked gather

    static inline uint32x4 gather(const u32* address, int32x4 offset, uint32x4 value, mask32x4 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline int32x4 gather(const s32* address, int32x4 offset, int32x4 value, mask32x4 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline float32x4 gather(const float* address, int32x4 offset, float32x4 value, mask32x4 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline uint64x2 gather(const u64* address, int32x2 offset, uint64x2 value, mask64x2 mask)
    {
        return simd::gather(address, simd::s32x2_set(offset.x, offset.y), value, mask);
    }

    static inline int64x2 gather(const s64* address, int32x2 offset, int64x2 value, mask64x2 mask)
    {
        return simd::gather(address, simd::s32x2_set(offset.x, offset.y), value, mask);
    }

    static inline float64x2 gather(const double* address, int32x2 offset, float64x2 value, mask64x2 mask)
    {
        return simd::gather(address, simd::s32x2_set(offset.x, offset.y), value, mask);
    }

    // 256 bit masked gather

    static inline uint32x8 gather(const u32* address, int32x8 offset, uint32x8 value, mask32x8 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline int32x8 gather(const s32* address, int32x8 offset, int32x8 value, mask32x8 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline float32x8 gather(const float* address, int32x8 offset, float32x8 value, mask32x8 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline uint64x4 gather(const u64* address, int32x4 offset, uint64x4 value, mask64x4 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline int64x4 gather(const s64* address, int32x4 offset, int64x4 value, mask64x4 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline float64x4 gather(const double* address, int32x4 offset, float64x4 value, mask64x4 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    // 512 bit masked gather

    static inline uint32x16 gather(const u32* address, int32x16 offset, uint32x16 value, mask32x16 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline int32x16 gather(const s32* address, int32x16 offset, int32x16 value, mask32x16 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline float32x16 gather(const float* address, int32x16 offset, float32x16 value, mask32x16 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline uint64x8 gather(const u64* address, int32x8 offset, uint64x8 value, mask64x8 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline int64x8 gather(const s64* address, int32x8 offset, int64x8 value, mask64x8 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

    static inline float64x8 gather(const double* address, int32x8 offset, float64x8 value, mask64x8 mask)
    {
        return simd::gather(address, offset, value, mask);
    }

} // namespace mango::math
