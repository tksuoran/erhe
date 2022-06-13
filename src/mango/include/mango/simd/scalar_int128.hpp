/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/scalar_detail.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // u8x16
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u8x16 set_component(u8x16 a, u8 s)
    {
        static_assert(Index < 16, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline u8 get_component(u8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return a[Index];
    }

    static inline u8x16 u8x16_zero()
    {
        return detail::scalar_set<u8, 16>(0);
    }

    static inline u8x16 u8x16_set(u8 s)
    {
        return detail::scalar_set<u8, 16>(s);
    }

    static inline u8x16 u8x16_set(
        u8 s0, u8 s1, u8 s2, u8 s3, u8 s4, u8 s5, u8 s6, u8 s7,
        u8 s8, u8 s9, u8 s10, u8 s11, u8 s12, u8 s13, u8 s14, u8 s15)
    {
        return {{ s0, s1, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15 }};
    }

    static inline u8x16 u8x16_uload(const u8* s)
    {
        return u8x16_set(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
    }

    static inline void u8x16_ustore(u8* dest, u8x16 a)
    {
        for (int i = 0; i < 16; ++i)
        {
            dest[i] = a[i];
        }
    }

    static inline u8x16 u8x16_load_low(const u8* s)
    {
        return u8x16_set(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], 0, 0, 0, 0, 0, 0, 0, 0);
    }

    static inline void u8x16_store_low(u8* dest, u8x16 a)
    {
        for (int i = 0; i < 8; ++i)
        {
            dest[i] = a[i];
        }
    }

    static inline u8x16 unpacklo(u8x16 a, u8x16 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline u8x16 unpackhi(u8x16 a, u8x16 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline u8x16 add(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline u8x16 sub(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline u8x16 adds(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_adds, a, b);
    }

    static inline u8x16 subs(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_subs, a, b);
    }

    static inline u8x16 avg(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_average, a, b);
    }

    static inline u8x16 avg_round(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_rounded_average, a, b);
    }

    // bitwise

    static inline u8x16 bitwise_nand(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline u8x16 bitwise_and(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline u8x16 bitwise_or(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline u8x16 bitwise_xor(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline u8x16 bitwise_not(u8x16 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask8x16 compare_eq(u8x16 a, u8x16 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask8x16 compare_gt(u8x16 a, u8x16 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask8x16 compare_neq(u8x16 a, u8x16 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask8x16 compare_lt(u8x16 a, u8x16 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask8x16 compare_le(u8x16 a, u8x16 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask8x16 compare_ge(u8x16 a, u8x16 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline u8x16 select(mask8x16 mask, u8x16 a, u8x16 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline u8x16 min(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline u8x16 max(u8x16 a, u8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // -----------------------------------------------------------------
    // u16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u16x8 set_component(u16x8 a, u16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline u16 get_component(u16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return a[Index];
    }

    static inline u16x8 u16x8_zero()
    {
        return detail::scalar_set<u16, 8>(0);
    }

    static inline u16x8 u16x8_set(u16 s)
    {
        return detail::scalar_set<u16, 8>(s);
    }

    static inline u16x8 u16x8_set(u16 s0, u16 s1, u16 s2, u16 s3, u16 s4, u16 s5, u16 s6, u16 s7)
    {
        return {{ s0, s1, s2, s3, s4, s5, s6, s7 }};
    }

    static inline u16x8 u16x8_uload(const u16* s)
    {
        return u16x8_set(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    }

    static inline void u16x8_ustore(u16* dest, u16x8 a)
    {
        for (int i = 0; i < 8; ++i)
        {
            dest[i] = a[i];
        }
    }

    static inline u16x8 u16x8_load_low(const u16* s)
    {
        return u16x8_set(s[0], s[1], s[2], s[3], 0, 0, 0, 0);
    }

    static inline void u16x8_store_low(u16* dest, u16x8 a)
    {
        for (int i = 0; i < 4; ++i)
        {
            dest[i] = a[i];
        }
    }

    static inline u16x8 unpacklo(u16x8 a, u16x8 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline u16x8 unpackhi(u16x8 a, u16x8 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline u16x8 add(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline u16x8 sub(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline u16x8 adds(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_adds, a, b);
    }

    static inline u16x8 subs(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_subs, a, b);
    }

    static inline u16x8 avg(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_average, a, b);
    }

    static inline u16x8 avg_round(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_rounded_average, a, b);
    }

    static inline u16x8 mullo(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_mullo, a, b);
    }

    // bitwise

    static inline u16x8 bitwise_nand(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline u16x8 bitwise_and(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline u16x8 bitwise_or(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline u16x8 bitwise_xor(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline u16x8 bitwise_not(u16x8 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask16x8 compare_eq(u16x8 a, u16x8 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask16x8 compare_gt(u16x8 a, u16x8 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask16x8 compare_neq(u16x8 a, u16x8 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask16x8 compare_lt(u16x8 a, u16x8 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask16x8 compare_le(u16x8 a, u16x8 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask16x8 compare_ge(u16x8 a, u16x8 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline u16x8 select(mask16x8 mask, u16x8 a, u16x8 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline u16x8 min(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline u16x8 max(u16x8 a, u16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // shift by constant

    template <int Count>
    static inline u16x8 slli(u16x8 a)
    {
        return detail::scalar_shift_left<u16x8, u16, Count>(a);
    }

    template <int Count>
    static inline u16x8 srli(u16x8 a)
    {
        return detail::scalar_shift_right<u16x8, u16, Count>(a);
    }

    template <int Count>
    static inline u16x8 srai(u16x8 a)
    {
        return detail::scalar_shift_right<u16x8, s16, Count>(a);
    }

    // shift by scalar

    static inline u16x8 sll(u16x8 a, int count)
    {
        return detail::scalar_shift_left<u16x8, u16>(a, count);
    }

    static inline u16x8 srl(u16x8 a, int count)
    {
        return detail::scalar_shift_right<u16x8, u16>(a, count);
    }

    static inline u16x8 sra(u16x8 a, int count)
    {
        return detail::scalar_shift_right<u16x8, s16>(a, count);
    }
    
    // -----------------------------------------------------------------
    // u32x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline u32x4 shuffle(u32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        return {{ v[x], v[y], v[z], v[w] }};
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
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline u32 get_component(u32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return a[Index];
    }

    static inline u32x4 u32x4_zero()
    {
        return {{ 0, 0, 0, 0 }};
    }

    static inline u32x4 u32x4_set(u32 s)
    {
        return {{ s, s, s, s }};
    }

    static inline u32x4 u32x4_set(u32 x, u32 y, u32 z, u32 w)
    {
        return {{ x, y, z, w }};
    }

    static inline u32x4 u32x4_uload(const u32* s)
    {
        return u32x4_set(s[0], s[1], s[2], s[3]);
    }

    static inline void u32x4_ustore(u32* dest, u32x4 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
        dest[2] = a[2];
        dest[3] = a[3];
    }

    static inline u32x4 u32x4_load_low(const u32* s)
    {
        return u32x4_set(s[0], s[1], 0, 0);
    }

    static inline void u32x4_store_low(u32* dest, u32x4 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline u32x4 unpacklo(u32x4 a, u32x4 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline u32x4 unpackhi(u32x4 a, u32x4 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline u32x4 add(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline u32x4 sub(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline u32x4 adds(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_adds, a, b);
    }

    static inline u32x4 subs(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_subs, a, b);
    }

    static inline u32x4 avg(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_average, a, b);
    }

    static inline u32x4 avg_round(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_rounded_average, a, b);
    }

    static inline u32x4 mullo(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_mullo, a, b);
    }

    // bitwise

    static inline u32x4 bitwise_nand(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline u32x4 bitwise_and(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline u32x4 bitwise_or(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline u32x4 bitwise_xor(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline u32x4 bitwise_not(u32x4 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask32x4 compare_eq(u32x4 a, u32x4 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask32x4 compare_gt(u32x4 a, u32x4 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask32x4 compare_neq(u32x4 a, u32x4 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask32x4 compare_lt(u32x4 a, u32x4 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask32x4 compare_le(u32x4 a, u32x4 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask32x4 compare_ge(u32x4 a, u32x4 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline u32x4 select(mask32x4 mask, u32x4 a, u32x4 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline u32x4 min(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline u32x4 max(u32x4 a, u32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // shift by constant

    template <int Count>
    static inline u32x4 slli(u32x4 a)
    {
        return detail::scalar_shift_left<u32x4, u32, Count>(a);
    }

    template <int Count>
    static inline u32x4 srli(u32x4 a)
    {
        return detail::scalar_shift_right<u32x4, u32, Count>(a);
    }

    template <int Count>
    static inline u32x4 srai(u32x4 a)
    {
        return detail::scalar_shift_right<u32x4, s32, Count>(a);
    }

    // shift by scalar

    static inline u32x4 sll(u32x4 a, int count)
    {
        return detail::scalar_shift_left<u32x4, u32>(a, count);
    }

    static inline u32x4 srl(u32x4 a, int count)
    {
        return detail::scalar_shift_right<u32x4, u32>(a, count);
    }

    static inline u32x4 sra(u32x4 a, int count)
    {
        return detail::scalar_shift_right<u32x4, s32>(a, count);
    }

    // shift by vector

    static inline u32x4 sll(u32x4 a, u32x4 count)
    {
        return detail::scalar_shift_left<u32x4, u32, u32x4>(a, count);
    }

    static inline u32x4 srl(u32x4 a, u32x4 count)
    {
        return detail::scalar_shift_right<u32x4, u32, u32x4>(a, count);
    }

    static inline u32x4 sra(u32x4 a, u32x4 count)
    {
        return detail::scalar_shift_right<u32x4, s32, u32x4>(a, count);
    }

    // -----------------------------------------------------------------
    // u64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u64x2 set_component(u64x2 a, u64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline u64 get_component(u64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return a[Index];
    }

    static inline u64x2 u64x2_zero()
    {
        return detail::scalar_set<u64, 2>(0);
    }

    static inline u64x2 u64x2_set(u64 s)
    {
        return detail::scalar_set<u64, 2>(s);
    }

    static inline u64x2 u64x2_set(u64 x, u64 y)
    {
        return {{ x, y }};
    }

    static inline u64x2 u64x2_uload(const u64* s)
    {
        return u64x2_set(s[0], s[1]);
    }

    static inline void u64x2_ustore(u64* dest, u64x2 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline u64x2 unpacklo(u64x2 a, u64x2 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline u64x2 unpackhi(u64x2 a, u64x2 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline u64x2 add(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline u64x2 sub(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline u64x2 avg(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_average, a, b);
    }

    static inline u64x2 avg_round(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_unsigned_rounded_average, a, b);
    }

    // bitwise

    static inline u64x2 bitwise_nand(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline u64x2 bitwise_and(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline u64x2 bitwise_or(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline u64x2 bitwise_xor(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline u64x2 bitwise_not(u64x2 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask64x2 compare_eq(u64x2 a, u64x2 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask64x2 compare_gt(u64x2 a, u64x2 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask64x2 compare_neq(u64x2 a, u64x2 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask64x2 compare_lt(u64x2 a, u64x2 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask64x2 compare_le(u64x2 a, u64x2 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask64x2 compare_ge(u64x2 a, u64x2 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline u64x2 select(mask64x2 mask, u64x2 a, u64x2 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline u64x2 min(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline u64x2 max(u64x2 a, u64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // shift by constant

    template <int Count>
    static inline u64x2 slli(u64x2 a)
    {
        return detail::scalar_shift_left<u64x2, u64, Count>(a);
    }

    template <int Count>
    static inline u64x2 srli(u64x2 a)
    {
        return detail::scalar_shift_right<u64x2, u64, Count>(a);
    }

    // shift by scalar

    static inline u64x2 sll(u64x2 a, int count)
    {
        return detail::scalar_shift_left<u64x2, u64>(a, count);
    }

    static inline u64x2 srl(u64x2 a, int count)
    {
        return detail::scalar_shift_right<u64x2, u64>(a, count);
    }

    // -----------------------------------------------------------------
    // s8x16
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s8x16 set_component(s8x16 a, s8 s)
    {
        static_assert(Index < 16, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline s8 get_component(s8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return a[Index];
    }

    static inline s8x16 s8x16_zero()
    {
        return detail::scalar_set<s8, 16>(0);
    }

    static inline s8x16 s8x16_set(s8 s)
    {
        return detail::scalar_set<s8, 16>(s);
    }

    static inline s8x16 s8x16_set(
        s8 v0, s8 v1, s8 v2, s8 v3, s8 v4, s8 v5, s8 v6, s8 v7,
        s8 v8, s8 v9, s8 v10, s8 v11, s8 v12, s8 v13, s8 v14, s8 v15)
    {
        return {{ v0, v1, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15 }};
    }

    static inline s8x16 s8x16_uload(const s8* s)
    {
        return s8x16_set(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
    }

    static inline void s8x16_ustore(s8* dest, s8x16 a)
    {
        for (int i = 0; i < 16; ++i)
        {
            dest[i] = a[i];
        }
    }

    static inline s8x16 s8x16_load_low(const s8* s)
    {
        return s8x16_set(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], 0, 0, 0, 0, 0, 0, 0, 0);
    }

    static inline void s8x16_store_low(s8* dest, s8x16 a)
    {
        for (int i = 0; i < 8; ++i)
        {
            dest[i] = a[i];
        }
    }

    static inline s8x16 unpacklo(s8x16 a, s8x16 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline s8x16 unpackhi(s8x16 a, s8x16 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline s8x16 add(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline s8x16 sub(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline s8x16 adds(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_adds, a, b);
    }

    static inline s8x16 subs(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_subs, a, b);
    }

    static inline s8x16 avg(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_average, a, b);
    }

    static inline s8x16 avg_round(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_rounded_average, a, b);
    }

    static inline s8x16 abs(s8x16 a)
    {
        return detail::scalar_unroll(detail::scalar_abs, a);
    }

    static inline s8x16 neg(s8x16 a)
    {
        return detail::scalar_unroll(detail::scalar_neg, a);
    }

    // bitwise

    static inline s8x16 bitwise_nand(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline s8x16 bitwise_and(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline s8x16 bitwise_or(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline s8x16 bitwise_xor(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline s8x16 bitwise_not(s8x16 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask8x16 compare_eq(s8x16 a, s8x16 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask8x16 compare_gt(s8x16 a, s8x16 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask8x16 compare_neq(s8x16 a, s8x16 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask8x16 compare_lt(s8x16 a, s8x16 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask8x16 compare_le(s8x16 a, s8x16 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask8x16 compare_ge(s8x16 a, s8x16 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline s8x16 select(mask8x16 mask, s8x16 a, s8x16 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline s8x16 min(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline s8x16 max(s8x16 a, s8x16 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // -----------------------------------------------------------------
    // s16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s16x8 set_component(s16x8 a, s16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline s16 get_component(s16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return a[Index];
    }

    static inline s16x8 s16x8_zero()
    {
        return detail::scalar_set<s16, 8>(0);
    }

    static inline s16x8 s16x8_set(s16 s)
    {
        return detail::scalar_set<s16, 8>(s);
    }

    static inline s16x8 s16x8_set(s16 s0, s16 s1, s16 s2, s16 s3, s16 s4, s16 s5, s16 s6, s16 s7)
    {
        return {{ s0, s1, s2, s3, s4, s5, s6, s7 }};
    }

    static inline s16x8 s16x8_uload(const s16* s)
    {
        return s16x8_set(s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7]);
    }

    static inline void s16x8_ustore(s16* dest, s16x8 a)
    {
        for (int i = 0; i < 8; ++i)
        {
            dest[i] = a[i];
        }
    }

    static inline s16x8 s16x8_load_low(const s16* s)
    {
        return s16x8_set(s[0], s[1], s[2], s[3], 0, 0, 0, 0);
    }

    static inline void s16x8_store_low(s16* dest, s16x8 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
        dest[2] = a[2];
        dest[3] = a[3];
    }

    static inline s16x8 unpacklo(s16x8 a, s16x8 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline s16x8 unpackhi(s16x8 a, s16x8 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline s16x8 add(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline s16x8 sub(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline s16x8 adds(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_adds, a, b);
    }

    static inline s16x8 subs(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_subs, a, b);
    }

    static inline s16x8 hadd(s16x8 a, s16x8 b)
    {
        s16x8 dest;
        dest[0] = a[0] + a[1];
        dest[1] = a[2] + a[3];
        dest[2] = a[4] + a[5];
        dest[3] = a[6] + a[7];
        dest[4] = b[0] + b[1];
        dest[5] = b[2] + b[3];
        dest[6] = b[4] + b[5];
        dest[7] = b[6] + b[7];
        return dest;
    }

    static inline s16x8 hsub(s16x8 a, s16x8 b)
    {
        s16x8 dest;
        dest[0] = a[0] - a[1];
        dest[1] = a[2] - a[3];
        dest[2] = a[4] - a[5];
        dest[3] = a[6] - a[7];
        dest[4] = b[0] - b[1];
        dest[5] = b[2] - b[3];
        dest[6] = b[4] - b[5];
        dest[7] = b[6] - b[7];
        return dest;
    }

    static inline s16x8 hadds(s16x8 a, s16x8 b)
    {
        s16x8 dest;
        dest[0] = detail::scalar_signed_adds(a[0], a[1]);
        dest[1] = detail::scalar_signed_adds(a[2], a[3]);
        dest[2] = detail::scalar_signed_adds(a[4], a[5]);
        dest[3] = detail::scalar_signed_adds(a[6], a[7]);
        dest[4] = detail::scalar_signed_adds(b[0], b[1]);
        dest[5] = detail::scalar_signed_adds(b[2], b[3]);
        dest[6] = detail::scalar_signed_adds(b[4], b[5]);
        dest[7] = detail::scalar_signed_adds(b[6], b[7]);
        return dest;
    }

    static inline s16x8 hsubs(s16x8 a, s16x8 b)
    {
        s16x8 dest;
        dest[0] = detail::scalar_signed_subs(a[0], a[1]);
        dest[1] = detail::scalar_signed_subs(a[2], a[3]);
        dest[2] = detail::scalar_signed_subs(a[4], a[5]);
        dest[3] = detail::scalar_signed_subs(a[6], a[7]);
        dest[4] = detail::scalar_signed_subs(b[0], b[1]);
        dest[5] = detail::scalar_signed_subs(b[2], b[3]);
        dest[6] = detail::scalar_signed_subs(b[4], b[5]);
        dest[7] = detail::scalar_signed_subs(b[6], b[7]);
        return dest;
    }

    static inline s16x8 avg(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_average, a, b);
    }

    static inline s16x8 avg_round(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_rounded_average, a, b);
    }

    static inline s16x8 mullo(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_mullo, a, b);
    }

    static inline s32x4 madd(s16x8 a, s16x8 b)
    {
        s32x4 dest;
        dest[0] = s32(a[0]) * s32(b[0]) + s32(a[1]) * s32(b[1]);
        dest[1] = s32(a[2]) * s32(b[2]) + s32(a[3]) * s32(b[3]);
        dest[2] = s32(a[4]) * s32(b[4]) + s32(a[5]) * s32(b[5]);
        dest[3] = s32(a[6]) * s32(b[6]) + s32(a[7]) * s32(b[7]);
        return dest;
    }

    static inline s16x8 abs(s16x8 a)
    {
        return detail::scalar_unroll(detail::scalar_abs, a);
    }

    static inline s16x8 neg(s16x8 a)
    {
        return detail::scalar_unroll(detail::scalar_neg, a);
    }

    // bitwise

    static inline s16x8 bitwise_nand(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline s16x8 bitwise_and(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline s16x8 bitwise_or(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline s16x8 bitwise_xor(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline s16x8 bitwise_not(s16x8 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask16x8 compare_eq(s16x8 a, s16x8 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask16x8 compare_gt(s16x8 a, s16x8 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask16x8 compare_neq(s16x8 a, s16x8 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask16x8 compare_lt(s16x8 a, s16x8 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask16x8 compare_le(s16x8 a, s16x8 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask16x8 compare_ge(s16x8 a, s16x8 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline s16x8 select(mask16x8 mask, s16x8 a, s16x8 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline s16x8 min(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline s16x8 max(s16x8 a, s16x8 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // shift by constant

    template <int Count>
    static inline s16x8 slli(s16x8 a)
    {
        return detail::scalar_shift_left<s16x8, u16, Count>(a);
    }

    template <int Count>
    static inline s16x8 srli(s16x8 a)
    {
        return detail::scalar_shift_right<s16x8, u16, Count>(a);
    }

    template <int Count>
    static inline s16x8 srai(s16x8 a)
    {
        return detail::scalar_shift_right<s16x8, s16, Count>(a);
    }

    // shift by scalar

    static inline s16x8 sll(s16x8 a, int count)
    {
        return detail::scalar_shift_left<s16x8, u16>(a, count);
    }

    static inline s16x8 srl(s16x8 a, int count)
    {
        return detail::scalar_shift_right<s16x8, u16>(a, count);
    }

    static inline s16x8 sra(s16x8 a, int count)
    {
        return detail::scalar_shift_right<s16x8, s16>(a, count);
    }

    // -----------------------------------------------------------------
    // s32x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline s32x4 shuffle(s32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        return {{ v[x], v[y], v[z], v[w] }};
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
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline s32 get_component(s32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return a[Index];
    }

    static inline s32x4 s32x4_zero()
    {
        return {{ 0, 0, 0, 0 }};
    }

    static inline s32x4 s32x4_set(s32 s)
    {
        return {{ s, s, s, s }};
    }

    static inline s32x4 s32x4_set(s32 x, s32 y, s32 z, s32 w)
    {
        return {{ x, y, z, w }};
    }

    static inline s32x4 s32x4_uload(const s32* s)
    {
        return s32x4_set(s[0], s[1], s[2], s[3]);
    }

    static inline void s32x4_ustore(s32* dest, s32x4 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
        dest[2] = a[2];
        dest[3] = a[3];
    }

    static inline s32x4 s32x4_load_low(const s32* s)
    {
        return s32x4_set(s[0], s[1], 0, 0);
    }

    static inline void s32x4_store_low(s32* dest, s32x4 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline s32x4 unpacklo(s32x4 a, s32x4 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline s32x4 unpackhi(s32x4 a, s32x4 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline s32x4 abs(s32x4 a)
    {
        return detail::scalar_unroll(detail::scalar_abs, a);
    }

    static inline s32x4 neg(s32x4 a)
    {
        return detail::scalar_unroll(detail::scalar_neg, a);
    }

    static inline s32x4 add(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline s32x4 sub(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline s32x4 adds(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_adds, a, b);
    }

    static inline s32x4 subs(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_subs, a, b);
    }

    static inline s32x4 hadd(s32x4 a, s32x4 b)
    {
        s32x4 dest;
        dest[0] = a[0] + a[1];
        dest[1] = a[2] + a[3];
        dest[2] = b[0] + b[1];
        dest[3] = b[2] + b[3];
        return dest;
    }

    static inline s32x4 hsub(s32x4 a, s32x4 b)
    {
        s32x4 dest;
        dest[0] = a[0] - a[1];
        dest[1] = a[2] - a[3];
        dest[2] = b[0] - b[1];
        dest[3] = b[2] - b[3];
        return dest;
    }

    static inline s32x4 avg(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_average, a, b);
    }

    static inline s32x4 avg_round(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_rounded_average, a, b);
    }

    static inline s32x4 mullo(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_mullo, a, b);
    }

    // bitwise

    static inline s32x4 bitwise_nand(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline s32x4 bitwise_and(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline s32x4 bitwise_or(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline s32x4 bitwise_xor(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline s32x4 bitwise_not(s32x4 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask32x4 compare_eq(s32x4 a, s32x4 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask32x4 compare_gt(s32x4 a, s32x4 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask32x4 compare_neq(s32x4 a, s32x4 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask32x4 compare_lt(s32x4 a, s32x4 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask32x4 compare_le(s32x4 a, s32x4 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask32x4 compare_ge(s32x4 a, s32x4 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline s32x4 select(mask32x4 mask, s32x4 a, s32x4 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline s32x4 min(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline s32x4 max(s32x4 a, s32x4 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // shift by constant

    template <int Count>
    static inline s32x4 slli(s32x4 a)
    {
        return detail::scalar_shift_left<s32x4, u32, Count>(a);
    }

    template <int Count>
    static inline s32x4 srli(s32x4 a)
    {
        return detail::scalar_shift_right<s32x4, u32, Count>(a);
    }

    template <int Count>
    static inline s32x4 srai(s32x4 a)
    {
        return detail::scalar_shift_right<s32x4, s32, Count>(a);
    }

    // shift by scalar

    static inline s32x4 sll(s32x4 a, int count)
    {
        return detail::scalar_shift_left<s32x4, u32>(a, count);
    }

    static inline s32x4 srl(s32x4 a, int count)
    {
        return detail::scalar_shift_right<s32x4, u32>(a, count);
    }

    static inline s32x4 sra(s32x4 a, int count)
    {
        return detail::scalar_shift_right<s32x4, s32>(a, count);
    }

    // shift by vector

    static inline s32x4 sll(s32x4 a, u32x4 count)
    {
        return detail::scalar_shift_left<s32x4, u32, u32x4>(a, count);
    }

    static inline s32x4 srl(s32x4 a, u32x4 count)
    {
        return detail::scalar_shift_right<s32x4, u32, u32x4>(a, count);
    }

    static inline s32x4 sra(s32x4 a, u32x4 count)
    {
        return detail::scalar_shift_right<s32x4, s32, u32x4>(a, count);
    }

    static inline u32 pack(s32x4 s)
    {
        const u32 x = byteclamp(s[0]);
        const u32 y = byteclamp(s[1]);
        const u32 z = byteclamp(s[2]);
        const u32 w = byteclamp(s[3]);
        return x | (y << 8) | (z << 16) | (w << 24);
    }

    static inline s32x4 unpack(u32 s)
    {
        s32x4 v;
        v[0] = (s >> 0) & 0xff;
        v[1] = (s >> 8) & 0xff;
        v[2] = (s >> 16) & 0xff;
        v[3] = (s >> 24);
        return v;
    }

    // -----------------------------------------------------------------
    // s64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s64x2 set_component(s64x2 a, s64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline s64 get_component(s64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return a[Index];
    }

    static inline s64x2 s64x2_zero()
    {
        return detail::scalar_set<s64, 2>(0);
    }

    static inline s64x2 s64x2_set(s64 s)
    {
        return detail::scalar_set<s64, 2>(s);
    }

    static inline s64x2 s64x2_set(s64 x, s64 y)
    {
        return {{ x, y }};
    }

    static inline s64x2 s64x2_uload(const s64* s)
    {
        return s64x2_set(s[0], s[1]);
    }

    static inline void s64x2_ustore(s64* dest, s64x2 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline s64x2 unpacklo(s64x2 a, s64x2 b)
    {
        return detail::scalar_unpacklo(a, b);
    }

    static inline s64x2 unpackhi(s64x2 a, s64x2 b)
    {
        return detail::scalar_unpackhi(a, b);
    }

    static inline s64x2 add(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline s64x2 sub(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline s64x2 avg(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_average, a, b);
    }

    static inline s64x2 avg_round(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_signed_rounded_average, a, b);
    }

    // bitwise

    static inline s64x2 bitwise_nand(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline s64x2 bitwise_and(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline s64x2 bitwise_or(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline s64x2 bitwise_xor(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline s64x2 bitwise_not(s64x2 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    // compare

    static inline mask64x2 compare_eq(s64x2 a, s64x2 b)
    {
        return detail::scalar_compare_eq(a, b);
    }

    static inline mask64x2 compare_gt(s64x2 a, s64x2 b)
    {
        return detail::scalar_compare_gt(a, b);
    }

    static inline mask64x2 compare_neq(s64x2 a, s64x2 b)
    {
        return detail::scalar_compare_neq(a, b);
    }

    static inline mask64x2 compare_lt(s64x2 a, s64x2 b)
    {
        return detail::scalar_compare_lt(a, b);
    }

    static inline mask64x2 compare_le(s64x2 a, s64x2 b)
    {
        return detail::scalar_compare_le(a, b);
    }

    static inline mask64x2 compare_ge(s64x2 a, s64x2 b)
    {
        return detail::scalar_compare_ge(a, b);
    }

    static inline s64x2 select(mask64x2 mask, s64x2 a, s64x2 b)
    {
        return detail::scalar_select(mask, a, b);
    }

    static inline s64x2 min(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline s64x2 max(s64x2 a, s64x2 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // shift by constant

    template <int Count>
    static inline s64x2 slli(s64x2 a)
    {
        return detail::scalar_shift_left<s64x2, u64, Count>(a);
    }

    template <int Count>
    static inline s64x2 srli(s64x2 a)
    {
        return detail::scalar_shift_right<s64x2, u64, Count>(a);
    }

    // shift by scalar

    static inline s64x2 sll(s64x2 a, int count)
    {
        return detail::scalar_shift_left<s64x2, u64>(a, count);
    }

    static inline s64x2 srl(s64x2 a, int count)
    {
        return detail::scalar_shift_right<s64x2, u64>(a, count);
    }

    // -----------------------------------------------------------------
    // mask8x16
    // -----------------------------------------------------------------

    static inline mask8x16 operator & (mask8x16 a, mask8x16 b)
    {
        return a.mask & b.mask;
    }

    static inline mask8x16 operator | (mask8x16 a, mask8x16 b)
    {
        return a.mask | b.mask;
    }

    static inline mask8x16 operator ^ (mask8x16 a, mask8x16 b)
    {
        return a.mask ^ b.mask;
    }

    static inline mask8x16 operator ! (mask8x16 a)
    {
        return ~a.mask;
    }

    static inline u32 get_mask(mask8x16 a)
    {
        return a.mask;
    }

    static inline bool none_of(mask8x16 a)
    {
        return a.mask == 0;
    }

    static inline bool any_of(mask8x16 a)
    {
        return a.mask != 0;
    }

    static inline bool all_of(mask8x16 a)
    {
        return a.mask == 0xffff;
    }

    // -----------------------------------------------------------------
    // mask16x8
    // -----------------------------------------------------------------

    static inline mask16x8 operator & (mask16x8 a, mask16x8 b)
    {
        return a.mask & b.mask;
    }

    static inline mask16x8 operator | (mask16x8 a, mask16x8 b)
    {
        return a.mask | b.mask;
    }

    static inline mask16x8 operator ^ (mask16x8 a, mask16x8 b)
    {
        return a.mask ^ b.mask;
    }

    static inline mask16x8 operator ! (mask16x8 a)
    {
        return ~a.mask;
    }

    static inline u32 get_mask(mask16x8 a)
    {
        return a.mask;
    }

    static inline bool none_of(mask16x8 a)
    {
        return a.mask == 0;
    }

    static inline bool any_of(mask16x8 a)
    {
        return a.mask != 0;
    }

    static inline bool all_of(mask16x8 a)
    {
        return a.mask == 0xff;
    }

    // -----------------------------------------------------------------
    // mask32x4
    // -----------------------------------------------------------------

    static inline mask32x4 operator & (mask32x4 a, mask32x4 b)
    {
        return a.mask & b.mask;
    }

    static inline mask32x4 operator | (mask32x4 a, mask32x4 b)
    {
        return a.mask | b.mask;
    }

    static inline mask32x4 operator ^ (mask32x4 a, mask32x4 b)
    {
        return a.mask ^ b.mask;
    }

    static inline mask32x4 operator ! (mask32x4 a)
    {
        return ~a.mask;
    }

    static inline u32 get_mask(mask32x4 a)
    {
        return a.mask;
    }

    static inline bool none_of(mask32x4 a)
    {
        return a.mask == 0;
    }

    static inline bool any_of(mask32x4 a)
    {
        return a.mask != 0;
    }

    static inline bool all_of(mask32x4 a)
    {
        return a.mask == 0xf;
    }

    // -----------------------------------------------------------------
    // mask64x2
    // -----------------------------------------------------------------

    static inline mask64x2 operator & (mask64x2 a, mask64x2 b)
    {
        return a.mask & b.mask;
    }

    static inline mask64x2 operator | (mask64x2 a, mask64x2 b)
    {
        return a.mask | b.mask;
    }

    static inline mask64x2 operator ^ (mask64x2 a, mask64x2 b)
    {
        return a.mask ^ b.mask;
    }

    static inline mask64x2 operator ! (mask64x2 a)
    {
        return ~a.mask;
    }

    static inline u32 get_mask(mask64x2 a)
    {
        return a.mask;
    }

    static inline bool none_of(mask64x2 a)
    {
        return a.mask == 0;
    }

    static inline bool any_of(mask64x2 a)
    {
        return a.mask != 0;
    }

    static inline bool all_of(mask64x2 a)
    {
        return a.mask == 0x3;
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
