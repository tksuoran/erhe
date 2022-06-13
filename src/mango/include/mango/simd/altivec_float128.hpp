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

#define VEC_SH4(n, select) \
    (select * 16 + n * 4 + 0), \
    (select * 16 + n * 4 + 1), \
    (select * 16 + n * 4 + 2), \
    (select * 16 + n * 4 + 3)

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f32x4 shuffle(f32x4 a, f32x4 b)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const u8x16::vector mask = { VEC_SH4(x, 0), VEC_SH4(y, 0), VEC_SH4(z, 1), VEC_SH4(w, 1) };
        return vec_perm(a.data, b.data, mask);
    }

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f32x4 shuffle(f32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const u8x16::vector mask = { VEC_SH4(x, 0), VEC_SH4(y, 0), VEC_SH4(z, 0), VEC_SH4(w, 0) };
        return vec_perm(v.data, v.data, mask);
    }

#undef VEC_SH4

    template <>
    inline f32x4 shuffle<0, 1, 2, 3>(f32x4 v)
    {
        // .xyzw
        return v;
    }

    template <>
    inline f32x4 shuffle<0, 0, 0, 0>(f32x4 v)
    {
        // .xxxx
        return vec_splat(v.data, 0);
    }

    template <>
    inline f32x4 shuffle<1, 1, 1, 1>(f32x4 v)
    {
        // .yyyy
        return vec_splat(v.data, 1);
    }

    template <>
    inline f32x4 shuffle<2, 2, 2, 2>(f32x4 v)
    {
        // .zzzz
        return vec_splat(v.data, 2);
    }

    template <>
    inline f32x4 shuffle<3, 3, 3, 3>(f32x4 v)
    {
        // .wwww
        return vec_splat(v.data, 3);
    }

    // indexed access

    template <unsigned int Index>
    static inline f32x4 set_component(f32x4 a, f32 s)
    {
        static_assert(Index < 4, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <int Index>
    static inline f32 get_component(f32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline f32x4 f32x4_zero()
    {
        return vec_splats(0.0f);
    }

    static inline f32x4 f32x4_set(f32 s)
    {
        return vec_splats(s);
    }

    static inline f32x4 f32x4_set(f32 x, f32 y, f32 z, f32 w)
    {
        return (f32x4::vector) { x, y, z, w };
    }

    static inline f32x4 f32x4_uload(const f32* s)
    {
        return vec_xl(0, s);
    }

    static inline void f32x4_ustore(f32* dest, f32x4 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline f32x4 movelh(f32x4 a, f32x4 b)
    {
        return shuffle<0, 1, 0, 1>(a, b);
    }

    static inline f32x4 movehl(f32x4 a, f32x4 b)
    {
        return shuffle<2, 3, 2, 3>(a, b);
    }

    static inline f32x4 unpacklo(f32x4 a, f32x4 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline f32x4 unpackhi(f32x4 a, f32x4 b)
    {
        return vec_mergel(a.data, b.data);
    }

    // bitwise

    static inline f32x4 bitwise_nand(f32x4 a, f32x4 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline f32x4 bitwise_and(f32x4 a, f32x4 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline f32x4 bitwise_or(f32x4 a, f32x4 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline f32x4 bitwise_xor(f32x4 a, f32x4 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline f32x4 bitwise_not(f32x4 a)
    {
        return vec_nor(a.data, a.data);
    }

    static inline f32x4 min(f32x4 a, f32x4 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline f32x4 max(f32x4 a, f32x4 b)
    {
        return vec_max(a.data, b.data);
    }

    static inline f32x4 hmin(f32x4 a)
    {
        auto temp = vec_min(a.data, (f32x4::vector) shuffle<2, 3, 0, 1>(a, a));
        return vec_min(temp, (f32x4::vector) shuffle<1, 0, 3, 2>(temp, temp));
    }

    static inline f32x4 hmax(f32x4 a)
    {
        auto temp = vec_max(a.data, (f32x4::vector) shuffle<2, 3, 0, 1>(a, a));
        return vec_max(temp, (f32x4::vector) shuffle<1, 0, 3, 2>(temp, temp));
    }

    static inline f32x4 abs(f32x4 a)
    {
        return vec_abs(a.data);
    }

    static inline f32x4 neg(f32x4 a)
    {
        return vec_sub(vec_xor(a.data, a.data), a.data);
    }

    static inline f32x4 sign(f32x4 a)
    {
        auto sign_mask = vec_splats(-0.0f);
        auto zero_mask = vec_cmpeq(a.data, vec_splats(0.0f));
        auto value_mask = vec_nor(zero_mask, zero_mask);
        auto sign_bits = vec_and(a.data, sign_mask);
        auto value_bits = vec_and(value_mask, vec_splats(1.0f));
        return vec_or(value_bits, sign_bits);
    }

    static inline f32x4 add(f32x4 a, f32x4 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline f32x4 sub(f32x4 a, f32x4 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline f32x4 mul(f32x4 a, f32x4 b)
    {
        return vec_mul(a.data, b.data);
    }

    static inline f32x4 div(f32x4 a, f32x4 b)
    {
        return vec_div(a.data, b.data);
    }

    static inline f32x4 div(f32x4 a, f32 b)
    {
        return vec_div(a.data, vec_splats(b));
    }

    static inline f32x4 hadd(f32x4 a, f32x4 b)
    {
        return vec_add((f32x4::vector) shuffle<0, 2, 0, 2>(a, b),
                       (f32x4::vector) shuffle<1, 3, 1, 3>(a, b));
    }

    static inline f32x4 hsub(f32x4 a, f32x4 b)
    {
        return vec_sub((f32x4::vector) shuffle<0, 2, 0, 2>(a, b),
                       (f32x4::vector) shuffle<1, 3, 1, 3>(a, b));
    }

    static inline f32x4 madd(f32x4 a, f32x4 b, f32x4 c)
    {
        // a + b * c
        return vec_madd(b.data, c.data, a.data);
    }

    static inline f32x4 msub(f32x4 a, f32x4 b, f32x4 c)
    {
        // b * c - a
        return vec_msub(b.data, c.data, a.data);
    }

    static inline f32x4 nmadd(f32x4 a, f32x4 b, f32x4 c)
    {
        // a - b * c
        return vec_nmsub(b.data, c.data, a.data);
    }

    static inline f32x4 nmsub(f32x4 a, f32x4 b, f32x4 c)
    {
        // -(a + b * c)
        return vec_nmadd(b.data, c.data, a.data);
    }

    static inline f32x4 lerp(f32x4 a, f32x4 b, f32x4 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

    static inline f32x4 rcp(f32x4 a)
    {
        return vec_re(a.data);
    }

    static inline f32x4 rsqrt(f32x4 a)
    {
        return vec_rsqrt(a.data);
    }

    static inline f32x4 sqrt(f32x4 a)
    {
        return vec_sqrt(a.data);
    }

    static inline f32 dot3(f32x4 a, f32x4 b)
    {
        f32x4 prod = vec_mul(a.data, b.data);
        f32x4 s = vec_add(shuffle<0, 0, 0, 0>(prod).data,
                      vec_add(shuffle<1, 1, 1, 1>(prod).data, 
                              shuffle<2, 2, 2, 2>(prod).data));
        return get_component<0>(s);
    }

    static inline f32 dot4(f32x4 a, f32x4 b)
    {
        f32x4 s = vec_mul(a.data, b.data);
        s = vec_add(s.data, shuffle<2, 3, 0, 1>(s).data);
        s = vec_add(s.data, shuffle<1, 0, 3, 2>(s).data);
        return get_component<0>(s);
    }

    static inline f32x4 cross3(f32x4 a, f32x4 b)
    {
        f32x4 c = vec_sub(vec_mul(a.data, shuffle<1, 2, 0, 3>(b).data),
                              vec_mul(b.data, shuffle<1, 2, 0, 3>(a).data));
        return shuffle<1, 2, 0, 3>(c);
    }

    // compare

    static inline mask32x4 compare_neq(f32x4 a, f32x4 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask32x4 compare_eq(f32x4 a, f32x4 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask32x4 compare_lt(f32x4 a, f32x4 b)
    {
        return vec_cmplt(a.data, b.data);
    }

    static inline mask32x4 compare_le(f32x4 a, f32x4 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask32x4 compare_gt(f32x4 a, f32x4 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask32x4 compare_ge(f32x4 a, f32x4 b)
    {
        return vec_cmpge(a.data, b.data);
    }

    static inline f32x4 select(mask32x4 mask, f32x4 a, f32x4 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    // rounding

    static inline f32x4 round(f32x4 s)
    {
        return vec_round(s.data);
    }

    static inline f32x4 trunc(f32x4 s)
    {
        return vec_trunc(s.data);
    }

    static inline f32x4 floor(f32x4 s)
    {
        return vec_floor(s.data);
    }

    static inline f32x4 ceil(f32x4 s)
    {
        return vec_ceil(s.data);
    }

    static inline f32x4 fract(f32x4 s)
    {
        return sub(s, floor(s));
    }

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

#define SIMD_ZEROMASK_FLOAT128
#define SIMD_MASK_FLOAT128
#include <mango/simd/common_mask.hpp>
#undef SIMD_ZEROMASK_FLOAT128
#undef SIMD_MASK_FLOAT128

} // namespace mango::simd
