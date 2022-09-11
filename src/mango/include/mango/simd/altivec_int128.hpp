/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // u8x16
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u8x16 set_component(u8x16 a, u8 s)
    {
        static_assert(Index < 16, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline u8 get_component(u8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline u8x16 u8x16_zero()
    {
        return vec_splats(u8(0));
    }

    static inline u8x16 u8x16_set(u8 s)
    {
        return vec_splats(s);
    }

    static inline u8x16 u8x16_set(
        u8 v0, u8 v1, u8 v2, u8 v3, u8 v4, u8 v5, u8 v6, u8 v7,
        u8 v8, u8 v9, u8 v10, u8 v11, u8 v12, u8 v13, u8 v14, u8 v15)
    {
        return (u8x16::vector) { v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15 };
    }

    static inline u8x16 u8x16_uload(const u8* source)
    {
        return vec_xl(0, source);
    }

    static inline void u8x16_ustore(u8* dest, u8x16 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline u8x16 u8x16_load_low(const u8* source)
    {
        auto s0 = source[0];
        auto s1 = source[1];
        auto s2 = source[2];
        auto s3 = source[3];
        auto s4 = source[4];
        auto s5 = source[5];
        auto s6 = source[6];
        auto s7 = source[7];
        return (u8x16::vector) { s0, s1, s2, s3, s4, s5, s6, s7, 0, 0, 0, 0, 0, 0, 0, 0 };
    }

    static inline void u8x16_store_low(u8* dest, u8x16 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline u8x16 unpacklo(u8x16 a, u8x16 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline u8x16 unpackhi(u8x16 a, u8x16 b)
    {
        return vec_mergel(a.data, b.data);
    }

    static inline u8x16 add(u8x16 a, u8x16 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline u8x16 sub(u8x16 a, u8x16 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline u8x16 adds(u8x16 a, u8x16 b)
    {
        return vec_adds(a.data, b.data);
    }

    static inline u8x16 subs(u8x16 a, u8x16 b)
    {
        return vec_subs(a.data, b.data);
    }

    static inline u8x16 avg(u8x16 a, u8x16 b)
    {
        auto axb = vec_xor(a.data, b.data);
        auto temp = vec_add(vec_and(a.data, b.data), vec_sr(axb, vec_splats(u8(1))));
        return temp;
    }

    static inline u8x16 avg_round(u8x16 a, u8x16 b)
    {
        return vec_avg(a, b);
    }

    // bitwise

    static inline u8x16 bitwise_nand(u8x16 a, u8x16 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline u8x16 bitwise_and(u8x16 a, u8x16 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline u8x16 bitwise_or(u8x16 a, u8x16 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline u8x16 bitwise_xor(u8x16 a, u8x16 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline u8x16 bitwise_not(u8x16 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask8x16 compare_eq(u8x16 a, u8x16 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask8x16 compare_gt(u8x16 a, u8x16 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask8x16 compare_neq(u8x16 a, u8x16 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask8x16 compare_lt(u8x16 a, u8x16 b)
    {
        return vec_cmplt(a.data, b.data);
    }

#if 0

    static inline mask8x16 compare_le(u8x16 a, u8x16 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask8x16 compare_ge(u8x16 a, u8x16 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask8x16 compare_le(u8x16 a, u8x16 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask8x16 compare_ge(u8x16 a, u8x16 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }

#endif

    static inline u8x16 select(mask8x16 mask, u8x16 a, u8x16 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline u8x16 min(u8x16 a, u8x16 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline u8x16 max(u8x16 a, u8x16 b)
    {
        return vec_max(a.data, b.data);
    }

    // -----------------------------------------------------------------
    // u16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u16x8 set_component(u16x8 a, u16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline u16 get_component(u16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline u16x8 u16x8_zero()
    {
        return vec_splats(u16(0));
    }

    static inline u16x8 u16x8_set(u16 s)
    {
        return vec_splats(s);
    }

    static inline u16x8 u16x8_set(u16 s0, u16 s1, u16 s2, u16 s3, u16 s4, u16 s5, u16 s6, u16 s7)
    {
        return (u16x8::vector) { s0, s1, s2, s3, s4, s5, s6, s7 };
    }

    static inline u16x8 u16x8_uload(const u16* source)
    {
        return vec_xl(0, source);
    }

    static inline void u16x8_ustore(u16* dest, u16x8 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline u16x8 u16x8_load_low(const u16* source)
    {
        auto s0 = source[0];
        auto s1 = source[1];
        auto s2 = source[2];
        auto s3 = source[3];
        return (u16x8::vector) { s0, s1, s2, s3, 0, 0, 0, 0 };
    }

    static inline void u16x8_store_low(u16* dest, u16x8 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline u16x8 unpacklo(u16x8 a, u16x8 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline u16x8 unpackhi(u16x8 a, u16x8 b)
    {
        return vec_mergel(a.data, b.data);
    }

    static inline u16x8 add(u16x8 a, u16x8 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline u16x8 sub(u16x8 a, u16x8 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline u16x8 adds(u16x8 a, u16x8 b)
    {
        return vec_adds(a.data, b.data);
    }

    static inline u16x8 subs(u16x8 a, u16x8 b)
    {
        return vec_subs(a.data, b.data);
    }

    static inline u16x8 mullo(u16x8 a, u16x8 b)
    {
        return vec_mladd(a.data, b.data, vec_xor(a.data, a.data));
    }

    static inline u16x8 avg(u16x8 a, u16x8 b)
    {
        auto axb = vec_xor(a.data, b.data);
        auto temp = vec_add(vec_and(a.data, b.data), vec_sr(axb, vec_splats(u16(1))));
        return temp;
    }

    static inline u16x8 avg_round(u16x8 a, u16x8 b)
    {
        return vec_avg(a, b);
    }

    // bitwise

    static inline u16x8 bitwise_nand(u16x8 a, u16x8 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline u16x8 bitwise_and(u16x8 a, u16x8 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline u16x8 bitwise_or(u16x8 a, u16x8 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline u16x8 bitwise_xor(u16x8 a, u16x8 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline u16x8 bitwise_not(u16x8 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask16x8 compare_neq(u16x8 a, u16x8 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask16x8 compare_lt(u16x8 a, u16x8 b)
    {
        return vec_cmplt(a.data, b.data);
    }

    static inline mask16x8 compare_eq(u16x8 a, u16x8 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask16x8 compare_gt(u16x8 a, u16x8 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

#if 0

    static inline mask16x8 compare_le(u16x8 a, u16x8 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask16x8 compare_ge(u16x8 a, u16x8 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask16x8 compare_le(u16x8 a, u16x8 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask16x8 compare_ge(u16x8 a, u16x8 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }

#endif

    static inline u16x8 select(mask16x8 mask, u16x8 a, u16x8 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline u16x8 min(u16x8 a, u16x8 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline u16x8 max(u16x8 a, u16x8 b)
    {
        return vec_max(a.data, b.data);
    }

    // shift by constant

    template <int Count>
    static inline u16x8 slli(u16x8 a)
    {
        return vec_sl(a.data, vec_splats(u16(Count)));
    }

    template <int Count>
    static inline u16x8 srli(u16x8 a)
    {
        return vec_sr(a.data, vec_splats(u16(Count)));
    }

    template <int Count>
    static inline u16x8 srai(u16x8 a)
    {
        return vec_sra(a.data, vec_splats(u16(Count)));
    }

    // shift by scalar

    static inline u16x8 sll(u16x8 a, int count)
    {
        return vec_sl(a.data, vec_splats(u16(count)));
    }

    static inline u16x8 srl(u16x8 a, int count)
    {
        return vec_sr(a.data, vec_splats(u16(count)));
    }

    static inline u16x8 sra(u16x8 a, int count)
    {
        return vec_sra(a.data, vec_splats(u16(count)));
    }
    
    // -----------------------------------------------------------------
    // u32x4
    // -----------------------------------------------------------------

    // shuffle

#define VEC_SH4(n, select) \
    (select * 16 + n * 4 + 0), \
    (select * 16 + n * 4 + 1), \
    (select * 16 + n * 4 + 2), \
    (select * 16 + n * 4 + 3)

    template <u32 x, u32 y, u32 z, u32 w>
    static inline u32x4 shuffle(u32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const u8x16::vector mask = { VEC_SH4(x, 0), VEC_SH4(y, 0), VEC_SH4(z, 0), VEC_SH4(w, 0) };
        return vec_perm(v.data, v.data, mask);
    }

#undef VEC_SH4

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
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline u32 get_component(u32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline u32x4 u32x4_zero()
    {
        return vec_splats(u32(0));
    }

    static inline u32x4 u32x4_set(u32 s)
    {
        return vec_splats(s);
    }

    static inline u32x4 u32x4_set(u32 x, u32 y, u32 z, u32 w)
    {
        return (u32x4::vector) { x, y, z, w };
    }

    static inline u32x4 u32x4_uload(const u32* source)
    {
        return vec_xl(0, source);
    }

    static inline void u32x4_ustore(u32* dest, u32x4 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline u32x4 u32x4_load_low(const u32* source)
    {
        auto s0 = source[0];
        auto s1 = source[1];
        return (u32x4::vector) { s0, s1, 0, 0 };
    }

    static inline void u32x4_store_low(u32* dest, u32x4 a)
    {
        dest[0] = vec_extract(a.data, 0);
        dest[1] = vec_extract(a.data, 1);
    }

    static inline u32x4 unpacklo(u32x4 a, u32x4 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline u32x4 unpackhi(u32x4 a, u32x4 b)
    {
        return vec_mergel(a.data, b.data);
    }

    static inline u32x4 add(u32x4 a, u32x4 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline u32x4 sub(u32x4 a, u32x4 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline u32x4 adds(u32x4 a, u32x4 b)
    {
        return vec_adds(a.data, b.data);
    }

    static inline u32x4 subs(u32x4 a, u32x4 b)
    {
        return vec_subs(a.data, b.data);
    }

    static inline u32x4 mullo(u32x4 a, u32x4 b)
    {
        f32x4 af = vec_ctf(a.data, 0);
        f32x4 bf = vec_ctf(b.data, 0);
        return vec_ctu(vec_mul(af.data, bf.data), 0);
    }

    static inline u32x4 avg(u32x4 a, u32x4 b)
    {
        auto axb = vec_xor(a.data, b.data);
        auto temp = vec_add(vec_and(a.data, b.data), vec_sr(axb, vec_splats(u32(1))));
        return temp;
    }

    static inline u32x4 avg_round(u32x4 a, u32x4 b)
    {
        return vec_avg(a, b);
    }

    // bitwise

    static inline u32x4 bitwise_nand(u32x4 a, u32x4 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline u32x4 bitwise_and(u32x4 a, u32x4 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline u32x4 bitwise_or(u32x4 a, u32x4 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline u32x4 bitwise_xor(u32x4 a, u32x4 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline u32x4 bitwise_not(u32x4 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask32x4 compare_eq(u32x4 a, u32x4 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask32x4 compare_gt(u32x4 a, u32x4 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask32x4 compare_neq(u32x4 a, u32x4 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask32x4 compare_lt(u32x4 a, u32x4 b)
    {
        return vec_cmplt(a.data, b.data);
    }

#if 0

    static inline mask32x4 compare_le(u32x4 a, u32x4 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask32x4 compare_ge(u32x4 a, u32x4 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask32x4 compare_le(u32x4 a, u32x4 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask32x4 compare_ge(u32x4 a, u32x4 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }
    
    #endif

    static inline u32x4 select(mask32x4 mask, u32x4 a, u32x4 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline u32x4 min(u32x4 a, u32x4 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline u32x4 max(u32x4 a, u32x4 b)
    {
        return vec_max(a.data, b.data);
    }

    // shift by constant

    template <int Count>
    static inline u32x4 slli(u32x4 a)
    {
        return vec_sl(a.data, vec_splats(u32(Count)));
    }

    template <int Count>
    static inline u32x4 srli(u32x4 a)
    {
        return vec_sr(a.data, vec_splats(u32(Count)));
    }

    template <int Count>
    static inline u32x4 srai(u32x4 a)
    {
        return vec_sra(a.data, vec_splats(u32(Count)));
    }

    // shift by scalar

    static inline u32x4 sll(u32x4 a, int count)
    {
        return vec_sl(a.data, vec_splats(u32(count)));
    }

    static inline u32x4 srl(u32x4 a, int count)
    {
        return vec_sr(a.data, vec_splats(u32(count)));
    }

    static inline u32x4 sra(u32x4 a, int count)
    {
        return vec_sra(a.data, vec_splats(u32(count)));
    }

    // shift by vector

    static inline u32x4 sll(u32x4 a, u32x4 count)
    {
        return vec_sl(a.data, count.data);
    }

    static inline u32x4 srl(u32x4 a, u32x4 count)
    {
        return vec_sr(a.data, count.data);
    }

    static inline u32x4 sra(u32x4 a, u32x4 count)
    {
        return vec_sra(a.data, count.data);
    }

    // -----------------------------------------------------------------
    // u64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u64x2 set_component(u64x2 a, u64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline u64 get_component(u64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline u64x2 u64x2_zero()
    {
        return vec_splats(u64(0));
    }

    static inline u64x2 u64x2_set(u64 s)
    {
        return vec_splats(s);
    }

    static inline u64x2 u64x2_set(u64 x, u64 y)
    {
        return (u64x2::vector) { x, y };
    }

    static inline u64x2 u64x2_uload(const u64* source)
    {
        return vec_xl(0, source);
    }

    static inline void u64x2_ustore(u64* dest, u64x2 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline u64x2 unpacklo(u64x2 a, u64x2 b)
    {
        return vec_insert(vec_extract(b.data, 0), a.data, 1);
    }

    static inline u64x2 unpackhi(u64x2 a, u64x2 b)
    {
        return vec_insert(vec_extract(a.data, 1), b.data, 0);
    }

    static inline u64x2 add(u64x2 a, u64x2 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline u64x2 sub(u64x2 a, u64x2 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline u64x2 avg(u64x2 a, u64x2 b)
    {
        auto axb = vec_xor(a.data, b.data);
        auto temp = vec_add(vec_and(a.data, b.data), vec_sr(axb, vec_splats(u64(1))));
        return temp;
    }

    static inline u64x2 avg_round(u64x2 a, u64x2 b)
    {
        return vec_avg(a, b);
    }

    // bitwise

    static inline u64x2 bitwise_nand(u64x2 a, u64x2 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline u64x2 bitwise_and(u64x2 a, u64x2 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline u64x2 bitwise_or(u64x2 a, u64x2 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline u64x2 bitwise_xor(u64x2 a, u64x2 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline u64x2 bitwise_not(u64x2 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask64x2 compare_eq(u64x2 a, u64x2 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask64x2 compare_gt(u64x2 a, u64x2 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask64x2 compare_neq(u64x2 a, u64x2 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask64x2 compare_lt(u64x2 a, u64x2 b)
    {
        return vec_cmplt(a.data, b.data);
    }

#if 0

    static inline mask64x2 compare_le(u64x2 a, u64x2 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask64x2 compare_ge(u64x2 a, u64x2 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask64x2 compare_le(u64x2 a, u64x2 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask64x2 compare_ge(u64x2 a, u64x2 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }
    
#endif

    static inline u64x2 select(mask64x2 mask, u64x2 a, u64x2 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline u64x2 min(u64x2 a, u64x2 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline u64x2 max(u64x2 a, u64x2 b)
    {
        return vec_max(a.data, b.data);
    }

    // shift by constant

    template <int Count>
    static inline u64x2 slli(u64x2 a)
    {
        a = vec_insert(vec_extract(a.data, 0) << Count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) << Count, a.data, 1);
        return a;
    }

    template <int Count>
    static inline u64x2 srli(u64x2 a)
    {
        a = vec_insert(vec_extract(a.data, 0) >> Count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) >> Count, a.data, 1);
        return a;
    }

    // shift by scalar

    static inline u64x2 sll(u64x2 a, int count)
    {
        a = vec_insert(vec_extract(a.data, 0) << count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) << count, a.data, 1);
        return a;
    }

    static inline u64x2 srl(u64x2 a, int count)
    {
        a = vec_insert(vec_extract(a.data, 0) >> count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) >> count, a.data, 1);
        return a;
    }

    // -----------------------------------------------------------------
    // s8x16
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s8x16 set_component(s8x16 a, s8 s)
    {
        static_assert(Index < 16, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline s8 get_component(s8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline s8x16 s8x16_zero()
    {
        return vec_splats(s8(0));
    }

    static inline s8x16 s8x16_set(s8 s)
    {
        return vec_splats(s);
    }

    static inline s8x16 s8x16_set(
        s8 v0, s8 v1, s8 v2, s8 v3, s8 v4, s8 v5, s8 v6, s8 v7,
        s8 v8, s8 v9, s8 v10, s8 v11, s8 v12, s8 v13, s8 v14, s8 v15)
    {
        return (s8x16::vector) { v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15 };
    }

    static inline s8x16 s8x16_uload(const s8* source)
    {
        return vec_xl(0, source);
    }

    static inline void s8x16_ustore(s8* dest, s8x16 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline s8x16 s8x16_load_low(const s8* source)
    {
        auto s0 = source[0];
        auto s1 = source[1];
        auto s2 = source[2];
        auto s3 = source[3];
        auto s4 = source[4];
        auto s5 = source[5];
        auto s6 = source[6];
        auto s7 = source[7];
        return (s8x16::vector) { s0, s1, s2, s3, s4, s5, s6, s7, 0, 0, 0, 0, 0, 0, 0, 0 };
    }

    static inline void s8x16_store_low(s8* dest, s8x16 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline s8x16 unpacklo(s8x16 a, s8x16 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline s8x16 unpackhi(s8x16 a, s8x16 b)
    {
        return vec_mergel(a.data, b.data);
    }

    static inline s8x16 add(s8x16 a, s8x16 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline s8x16 sub(s8x16 a, s8x16 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline s8x16 adds(s8x16 a, s8x16 b)
    {
        return vec_adds(a.data, b.data);
    }

    static inline s8x16 subs(s8x16 a, s8x16 b)
    {
        return vec_subs(a.data, b.data);
    }

    static inline s8x16 avg(s8x16 sa, s8x16 sb)
    {
        u8x16::vector sign = vec_splats(u8(0x80));
        u8x16::vector a = vec_xor(u8x16::vector(sa.data), sign);
        u8x16::vector b = vec_xor(u8x16::vector(sb.data), sign);

        // unsigned average
        u8x16::vector axb = vec_xor(a, b);
        u8x16::vector temp = vec_add(vec_and(a, b), vec_sr(axb, vec_splats(u8(1))));

        temp = vec_xor(temp, sign);
        return s8x16::vector(temp);
    }

    static inline s8x16 avg_round(s8x16 a, s8x16 b)
    {
        return vec_avg(a, b);
    }

    static inline s8x16 abs(s8x16 a)
    {
        return vec_abs(a.data);
    }

    static inline s8x16 neg(s8x16 a)
    {
        return vec_sub(vec_xor(a.data, a.data), a.data);
    }

    // bitwise

    static inline s8x16 bitwise_nand(s8x16 a, s8x16 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline s8x16 bitwise_and(s8x16 a, s8x16 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline s8x16 bitwise_or(s8x16 a, s8x16 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline s8x16 bitwise_xor(s8x16 a, s8x16 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline s8x16 bitwise_not(s8x16 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask8x16 compare_eq(s8x16 a, s8x16 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask8x16 compare_gt(s8x16 a, s8x16 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask8x16 compare_neq(s8x16 a, s8x16 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask8x16 compare_lt(s8x16 a, s8x16 b)
    {
        return vec_cmplt(a.data, b.data);
    }

#if 0

    static inline mask8x16 compare_le(s8x16 a, s8x16 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask8x16 compare_ge(s8x16 a, s8x16 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask8x16 compare_le(s8x16 a, s8x16 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask8x16 compare_ge(s8x16 a, s8x16 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }

#endif

    static inline s8x16 select(mask8x16 mask, s8x16 a, s8x16 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline s8x16 min(s8x16 a, s8x16 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline s8x16 max(s8x16 a, s8x16 b)
    {
        return vec_max(a.data, b.data);
    }

    // -----------------------------------------------------------------
    // s16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s16x8 set_component(s16x8 a, s16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline s16 get_component(s16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline s16x8 s16x8_zero()
    {
        return vec_splats(s16(0));
    }

    static inline s16x8 s16x8_set(s16 s)
    {
        return vec_splats(s);
    }

    static inline s16x8 s16x8_set(s16 s0, s16 s1, s16 s2, s16 s3, s16 s4, s16 s5, s16 s6, s16 s7)
    {
        return (s16x8::vector) { s0, s1, s2, s3, s4, s5, s6, s7 };
    }

    static inline s16x8 s16x8_uload(const s16* source)
    {
        return vec_xl(0, source);
    }

    static inline void s16x8_ustore(s16* dest, s16x8 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline s16x8 s16x8_load_low(const s16* source)
    {
        auto s0 = source[0];
        auto s1 = source[1];
        auto s2 = source[2];
        auto s3 = source[3];
        return (s16x8::vector) { s0, s1, s2, s3, 0, 0, 0, 0 };
    }

    static inline void s16x8_store_low(s16* dest, s16x8 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline s16x8 unpacklo(s16x8 a, s16x8 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline s16x8 unpackhi(s16x8 a, s16x8 b)
    {
        return vec_mergel(a.data, b.data);
    }

    static inline s16x8 add(s16x8 a, s16x8 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline s16x8 sub(s16x8 a, s16x8 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline s16x8 adds(s16x8 a, s16x8 b)
    {
        return vec_adds(a.data, b.data);
    }

    static inline s16x8 subs(s16x8 a, s16x8 b)
    {
        return vec_subs(a.data, b.data);
    }

    static inline s16x8 hadd(s16x8 a, s16x8 b)
    {
        s16x8 temp_a = unpacklo(a, b);
        s16x8 temp_b = unpackhi(a, b);
        a = unpacklo(temp_a, temp_b);
        b = unpackhi(temp_a, temp_b);
        temp_a = unpacklo(a, b);
        temp_b = unpackhi(a, b);
        return add(temp_a, temp_b);
    }

    static inline s16x8 hsub(s16x8 a, s16x8 b)
    {
        s16x8 temp_a = unpacklo(a, b);
        s16x8 temp_b = unpackhi(a, b);
        a = unpacklo(temp_a, temp_b);
        b = unpackhi(temp_a, temp_b);
        temp_a = unpacklo(a, b);
        temp_b = unpackhi(a, b);
        return sub(temp_a, temp_b);
    }

    static inline s16x8 hadds(s16x8 a, s16x8 b)
    {
        s16x8 temp_a = unpacklo(a, b);
        s16x8 temp_b = unpackhi(a, b);
        a = unpacklo(temp_a, temp_b);
        b = unpackhi(temp_a, temp_b);
        temp_a = unpacklo(a, b);
        temp_b = unpackhi(a, b);
        return adds(temp_a, temp_b);
    }

    static inline s16x8 hsubs(s16x8 a, s16x8 b)
    {
        s16x8 temp_a = unpacklo(a, b);
        s16x8 temp_b = unpackhi(a, b);
        a = unpacklo(temp_a, temp_b);
        b = unpackhi(temp_a, temp_b);
        temp_a = unpacklo(a, b);
        temp_b = unpackhi(a, b);
        return subs(temp_a, temp_b);
    }

    static inline s16x8 avg(s16x8 sa, s16x8 sb)
    {
        u16x8::vector sign = vec_splats(u16(0x8000));
        u16x8::vector a = vec_xor(u16x8::vector(sa.data), sign);
        u16x8::vector b = vec_xor(u16x8::vector(sb.data), sign);

        // unsigned average
        u16x8::vector axb = vec_xor(a, b);
        u16x8::vector temp = vec_add(vec_and(a, b), vec_sr(axb, vec_splats(u16(1))));

        temp = vec_xor(temp, sign);
        return s16x8::vector(temp);
    }

    static inline s16x8 avg_round(s16x8 a, s16x8 b)
    {
        return vec_avg(a, b);
    }

    static inline s16x8 mullo(s16x8 a, s16x8 b)
    {
        return vec_mladd(a.data, b.data, vec_xor(a.data, a.data));
    }

    static inline s32x4 madd(s16x8 a, s16x8 b)
    {
        s16 a[8];
        std::memcpy(a, &va, 16);

        s16 b[8];
        std::memcpy(b, &vb, 16);

        s32 x = s32(a[0]) * s32(b[0]) + s32(a[1]) * s32(b[1]);
        s32 y = s32(a[2]) * s32(b[2]) + s32(a[3]) * s32(b[3]);
        s32 z = s32(a[4]) * s32(b[4]) + s32(a[5]) * s32(b[5]);
        s32 w = s32(a[6]) * s32(b[6]) + s32(a[7]) * s32(b[7]);

        return (s32x4::vector) { x, y, z, w };
    }

    static inline s16x8 abs(s16x8 a)
    {
        return vec_abs(a.data);
    }

    static inline s16x8 neg(s16x8 a)
    {
        return vec_sub(vec_xor(a.data, a.data), a.data);
    }

    // bitwise

    static inline s16x8 bitwise_nand(s16x8 a, s16x8 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline s16x8 bitwise_and(s16x8 a, s16x8 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline s16x8 bitwise_or(s16x8 a, s16x8 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline s16x8 bitwise_xor(s16x8 a, s16x8 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline s16x8 bitwise_not(s16x8 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask16x8 compare_eq(s16x8 a, s16x8 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask16x8 compare_gt(s16x8 a, s16x8 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask16x8 compare_neq(s16x8 a, s16x8 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask16x8 compare_lt(s16x8 a, s16x8 b)
    {
        return vec_cmplt(a.data, b.data);
    }

#if 0

    static inline mask16x8 compare_le(s16x8 a, s16x8 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask16x8 compare_ge(s16x8 a, s16x8 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask16x8 compare_le(s16x8 a, s16x8 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask16x8 compare_ge(s16x8 a, s16x8 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }

#endif

    static inline s16x8 select(mask16x8 mask, s16x8 a, s16x8 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline s16x8 min(s16x8 a, s16x8 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline s16x8 max(s16x8 a, s16x8 b)
    {
        return vec_max(a.data, b.data);
    }

    // shift by constant

    template <int Count>
    static inline s16x8 slli(s16x8 a)
    {
        return vec_sl(a.data, vec_splats(u16(Count)));
    }

    template <int Count>
    static inline s16x8 srli(s16x8 a)
    {
        return vec_sr(a.data, vec_splats(u16(Count)));
    }

    template <int Count>
    static inline s16x8 srai(s16x8 a)
    {
        return vec_sra(a.data, vec_splats(u16(Count)));
    }

    // shift by scalar

    static inline s16x8 sll(s16x8 a, int count)
    {
        return vec_sl(a.data, vec_splats(u16(count)));
    }

    static inline s16x8 srl(s16x8 a, int count)
    {
        return vec_sr(a.data, vec_splats(u16(count)));
    }

    static inline s16x8 sra(s16x8 a, int count)
    {
        return vec_sra(a.data, vec_splats(u16(count)));
    }

    // -----------------------------------------------------------------
    // s32x4
    // -----------------------------------------------------------------

    // shuffle

#define VEC_SH4(n, select) \
    (select * 16 + n * 4 + 0), \
    (select * 16 + n * 4 + 1), \
    (select * 16 + n * 4 + 2), \
    (select * 16 + n * 4 + 3)

    template <u32 x, u32 y, u32 z, u32 w>
    static inline s32x4 shuffle(s32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const u8x16::vector mask = { VEC_SH4(x, 0), VEC_SH4(y, 0), VEC_SH4(z, 0), VEC_SH4(w, 0) };
        return vec_perm(v.data, v.data, mask);
    }

#undef VEC_SH4

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
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline s32 get_component(s32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline s32x4 s32x4_zero()
    {
        return vec_splats(s32(0));
    }

    static inline s32x4 s32x4_set(s32 s)
    {
        return vec_splats(s);
    }

    static inline s32x4 s32x4_set(s32 x, s32 y, s32 z, s32 w)
    {
        return (s32x4::vector) { x, y, z, w };
    }

    static inline s32x4 s32x4_uload(const s32* source)
    {
        return vec_xl(0, source);
    }

    static inline void s32x4_ustore(s32* dest, s32x4 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline s32x4 s32x4_load_low(const s32* source)
    {
        auto s0 = source[0];
        auto s1 = source[1];
        return (s32x4::vector) { s0, s1, 0, 0 };
    }

    static inline void s32x4_store_low(s32* dest, s32x4 a)
    {
        dest[0] = vec_extract(a.data, 0);
        dest[1] = vec_extract(a.data, 1);
    }

    static inline s32x4 unpacklo(s32x4 a, s32x4 b)
    {
        return vec_mergeh(a.data, b.data);
    }

    static inline s32x4 unpackhi(s32x4 a, s32x4 b)
    {
        return vec_mergel(a.data, b.data);
    }

    static inline s32x4 abs(s32x4 a)
    {
        return vec_abs(a.data);
    }

    static inline s32x4 neg(s32x4 a)
    {
        return vec_sub(vec_xor(a.data, a.data), a.data);
    }

    static inline s32x4 add(s32x4 a, s32x4 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline s32x4 sub(s32x4 a, s32x4 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline s32x4 adds(s32x4 a, s32x4 b)
    {
        return vec_adds(a.data, b.data);
    }

    static inline s32x4 subs(s32x4 a, s32x4 b)
    {
        return vec_subs(a.data, b.data);
    }

    static inline s32x4 hadd(s32x4 a, s32x4 b)
    {
        s32x4 temp_a = unpacklo(a, b);
        s32x4 temp_b = unpackhi(a, b);
        a = unpacklo(temp_a, temp_b);
        b = unpackhi(temp_a, temp_b);
        return add(a, b);
    }

    static inline s32x4 hsub(s32x4 a, s32x4 b)
    {
        s32x4 temp_a = unpacklo(a, b);
        s32x4 temp_b = unpackhi(a, b);
        a = unpacklo(temp_a, temp_b);
        b = unpackhi(temp_a, temp_b);
        return sub(a, b);
    }

    static inline s32x4 avg(s32x4 sa, s32x4 sb)
    {
        u32x4::vector sign = vec_splats(u32(0x80000000));
        u32x4::vector a = vec_xor(u32x4::vector(sa.data), sign);
        u32x4::vector b = vec_xor(u32x4::vector(sb.data), sign);

        // unsigned average
        u32x4::vector axb = vec_xor(a, b);
        u32x4::vector temp = vec_add(vec_and(a, b), vec_sr(axb, vec_splats(u32(1))));

        temp = vec_xor(temp, sign);
        return s32x4::vector(temp);
    }

    static inline s32x4 avg_round(s32x4 a, s32x4 b)
    {
        return vec_avg(a, b);
    }

    static inline s32x4 mullo(s32x4 a, s32x4 b)
    {
        f32x4 af = vec_ctf(a.data, 0);
        f32x4 bf = vec_ctf(b.data, 0);
        return vec_cts(vec_mul(af.data, bf.data), 0);
    }

    // bitwise

    static inline s32x4 bitwise_nand(s32x4 a, s32x4 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline s32x4 bitwise_and(s32x4 a, s32x4 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline s32x4 bitwise_or(s32x4 a, s32x4 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline s32x4 bitwise_xor(s32x4 a, s32x4 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline s32x4 bitwise_not(s32x4 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask32x4 compare_eq(s32x4 a, s32x4 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask32x4 compare_gt(s32x4 a, s32x4 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask32x4 compare_neq(s32x4 a, s32x4 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask32x4 compare_lt(s32x4 a, s32x4 b)
    {
        return vec_cmplt(a.data, b.data);
    }

#if 0

    static inline mask32x4 compare_le(s32x4 a, s32x4 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask32x4 compare_ge(s32x4 a, s32x4 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask32x4 compare_le(s32x4 a, s32x4 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask32x4 compare_ge(s32x4 a, s32x4 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }

#endif

    static inline s32x4 select(mask32x4 mask, s32x4 a, s32x4 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline s32x4 min(s32x4 a, s32x4 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline s32x4 max(s32x4 a, s32x4 b)
    {
        return vec_max(a.data, b.data);
    }

    // shift by constant

    template <int Count>
    static inline s32x4 slli(s32x4 a)
    {
        return vec_sl(a.data, vec_splats(u32(Count)));
    }

    template <int Count>
    static inline s32x4 srli(s32x4 a)
    {
        return vec_sr(a.data, vec_splats(u32(Count)));
    }

    template <int Count>
    static inline s32x4 srai(s32x4 a)
    {
        return vec_sra(a.data, vec_splats(u32(Count)));
    }

    // shift by scalar

    static inline s32x4 sll(s32x4 a, int count)
    {
        return vec_sl(a.data, vec_splats(u32(count)));
    }

    static inline s32x4 srl(s32x4 a, int count)
    {
        return vec_sr(a.data, vec_splats(u32(count)));
    }

    static inline s32x4 sra(s32x4 a, int count)
    {
        return vec_sra(a.data, vec_splats(u32(count)));
    }

    // shift by vector

    static inline s32x4 sll(s32x4 a, u32x4 count)
    {
        return vec_sl(a.data, count.data);
    }

    static inline s32x4 srl(s32x4 a, u32x4 count)
    {
        return vec_sr(a.data, count.data);
    }

    static inline s32x4 sra(s32x4 a, u32x4 count)
    {
        return vec_sra(a.data, count.data);
    }

    static inline u32 pack(s32x4 s)
    {
        s32x4 v = vec_sl(s.data, (u32x4::vector) { 0, 8, 16, 24 });
        v = vec_or(vec_mergeh(v.data, v.data), vec_mergel(v.data, v.data));
        v = vec_or(vec_mergeh(v.data, v.data), vec_mergel(v.data, v.data));
        return vec_extract(v.data, 0);
    }

    static inline s32x4 unpack(u32 s)
    {
        u32x4 v = vec_splats(s);
        v = vec_and(v.data, (u32x4::vector) { 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000 });
        return (s32x4::vector) vec_sr(v.data, (u32x4::vector) { 0, 8, 16, 24 });
    }

    // -----------------------------------------------------------------
    // s64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s64x2 set_component(s64x2 a, s64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return vec_insert(s, a.data, Index);
    }

    template <unsigned int Index>
    static inline s64 get_component(s64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return vec_extract(a.data, Index);
    }

    static inline s64x2 s64x2_zero()
    {
        return vec_splats(s64(0));
    }

    static inline s64x2 s64x2_set(s64 s)
    {
        return vec_splats(s);
    }

    static inline s64x2 s64x2_set(s64 x, s64 y)
    {
        return (s64x2::vector) { x, y };
    }

    static inline s64x2 s64x2_uload(const s64* source)
    {
        return vec_xl(0, source);
    }

    static inline void s64x2_ustore(s64* dest, s64x2 a)
    {
        vec_xst(a.data, 0, dest);
    }

    static inline s64x2 unpacklo(s64x2 a, s64x2 b)
    {
        return vec_insert(vec_extract(b.data, 0), a.data, 1);
    }

    static inline s64x2 unpackhi(s64x2 a, s64x2 b)
    {
        return vec_insert(vec_extract(a.data, 1), b.data, 0);
    }

    static inline s64x2 add(s64x2 a, s64x2 b)
    {
        return vec_add(a.data, b.data);
    }

    static inline s64x2 sub(s64x2 a, s64x2 b)
    {
        return vec_sub(a.data, b.data);
    }

    static inline s64x2 avg(s64x2 sa, s64x2 sb)
    {
        u64x2::vector sign = vec_splats(u64(0x8000000000000000ull));
        u64x2::vector a = vec_xor(u64x2::vector(sa.data), sign);
        u64x2::vector b = vec_xor(u64x2::vector(sb.data), sign);

        // unsigned average
        u64x2::vector axb = vec_xor(a, b);
        u64x2::vector temp = vec_add(vec_and(a, b), vec_sr(axb, vec_splats(u64(1))));

        temp = vec_xor(temp, sign);
        return s64x2::vector(temp);
    }

    static inline s64x2 avg_round(s64x2 a, s64x2 b)
    {
        return vec_avg(a, b);
    }

    static inline s64x2 neg(s64x2 a)
    {
        return vec_sub(vec_xor(a.data, a.data), a.data);
    }

    // bitwise

    static inline s64x2 bitwise_nand(s64x2 a, s64x2 b)
    {
        return vec_andc(b.data, a.data);
    }

    static inline s64x2 bitwise_and(s64x2 a, s64x2 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline s64x2 bitwise_or(s64x2 a, s64x2 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline s64x2 bitwise_xor(s64x2 a, s64x2 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline s64x2 bitwise_not(s64x2 a)
    {
        return vec_nor(a.data, a.data);
    }

    // compare

    static inline mask64x2 compare_eq(s64x2 a, s64x2 b)
    {
        return vec_cmpeq(a.data, b.data);
    }

    static inline mask64x2 compare_gt(s64x2 a, s64x2 b)
    {
        return vec_cmpgt(a.data, b.data);
    }

    static inline mask64x2 compare_neq(s64x2 a, s64x2 b)
    {
        auto mask = vec_cmpeq(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask64x2 compare_lt(s64x2 a, s64x2 b)
    {
        return vec_cmplt(a.data, b.data);
    }

#if 0

    static inline mask64x2 compare_le(s64x2 a, s64x2 b)
    {
        return vec_cmple(a.data, b.data);
    }

    static inline mask64x2 compare_ge(s64x2 a, s64x2 b)
    {
        return vec_cmpge(a.data, b.data);
    }

#else

    static inline mask64x2 compare_le(s64x2 a, s64x2 b)
    {
        auto mask = vec_cmpgt(a.data, b.data);
        return vec_nor(mask, mask);
    }

    static inline mask64x2 compare_ge(s64x2 a, s64x2 b)
    {
        auto mask = vec_cmplt(a.data, b.data);
        return vec_nor(mask, mask);
    }

#endif

    static inline s64x2 select(mask64x2 mask, s64x2 a, s64x2 b)
    {
        return vec_sel(b.data, a.data, mask.data);
    }

    static inline s64x2 min(s64x2 a, s64x2 b)
    {
        return vec_min(a.data, b.data);
    }

    static inline s64x2 max(s64x2 a, s64x2 b)
    {
        return vec_max(a.data, b.data);
    }

    // shift by constant

    template <int Count>
    static inline s64x2 slli(s64x2 a)
    {
        a = vec_insert(vec_extract(a.data, 0) << Count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) << Count, a.data, 1);
        return a;
    }

    template <int Count>
    static inline s64x2 srli(s64x2 a)
    {
        a = vec_insert(vec_extract(a.data, 0) >> Count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) >> Count, a.data, 1);
        return a;
    }

    // shift by scalar

    static inline s64x2 sll(s64x2 a, int count)
    {
        a = vec_insert(vec_extract(a.data, 0) << count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) << count, a.data, 1);
        return a;
    }

    static inline s64x2 srl(s64x2 a, int count)
    {
        a = vec_insert(vec_extract(a.data, 0) >> count, a.data, 0);
        a = vec_insert(vec_extract(a.data, 1) >> count, a.data, 1);
        return a;
    }

    // -----------------------------------------------------------------
    // mask8x16
    // -----------------------------------------------------------------

    static inline mask8x16 operator & (mask8x16 a, mask8x16 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline mask8x16 operator | (mask8x16 a, mask8x16 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline mask8x16 operator ^ (mask8x16 a, mask8x16 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline mask8x16 operator ! (mask8x16 a)
    {
        return vec_nor(a.data, a.data);
    }

    static inline u32 get_mask(mask8x16 a)
    {
        const u32x4::vector zero = u32x4_zero();
        u8x16::vector masked = vec_and(a.data, (u8x16::vector) { 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128 });
        s32x4::vector sum = (s32x4::vector) vec_sl(vec_sum4s(masked, zero), (u32x4::vector) { 0, 0, 8, 8 });
        return vec_extract(vec_sums(sum, vec_xor(sum, sum)), 3);
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

    static inline mask16x8 operator & (mask16x8 a, mask16x8 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline mask16x8 operator | (mask16x8 a, mask16x8 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline mask16x8 operator ^ (mask16x8 a, mask16x8 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline mask16x8 operator ! (mask16x8 a)
    {
        return vec_nor(a.data, a.data);
    }

    static inline u32 get_mask(mask16x8 a)
    {
        const s32x4::vector zero = s32x4_zero();
        s16x8::vector masked = (s16x8::vector) vec_and(a.data, (s16x8::vector) { 1, 2, 4, 8, 16, 32, 64, 128 });
        s32x4::vector sum = vec_sum4s(masked, zero);
        return vec_extract(vec_sums(sum, zero), 3);
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

    static inline mask32x4 operator & (mask32x4 a, mask32x4 b)
    {
        return vec_and(a.data, b.data);
    }

    static inline mask32x4 operator | (mask32x4 a, mask32x4 b)
    {
        return vec_or(a.data, b.data);
    }

    static inline mask32x4 operator ^ (mask32x4 a, mask32x4 b)
    {
        return vec_xor(a.data, b.data);
    }

    static inline mask32x4 operator ! (mask32x4 a)
    {
        return vec_nor(a.data, a.data);
    }

    static inline u32 get_mask(mask32x4 a)
    {
        const s32x4::vector zero = s32x4_zero();
        s32x4::vector masked = (s32x4::vector) vec_and(a.data, (s32x4::vector) { 1, 2, 4, 8 });
        return vec_extract(vec_sums(masked, zero), 3);
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

    static inline mask64x2 operator & (mask64x2 a, mask64x2 b)
    {
        return (mask64x2::vector) vec_and((u64x2::vector)a.data, (u64x2::vector)b.data);
    }

    static inline mask64x2 operator | (mask64x2 a, mask64x2 b)
    {
        return (mask64x2::vector) vec_or((u64x2::vector)a.data, (u64x2::vector)b.data);
    }

    static inline mask64x2 operator ^ (mask64x2 a, mask64x2 b)
    {
        return (mask64x2::vector) vec_xor((u64x2::vector)a.data, (u64x2::vector)b.data);
    }

    static inline mask64x2 operator ! (mask64x2 a)
    {
        return vec_nor(a.data, a.data);
    }

    static inline u32 get_mask(mask64x2 a)
    {
        u64x2 temp = (u64x2::vector) a.data;
        u32 x = u32(get_component<0>(temp)) & 1;
        u32 y = u32(get_component<1>(temp)) & 2;
        return x | y;
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

    // -----------------------------------------------------------------
    // masked functions
    // -----------------------------------------------------------------

#define SIMD_ZEROMASK_INT128
#define SIMD_MASK_INT128
#include <mango/simd/common_mask.hpp>
#undef SIMD_ZEROMASK_INT128
#undef SIMD_MASK_INT128

} // namespace mango::simd
