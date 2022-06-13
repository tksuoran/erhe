/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f64x2
    // -----------------------------------------------------------------

    template <u32 X, u32 Y>
    static inline f64x2 shuffle(f64x2 v)
    {
        static_assert(X < 2 && Y < 2, "Index out of range.");
        constexpr u8 x = X * 8;
        constexpr u8 y = Y * 8;
        return vec_perm(v.data, v.data, (u8x16::vector) { 
            x + 0, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7, 
            y + 0, y + 1, y + 2, y + 3, y + 4, y + 5, y + 6, y + 7 });
    }

    template <u32 X, u32 Y>
    static inline f64x2 shuffle(f64x2 a, f64x2 b)
    {
        static_assert(X < 2 && Y < 2, "Index out of range.");
        constexpr u8 x = X * 8;
        constexpr u8 y = Y * 8 + 16;
        return vec_perm(a.data, b.data, (u8x16::vector) { 
            x + 0, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7, 
            y + 0, y + 1, y + 2, y + 3, y + 4, y + 5, y + 6, y + 7 });
    }

    template <>
    inline f64x2 shuffle<0, 1>(f64x2 v)
    {
        // .xy
        return v;
    }

    template <>
    inline f64x2 shuffle<0, 0>(f64x2 v)
    {
        // .xx
        return vec_splat(v.data, 0);
    }

    template <>
    inline f64x2 shuffle<1, 1>(f64x2 v)
    {
        // .yy
        return vec_splat(v.data, 1);
    }
    
    // indexed access

    template <unsigned int Index>
    static inline f64x2 set_component(f64x2 a, f64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <int Index>
    static inline f64 get_component(f64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline f64x2 f64x2_zero()
    {
        return vec_splats(0.0);
    }

    static inline f64x2 f64x2_set(f64 s)
    {
        return vec_splats(s);
    }

    static inline f64x2 f64x2_set(f64 x, f64 y)
    {
        return (f64x2::vector) { x, y };
    }

    static inline f64x2 f64x2_uload(const f64* s)
    {
        return vec_xl(0, s);
    }

    static inline void f64x2_ustore(f64* dest, f64x2 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline f64x2 unpackhi(f64x2 a, f64x2 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline f64x2 unpacklo(f64x2 a, f64x2 b)
    {
        return vec_mergel(a.data, b.data);
    }

    // bitwise

    static inline f64x2 bitwise_nand(f64x2 a, f64x2 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline f64x2 bitwise_and(f64x2 a, f64x2 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline f64x2 bitwise_or(f64x2 a, f64x2 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline f64x2 bitwise_xor(f64x2 a, f64x2 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline f64x2 bitwise_not(f64x2 a)
    {
        return vec_nor(a.data, a.data);
    }

    static inline f64x2 min(f64x2 a, f64x2 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline f64x2 max(f64x2 a, f64x2 b)
    {
        return vec_max(a.data, b.data);
    }

    static inline f64x2 abs(f64x2 a)
    {
        return vec_abs(a.data);
    }

    static inline f64x2 neg(f64x2 a)
    {
        return vec_sub(vec_xor(a.data, a.data), a.data);
    }

    static inline f64x2 sign(f64x2 a)
    {
        auto sign_mask = vec_splats(-0.0);
        auto zero_mask = (f64x2::vector) vec_cmpeq(a.data, vec_splats(0.0));
        auto value_mask = vec_nor(zero_mask, zero_mask);
        auto sign_bits = vec_and(a.data, sign_mask);
        auto value_bits = vec_and(value_mask, vec_splats(1.0));
        return vec_or(value_bits, sign_bits);
    }

    static inline f64x2 add(f64x2 a, f64x2 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline f64x2 mul(f64x2 a, f64x2 b)
    {
        return vec_mul(a.data, b.data);
    }

    static inline f64x2 div(f64x2 a, f64x2 b)
    {
        return vec_div(a.data, b.data);
    }

    static inline f64x2 div(f64x2 a, f64 b)
    {
        return vec_div(a.data, vec_splats(b));
    }

    static inline f64x2 hadd(f64x2 a, f64x2 b)
    {
        return add(unpacklo(a, b), unpackhi(a, b));
    }

    static inline f64x2 hsub(f64x2 a, f64x2 b)
    {
        return sub(unpacklo(a, b), unpackhi(a, b));
    }

    static inline f64x2 madd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a + b * c
        return vec_madd(b.data, c.data, a.data);
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        // b * c - a
        return vec_msub(b.data, c.data, a.data);
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a - b * c
        return vec_nmsub(b.data, c.data, a.data);
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        // -(a + b * c)
        return vec_nmadd(b.data, c.data, a.data);
    }

    static inline f64x2 lerp(f64x2 a, f64x2 b, f64x2 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

    static inline f64x2 rcp(f64x2 a)
    {
        return vec_re(a.data);
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        return vec_rsqrt(a.data);
    }

    static inline f64x2 sqrt(f64x2 a)
    {
        return vec_sqrt(a.data);
    }

    static inline f64 dot2(f64x2 a, f64x2 b)
    {
        f64x2 s = vec_mul(a.data, b.data);
        f64 x = get_component<0>(s);
        f64 y = get_component<1>(s);
        return x + y;
    }

    // compare

    static inline mask64x2 compare_neq(f64x2 a, f64x2 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return (mask64x2::vector) vec_nor((f64x2::vector)mask, (f64x2::vector)mask);
    }

    static inline mask64x2 compare_eq(f64x2 a, f64x2 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask64x2 compare_lt(f64x2 a, f64x2 b)
    {
        return vec_cmplt(a.data, b.data);
    }

    static inline mask64x2 compare_le(f64x2 a, f64x2 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask64x2 compare_gt(f64x2 a, f64x2 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask64x2 compare_ge(f64x2 a, f64x2 b)
    {
        return vec_cmpge(a.data, b.data);
    }

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    // rounding

    static inline f64x2 round(f64x2 s)
    {
        return vec_round(s.data);
    }

    static inline f64x2 trunc(f64x2 s)
    {
        return vec_trunc(s.data);
    }

    static inline f64x2 floor(f64x2 s)
    {
        return vec_floor(s.data);
    }

    static inline f64x2 ceil(f64x2 s)
    {
        return vec_ceil(s.data);
    }

    static inline f64x2 fract(f64x2 s)
    {
        return sub(s, floor(s));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

#define SIMD_ZEROMASK_DOUBLE128
#define SIMD_MASK_DOUBLE128
#include <mango/simd/common_mask.hpp>
#undef SIMD_ZEROMASK_DOUBLE128
#undef SIMD_MASK_DOUBLE128

} // namespace mango::simd
