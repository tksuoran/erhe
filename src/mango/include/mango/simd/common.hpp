/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // ------------------------------------------------------------------
    // u8x16
    // ------------------------------------------------------------------

    static inline u8x16 clamp(u8x16 v, u8x16 vmin, u8x16 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u8x32
    // ------------------------------------------------------------------

    static inline u8x32 clamp(u8x32 v, u8x32 vmin, u8x32 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u8x64
    // ------------------------------------------------------------------

    static inline u8x64 clamp(u8x64 v, u8x64 vmin, u8x64 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u16x8
    // ------------------------------------------------------------------

    static inline u16x8 clamp(u16x8 v, u16x8 vmin, u16x8 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u16x16
    // ------------------------------------------------------------------

    static inline u16x16 clamp(u16x16 v, u16x16 vmin, u16x16 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u16x32
    // ------------------------------------------------------------------

    static inline u16x32 clamp(u16x32 v, u16x32 vmin, u16x32 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u32x4
    // ------------------------------------------------------------------

    static inline u32x4 clamp(u32x4 v, u32x4 vmin, u32x4 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u32x8
    // ------------------------------------------------------------------

    static inline u32x8 clamp(u32x8 v, u32x8 vmin, u32x8 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u32x16
    // ------------------------------------------------------------------

    static inline u32x16 clamp(u32x16 v, u32x16 vmin, u32x16 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // u64x2
    // ------------------------------------------------------------------

    // ...

    // ------------------------------------------------------------------
    // s8x16
    // ------------------------------------------------------------------

    static inline s8x16 clamp(s8x16 v, s8x16 vmin, s8x16 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s8x32
    // ------------------------------------------------------------------

    static inline s8x32 clamp(s8x32 v, s8x32 vmin, s8x32 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s8x64
    // ------------------------------------------------------------------

    static inline s8x64 clamp(s8x64 v, s8x64 vmin, s8x64 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s16x8
    // ------------------------------------------------------------------

    static inline s16x8 clamp(s16x8 v, s16x8 vmin, s16x8 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s16x16
    // ------------------------------------------------------------------

    static inline s16x16 clamp(s16x16 v, s16x16 vmin, s16x16 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s16x32
    // ------------------------------------------------------------------

    static inline s16x32 clamp(s16x32 v, s16x32 vmin, s16x32 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s32x4
    // ------------------------------------------------------------------

    static inline s32x4 clamp(s32x4 v, s32x4 vmin, s32x4 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s32x8
    // ------------------------------------------------------------------

    static inline s32x8 clamp(s32x8 v, s32x8 vmin, s32x8 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s32x16
    // ------------------------------------------------------------------

    static inline s32x16 clamp(s32x16 v, s32x16 vmin, s32x16 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    // ------------------------------------------------------------------
    // s64x2
    // ------------------------------------------------------------------

    // ...

    // ------------------------------------------------------------------
    // f32x4
    // ------------------------------------------------------------------

    static inline f32x4 clamp(f32x4 v, f32x4 vmin, f32x4 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    static inline f32x4 mod(f32x4 a, f32x4 b)
    {
        return sub(a, mul(b, floor(div(a, b))));
    }

    static inline f32x4 radians(f32x4 a)
    {
        return mul(a, f32x4_set(0.01745329251f));
    }

    static inline f32x4 degrees(f32x4 a)
    {
        return mul(a, f32x4_set(57.2957795131f));
    }

    static inline f32 square(f32x4 a)
    {
        return dot4(a, a);
    }

    static inline f32 length(f32x4 a)
    {
        return f32(std::sqrt(dot4(a, a)));
    }

    static inline f32x4 normalize(f32x4 a)
    {
        return mul(a, rsqrt(f32x4_set(square(a))));
    }

    // ------------------------------------------------------------------
    // f32x8
    // ------------------------------------------------------------------

    static inline f32x8 clamp(f32x8 v, f32x8 vmin, f32x8 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    static inline f32x8 mod(f32x8 a, f32x8 b)
    {
        return sub(a, mul(b, floor(div(a, b))));
    }

    static inline f32x8 radians(f32x8 a)
    {
        return mul(a, f32x8_set(0.01745329251f));
    }

    static inline f32x8 degrees(f32x8 a)
    {
        return mul(a, f32x8_set(57.2957795131f));
    }

    // ------------------------------------------------------------------
    // f32x16
    // ------------------------------------------------------------------

    static inline f32x16 clamp(f32x16 v, f32x16 vmin, f32x16 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    static inline f32x16 mod(f32x16 a, f32x16 b)
    {
        return sub(a, mul(b, floor(div(a, b))));
    }

    static inline f32x16 radians(f32x16 a)
    {
        return mul(a, f32x16_set(0.01745329251f));
    }

    static inline f32x16 degrees(f32x16 a)
    {
        return mul(a, f32x16_set(57.2957795131f));
    }

    // ------------------------------------------------------------------
    // f64x2
    // ------------------------------------------------------------------

    static inline f64x2 clamp(f64x2 v, f64x2 vmin, f64x2 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    static inline f64x2 mod(f64x2 a, f64x2 b)
    {
        return sub(a, mul(b, floor(div(a, b))));
    }

    static inline f64x2 radians(f64x2 a)
    {
        return mul(a, f64x2_set(0.01745329251));
    }

    static inline f64x2 degrees(f64x2 a)
    {
        return mul(a, f64x2_set(57.2957795131));
    }

    static inline f64 square(f64x2 a)
    {
        return dot2(a, a);
    }

    static inline f64 length(f64x2 a)
    {
        return std::sqrt(dot2(a, a));
    }

    static inline f64x2 normalize(f64x2 a)
    {
        return mul(a, rsqrt(f64x2_set(square(a))));
    }

    // ------------------------------------------------------------------
    // f64x4
    // ------------------------------------------------------------------

    static inline f64x4 clamp(f64x4 v, f64x4 vmin, f64x4 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    static inline f64x4 mod(f64x4 a, f64x4 b)
    {
        return sub(a, mul(b, floor(div(a, b))));
    }

    static inline f64x4 radians(f64x4 a)
    {
        return mul(a, f64x4_set(0.01745329251));
    }

    static inline f64x4 degrees(f64x4 a)
    {
        return mul(a, f64x4_set(57.2957795131));
    }

    static inline f64 square(f64x4 a)
    {
        return dot4(a, a);
    }

    static inline f64 length(f64x4 a)
    {
        return std::sqrt(dot4(a, a));
    }

    static inline f64x4 normalize(f64x4 a)
    {
        f64x4 v = f64x4_set(square(a));
        return mul(a, rsqrt(v));
    }

    // ------------------------------------------------------------------
    // f64x8
    // ------------------------------------------------------------------

    static inline f64x8 clamp(f64x8 v, f64x8 vmin, f64x8 vmax)
    {
        return min(vmax, max(vmin, v));
    }

    static inline f64x8 mod(f64x8 a, f64x8 b)
    {
        return sub(a, mul(b, floor(div(a, b))));
    }

    static inline f64x8 radians(f64x8 a)
    {
        return mul(a, f64x8_set(0.01745329251));
    }

    static inline f64x8 degrees(f64x8 a)
    {
        return mul(a, f64x8_set(57.2957795131));
    }

    // ------------------------------------------------------------------
    // macros
    // ------------------------------------------------------------------

    // Hide the worst hacks here in the end. We desperately want to use
    // API that allows to the "constant integer" parameter to be passed as-if
    // the shift was a normal function. CLANG implementation for example does not
    // accept anything else so we do this immoral macro sleight-of-hand to get
    // what we want. The count still has to be a compile-time constant, of course.

    #define slli(Value, Count) slli<Count>(Value)
    #define srli(Value, Count) srli<Count>(Value)
    #define srai(Value, Count) srai<Count>(Value)

} // namespace mango::simd
