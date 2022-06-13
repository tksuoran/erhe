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
        return (v16u8) __msa_insert_b((v16i8) a, Index, s);
    }

    template <unsigned int Index>
    static inline u8 get_component(u8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return __msa_copy_u_b(a, Index);
    }

    static inline u8x16 u8x16_zero()
    {
        return (v16u8) __msa_fill_b(0);
    }

    static inline u8x16 u8x16_set(u8 s)
    {
        return (v16u8) __msa_fill_b(s);
    }

    static inline u8x16 u8x16_set(
        u8 s0, u8 s1, u8 s2, u8 s3, u8 s4, u8 s5, u8 s6, u8 s7,
        u8 s8, u8 s9, u8 s10, u8 s11, u8 s12, u8 s13, u8 s14, u8 s15)
    {
        return (v16u8) { s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15 };
    }

    static inline u8x16 u8x16_uload(const u8* source)
    {
        //return v16u8(__msa_ld_b(source, 0));
        u8x16 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void u8x16_ustore(u8* dest, u8x16 a)
    {
        //__msa_st_b(v16i8(a), dest, 0);
        std::memcpy(dest, &a, sizeof(a));
    }

    static inline u8x16 u8x16_load_low(const u8* source)
    {
        return (v2u64) { uload64(source), 0 };
    }

    static inline void u8x16_store_low(u8* dest, u8x16 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline u8x16 unpacklo(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_ilvr_b((v16i8)b, (v16i8)a);
    }

    static inline u8x16 unpackhi(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_ilvl_b((v16i8)b, (v16i8)a);
    }

    static inline u8x16 add(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_addv_b((v16i8)a, (v16i8)b);
    }

    static inline u8x16 sub(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_subv_b((v16i8)a, (v16i8)b);
    }

    static inline u8x16 adds(u8x16 a, u8x16 b)
    {
        return __msa_adds_u_b(a, b);
    }

    static inline u8x16 subs(u8x16 a, u8x16 b)
    {
        return __msa_subs_u_b(a, b);
    }

    static inline u8x16 avg(u8x16 a, u8x16 b)
    {
        return __msa_ave_u_b(a, b);
    }

    static inline u8x16 avg_round(u8x16 a, u8x16 b)
    {
        return __msa_aver_u_b(a, b);
    }

    // bitwise

    static inline u8x16 bitwise_nand(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline u8x16 bitwise_and(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline u8x16 bitwise_or(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline u8x16 bitwise_xor(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline u8x16 bitwise_not(u8x16 a)
    {
        return (v16u8) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask8x16 compare_eq(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_ceq_b((v16i8) a, (v16i8) b);
    }

    static inline mask8x16 compare_gt(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_clt_u_b(b, a);
    }

    static inline mask8x16 compare_neq(u8x16 a, u8x16 b)
    {
        auto mask = (v16u8) __msa_ceq_b((v16i8) a, (v16i8) b);
        return (v16u8) __msa_nor_v(mask, mask);
    }

    static inline mask8x16 compare_lt(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_clt_u_b(a, b);
    }

    static inline mask8x16 compare_le(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_cle_u_b(a, b);
    }

    static inline mask8x16 compare_ge(u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_cle_u_b(b, a);
    }

    static inline u8x16 select(mask8x16 mask, u8x16 a, u8x16 b)
    {
        return (v16u8) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline u8x16 min(u8x16 a, u8x16 b)
    {
        return __msa_min_u_b(a, b);
    }

    static inline u8x16 max(u8x16 a, u8x16 b)
    {
        return __msa_max_u_b(a, b);
    }

    // -----------------------------------------------------------------
    // u16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u16x8 set_component(u16x8 a, u16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        return (v8u16) __msa_insert_h((v8i16) a, Index, s);
    }

    template <unsigned int Index>
    static inline u16 get_component(u16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return __msa_copy_u_h(a, Index);
    }

    static inline u16x8 u16x8_zero()
    {
        return (v8u16) __msa_fill_h(0);
    }

    static inline u16x8 u16x8_set(u16 s)
    {
        return (v8u16) __msa_fill_h(s);
    }

    static inline u16x8 u16x8_set(u16 s0, u16 s1, u16 s2, u16 s3, u16 s4, u16 s5, u16 s6, u16 s7)
    {
        return (v8u16) { s0, s1, s2, s3, s4, s5, s6, s7 };
    }

    static inline u16x8 u16x8_uload(const u16* source)
    {
        //return v8u16(__msa_ld_h(source, 0));
        u16x8 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void u16x8_ustore(u16* dest, u16x8 a)
    {
        //__msa_st_h(v8i16(a), dest, 0);
        std::memcpy(dest, &a, sizeof(a));
    }

    static inline u16x8 u16x8_load_low(const u16* source)
    {
        return (v8u16) { source[0], source[1], source[2], source[3], 0, 0, 0, 0 };
    }

    static inline void u16x8_store_low(u16* dest, u16x8 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline u16x8 unpacklo(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_ilvr_h((v8i16)b, (v8i16)a);
    }

    static inline u16x8 unpackhi(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_ilvl_h((v8i16)b, (v8i16)a);
    }

    static inline u16x8 add(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_addv_h((v8i16)a, (v8i16)b);
    }

    static inline u16x8 sub(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_subv_h((v8i16)a, (v8i16)b);
    }

    static inline u16x8 adds(u16x8 a, u16x8 b)
    {
        return __msa_adds_u_h(a, b);
    }

    static inline u16x8 subs(u16x8 a, u16x8 b)
    {
        return __msa_subs_u_h(a, b);
    }

    static inline u16x8 avg(u16x8 a, u16x8 b)
    {
        return __msa_ave_u_h(a, b);
    }

    static inline u16x8 avg_round(u16x8 a, u16x8 b)
    {
        return __msa_aver_u_h(a, b);
    }

    static inline u16x8 mullo(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_mulv_h((v8i16) a, (v8i16) b);
    }

    // bitwise

    static inline u16x8 bitwise_nand(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline u16x8 bitwise_and(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline u16x8 bitwise_or(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline u16x8 bitwise_xor(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline u16x8 bitwise_not(u16x8 a)
    {
        return (v8u16) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask16x8 compare_neq(u16x8 a, u16x8 b)
    {
        auto mask = (v8u16) __msa_ceq_h((v8i16) a, (v8i16) b);
        return (v8u16) __msa_nor_v(mask, mask);
    }

    static inline mask16x8 compare_lt(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_clt_u_h(a, b);
    }

    static inline mask16x8 compare_le(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_cle_u_h(a, b);
    }

    static inline mask16x8 compare_ge(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_cle_u_h(b, a);
    }

    static inline mask16x8 compare_eq(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_ceq_h((v8i16) a, (v8i16) b);
    }

    static inline mask16x8 compare_gt(u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_clt_u_h(b, a);
    }

    static inline u16x8 select(mask16x8 mask, u16x8 a, u16x8 b)
    {
        return (v8u16) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline u16x8 min(u16x8 a, u16x8 b)
    {
        return __msa_min_u_h(a, b);
    }

    static inline u16x8 max(u16x8 a, u16x8 b)
    {
        return __msa_max_u_h(a, b);
    }

    // shift by constant

    template <int Count>
    static inline u16x8 slli(u16x8 a)
    {
        return (v8u16) __msa_slli_h((v8i16)a, Count);
    }

    template <int Count>
    static inline u16x8 srli(u16x8 a)
    {
        return (v8u16) __msa_srli_h((v8i16)a, Count);
    }

    template <int Count>
    static inline u16x8 srai(u16x8 a)
    {
        return (v8u16) __msa_srai_h((v8i16)a, Count);
    }

    // shift by scalar

    static inline u16x8 sll(u16x8 a, int count)
    {
        return (v8u16) __msa_sll_h((v8u16) a, (v8i16) __msa_fill_h(count));
    }

    static inline u16x8 srl(u16x8 a, int count)
    {
        return (v8u16) __msa_srl_h((v8u16) a, (v8i16) __msa_fill_h(count));
    }

    static inline u16x8 sra(u16x8 a, int count)
    {
        return (v8u16) __msa_sra_h((v8u16) a, (v8i16) __msa_fill_h(count));
    }

    // -----------------------------------------------------------------
    // u32x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline u32x4 shuffle(u32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const v4i32 control = (v4i32) { x, y, z, w };
        return (v4u32) __msa_vshf_w(control, (v4i32) v, (v4i32) v);
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
        return (v4u32) __msa_insert_w((v4i32) a, Index, s);
    }

    template <unsigned int Index>
    static inline u32 get_component(u32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return __msa_copy_u_w(a, Index);
    }

    static inline u32x4 u32x4_zero()
    {
        return (v4u32) __msa_fill_w(0);
    }

    static inline u32x4 u32x4_set(u32 s)
    {
        return (v4u32) __msa_fill_w(s);
    }

    static inline u32x4 u32x4_set(u32 x, u32 y, u32 z, u32 w)
    {
        return (v4u32) { x, y, z, w };
    }

    static inline u32x4 u32x4_uload(const u32* source)
    {
        //return v4u32(__msa_ld_w(source, 0));
        return reinterpret_cast<const v4u32 *>(source)[0];
    }

    static inline void u32x4_ustore(u32* dest, u32x4 a)
    {
        //__msa_st_w(v4i32(a), dest, 0);
        reinterpret_cast<v4u32 *>(dest)[0] = a;
    }

    static inline u32x4 u32x4_load_low(const u32* source)
    {
        return (v4u32) { source[0], source[1], 0, 0 };
    }

    static inline void u32x4_store_low(u32* dest, u32x4 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline u32x4 unpacklo(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_ilvr_w((v4i32)b, (v4i32)a);
    }

    static inline u32x4 unpackhi(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_ilvl_w((v4i32)b, (v4i32)a);
    }

    static inline u32x4 add(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_addv_w((v4i32)a, (v4i32)b);
    }

    static inline u32x4 sub(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_subv_w((v4i32)a, (v4i32)b);
    }

    static inline u32x4 adds(u32x4 a, u32x4 b)
    {
        return __msa_adds_u_w(a, b);
    }

    static inline u32x4 subs(u32x4 a, u32x4 b)
    {
        return __msa_subs_u_w(a, b);
    }

    static inline u32x4 avg(u32x4 a, u32x4 b)
    {
        return __msa_ave_u_w(a, b);
    }

    static inline u32x4 avg_round(u32x4 a, u32x4 b)
    {
        return __msa_aver_u_w(a, b);
    }

    static inline u32x4 mullo(u32x4 a, u32x4 b)
    {
        return (u32x4) __msa_mulv_w((s32x4) a, (s32x4) b);
    }

    // bitwise

    static inline u32x4 bitwise_nand(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline u32x4 bitwise_and(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline u32x4 bitwise_or(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline u32x4 bitwise_xor(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline u32x4 bitwise_not(u32x4 a)
    {
        return (v4u32) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask32x4 compare_eq(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_ceq_w((v4i32) a, (v4i32) b);
    }

    static inline mask32x4 compare_gt(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_clt_u_w(b, a);
    }

    static inline mask32x4 compare_neq(u32x4 a, u32x4 b)
    {
        auto mask = (v4u32) __msa_ceq_w((v4i32) a, (v4i32) b);
        return (v4u32) __msa_nor_v(mask, mask);
    }

    static inline mask32x4 compare_lt(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_clt_u_w(a, b);
    }

    static inline mask32x4 compare_le(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_cle_u_w(a, b);
    }

    static inline mask32x4 compare_ge(u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_cle_u_w(b, a);
    }

    static inline u32x4 select(mask32x4 mask, u32x4 a, u32x4 b)
    {
        return (v4u32) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline u32x4 min(u32x4 a, u32x4 b)
    {
        return __msa_min_u_w(a, b);
    }

    static inline u32x4 max(u32x4 a, u32x4 b)
    {
        return __msa_max_u_w(a, b);
    }

    // shift by constant

    template <int Count>
    static inline u32x4 slli(u32x4 a)
    {
        return (v4u32) __msa_slli_w((v4i32)a, Count);
    }

    template <int Count>
    static inline u32x4 srli(u32x4 a)
    {
        return (v4u32) __msa_srli_w((v4i32)a, Count);
    }

    template <int Count>
    static inline u32x4 srai(u32x4 a)
    {
        return (v4u32) __msa_srai_w((v4i32)a, Count);
    }

    // shift by scalar

    static inline u32x4 sll(u32x4 a, int count)
    {
        return (v4u32) __msa_sll_w((v4u32) a, (v4i32) __msa_fill_w(count));
    }

    static inline u32x4 srl(u32x4 a, int count)
    {
        return (v4u32) __msa_srl_w((v4u32) a, (v4i32) __msa_fill_w(count));
    }

    static inline u32x4 sra(u32x4 a, int count)
    {
        return (v4u32) __msa_sra_w((v4u32) a, (v4i32) __msa_fill_w(count));
    }

    // shift by vector

    static inline u32x4 sll(u32x4 a, u32x4 count)
    {
        return (v4u32) __msa_sll_w((v4i32)a, (v4i32)count);
    }

    static inline u32x4 srl(u32x4 a, u32x4 count)
    {
        return (v4u32) __msa_srl_w((v4i32)a, (v4i32)count);
    }

    static inline u32x4 sra(u32x4 a, u32x4 count)
    {
        return (v4u32) __msa_sra_w((v4i32)a, (v4i32)count);
    }

    // -----------------------------------------------------------------
    // u64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u64x2 set_component(u64x2 a, u64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return (v2u64) __msa_insert_d((v2i64) a, Index, s);
    }

    template <unsigned int Index>
    static inline u64 get_component(u64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return __msa_copy_u_d(a, Index);
    }

    static inline u64x2 u64x2_zero()
    {
        return (v2u64) __msa_fill_d(0);
    }

    static inline u64x2 u64x2_set(u64 s)
    {
        return (v2u64) __msa_fill_d(s);
    }

    static inline u64x2 u64x2_set(u64 x, u64 y)
    {
        return (v2u64) { x, y };
    }

    static inline u64x2 u64x2_uload(const u64* source)
    {
        //return v2u64(__msa_ld_d(source, 0));
        u64x2 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void u64x2_ustore(u64* dest, u64x2 a)
    {
        //__msa_st_d(v2i64(a), dest, 0);
        std::memcpy(dest, &a, sizeof(a));
    }

    static inline u64x2 unpacklo(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_ilvr_d((v2i64)b, (v2i64)a);
    }

    static inline u64x2 unpackhi(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_ilvl_d((v2i64)b, (v2i64)a);
    }

    static inline u64x2 add(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_addv_d((v2i64)a, (v2i64)b);
    }

    static inline u64x2 sub(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_subv_d((v2i64)a, (v2i64)b);
    }

    static inline u64x2 avg(u64x2 a, u64x2 b)
    {
        return __msa_ave_u_d(a, b);
    }

    static inline u64x2 avg_round(u64x2 a, u64x2 b)
    {
        return __msa_aver_u_d(a, b);
    }

    // bitwise

    static inline u64x2 bitwise_nand(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline u64x2 bitwise_and(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline u64x2 bitwise_or(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline u64x2 bitwise_xor(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline u64x2 bitwise_not(u64x2 a)
    {
        return (v2u64) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask64x2 compare_eq(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_ceq_d((v2u64) a, (v2u64) b);
    }

    static inline mask64x2 compare_gt(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_clti_u_d((v2u64) a, (v2u64) b);
    }

    static inline mask64x2 compare_neq(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_nor_v(compare_eq(b, a));
    }

    static inline mask64x2 compare_lt(u64x2 a, u64x2 b)
    {
        return compare_gt(b, a);
    }

    static inline mask64x2 compare_le(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_nor_v(compare_gt(a, b));
    }

    static inline mask64x2 compare_ge(u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_nor_v(compare_gt(b, a));
    }

    static inline u64x2 select(mask64x2 mask, u64x2 a, u64x2 b)
    {
        return (v2u64) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline u64x2 min(u64x2 a, u64x2 b)
    {
        return __msa_min_u_d(a, b);
    }

    static inline u64x2 max(u64x2 a, u64x2 b)
    {
        return __msa_max_u_d(a, b);
    }

    // shift by constant

    template <int Count>
    static inline u64x2 slli(u64x2 a)
    {
        return (v2u64) __msa_slli_d((v2i64)a, Count);
    }

    template <int Count>
    static inline u64x2 srli(u64x2 a)
    {
        return (v2u64) __msa_srli_d((v2i64)a, Count);
    }

    // shift by scalar

    static inline u64x2 sll(u64x2 a, int count)
    {
        return (v2u64) __msa_sll_d((v2u64) a, (v2i64) __msa_fill_d(count));
    }

    static inline u64x2 srl(u64x2 a, int count)
    {
        return (v2u64) __msa_srl_d((v2u64) a, (v2i64) __msa_fill_d(count));
    }

    // -----------------------------------------------------------------
    // s8x16
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s8x16 set_component(s8x16 a, s8 s)
    {
        static_assert(Index < 16, "Index out of range.");
        return __msa_insert_b(a, Index, s);
    }

    template <unsigned int Index>
    static inline s8 get_component(s8x16 a)
    {
        static_assert(Index < 16, "Index out of range.");
        return __msa_copy_s_b(a, Index);
    }

    static inline s8x16 s8x16_zero()
    {
        return __msa_fill_b(0);
    }

    static inline s8x16 s8x16_set(s8 s)
    {
        return __msa_fill_b(s);
    }

    static inline s8x16 s8x16_set(
        s8 v0, s8 v1, s8 v2, s8 v3, s8 v4, s8 v5, s8 v6, s8 v7,
        s8 v8, s8 v9, s8 v10, s8 v11, s8 v12, s8 v13, s8 v14, s8 v15)
    {
        return (v16i8) { v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15 };
    }

    static inline s8x16 s8x16_uload(const s8* source)
    {
        //return v16u8(__msa_ld_b(source, 0));
        s8x16 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void s8x16_ustore(s8* dest, s8x16 a)
    {
        //__msa_st_b(v16i8(a), dest, 0);
        std::memcpy(dest, &a, sizeof(a));
    }

    static inline s8x16 s8x16_load_low(const s8* source)
    {
        return (v2i64) { uload64(source), 0 };
    }

    static inline void s8x16_store_low(s8* dest, s8x16 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline s8x16 unpacklo(s8x16 a, s8x16 b)
    {
        return __msa_ilvr_b(b, a);
    }

    static inline s8x16 unpackhi(s8x16 a, s8x16 b)
    {
        return __msa_ilvl_b(b, a);
    }

    static inline s8x16 add(s8x16 a, s8x16 b)
    {
        return __msa_addv_b(a, b);
    }

    static inline s8x16 sub(s8x16 a, s8x16 b)
    {
        return __msa_subv_b(a, b);
    }

    static inline s8x16 adds(s8x16 a, s8x16 b)
    {
        return __msa_adds_s_b(a, b);
    }

    static inline s8x16 subs(s8x16 a, s8x16 b)
    {
        return __msa_subs_s_b(a, b);
    }

    static inline s8x16 avg(s8x16 a, s8x16 b)
    {
        return __msa_ave_s_b(a, b);
    }

    static inline s8x16 avg_round(s8x16 a, s8x16 b)
    {
        return __msa_aver_s_b(a, b);
    }

    static inline s8x16 abs(s8x16 a)
    {
        return __msa_add_a_b(a, (v16i8) __msa_xor_v((v16u8) a, (v16u8) a));
    }

    static inline s8x16 neg(s8x16 a)
    {
        const v16i8 zero = (v16i8) __msa_xor_v((v16u8) a, (v16u8) a);
        return __msa_subs_s_b(zero, a);
    }

    // bitwise

    static inline s8x16 bitwise_nand(s8x16 a, s8x16 b)
    {
        return (v16i8) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline s8x16 bitwise_and(s8x16 a, s8x16 b)
    {
        return (v16i8) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline s8x16 bitwise_or(s8x16 a, s8x16 b)
    {
        return (v16i8) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline s8x16 bitwise_xor(s8x16 a, s8x16 b)
    {
        return (v16i8) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline s8x16 bitwise_not(s8x16 a)
    {
        return (v16i8) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask8x16 compare_eq(s8x16 a, s8x16 b)
    {
        return (v16u8) __msa_ceq_b(a, b);
    }

    static inline mask8x16 compare_gt(s8x16 a, s8x16 b)
    {
        return (v16u8) __msa_clt_s_b(b, a);
    }

    static inline mask8x16 compare_neq(s8x16 a, s8x16 b)
    {
        auto mask = (v16u8) __msa_ceq_b(a, b);
        return (v16u8) __msa_nor_v(mask, mask);
    }

    static inline mask8x16 compare_lt(s8x16 a, s8x16 b)
    {
        return (v16u8) __msa_clt_s_b(a, b);
    }

    static inline mask8x16 compare_le(s8x16 a, s8x16 b)
    {
        return (v16u8) __msa_cle_s_b(a, b);
    }

    static inline mask8x16 compare_ge(s8x16 a, s8x16 b)
    {
        return (v16u8) __msa_cle_s_b(b, a);
    }

    static inline s8x16 select(mask8x16 mask, s8x16 a, s8x16 b)
    {
        return (v16i8) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline s8x16 min(s8x16 a, s8x16 b)
    {
        return __msa_min_s_b(a, b);
    }

    static inline s8x16 max(s8x16 a, s8x16 b)
    {
        return __msa_max_s_b(a, b);
    }

    // -----------------------------------------------------------------
    // s16x8
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s16x8 set_component(s16x8 a, s16 s)
    {
        static_assert(Index < 8, "Index out of range.");
        return __msa_insert_h(a, Index, s);
    }

    template <unsigned int Index>
    static inline s16 get_component(s16x8 a)
    {
        static_assert(Index < 8, "Index out of range.");
        return __msa_copy_s_h(a, Index);
    }

    static inline s16x8 s16x8_zero()
    {
        return __msa_fill_h(0);
    }

    static inline s16x8 s16x8_set(s16 s)
    {
        return __msa_fill_h(s);
    }

    static inline s16x8 s16x8_set(s16 s0, s16 s1, s16 s2, s16 s3, s16 s4, s16 s5, s16 s6, s16 s7)
    {
        return (v8i16) { s0, s1, s2, s3, s4, s5, s6, s7 };
    }

    static inline s16x8 s16x8_uload(const s16* source)
    {
        //return v8u16(__msa_ld_h(source, 0));
        s16x8 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void s16x8_ustore(s16* dest, s16x8 a)
    {
        //__msa_st_h(v8i16(a), dest, 0);
        std::memcpy(dest, &a, sizeof(a));
    }

    static inline s16x8 s16x8_load_low(const s16* source)
    {
        return (v8i16) { source[0], source[1], source[2], source[3], 0, 0, 0, 0 };
    }

    static inline void s16x8_store_low(s16* dest, s16x8 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline s16x8 unpacklo(s16x8 a, s16x8 b)
    {
        return __msa_ilvr_h(b, a);
    }

    static inline s16x8 unpackhi(s16x8 a, s16x8 b)
    {
        return __msa_ilvl_h(b, a);
    }

    static inline s16x8 add(s16x8 a, s16x8 b)
    {
        return __msa_addv_h(a, b);
    }

    static inline s16x8 sub(s16x8 a, s16x8 b)
    {
        return __msa_subv_h(a, b);
    }

    static inline s16x8 adds(s16x8 a, s16x8 b)
    {
        return __msa_adds_s_h(a, b);
    }

    static inline s16x8 subs(s16x8 a, s16x8 b)
    {
        return __msa_subs_s_h(a, b);
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

    static inline s16x8 avg(s16x8 a, s16x8 b)
    {
        return __msa_ave_s_h(a, b);
    }

    static inline s16x8 avg_round(s16x8 a, s16x8 b)
    {
        return __msa_aver_s_h(a, b);
    }

    static inline s16x8 mullo(s16x8 a, s16x8 b)
    {
        return __msa_mulv_h(a, b);
    }

    static inline s32x4 madd(s16x8 va, s16x8 vb)
    {
        s16 a[8];
        std::memcpy(a, &va, 16);

        s16 b[8];
        std::memcpy(b, &vb, 16);

        s32 x = s32(a[0]) * s32(b[0]) + s32(a[1]) * s32(b[1]);
        s32 y = s32(a[2]) * s32(b[2]) + s32(a[3]) * s32(b[3]);
        s32 z = s32(a[4]) * s32(b[4]) + s32(a[5]) * s32(b[5]);
        s32 w = s32(a[6]) * s32(b[6]) + s32(a[7]) * s32(b[7]);

        return (v4i32) { x, y, z, w };
    }

    static inline s16x8 abs(s16x8 a)
    {
        return __msa_add_a_h(a, (v8i16) __msa_xor_v((v16u8) a, (v16u8) a));
    }

    static inline s16x8 neg(s16x8 a)
    {
        const v8i16 zero = (v8i16) __msa_xor_v((v16u8) a, (v16u8) a);
        return __msa_subs_s_h(zero, a);
    }

    // bitwise

    static inline s16x8 bitwise_nand(s16x8 a, s16x8 b)
    {
        return (v8i16) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline s16x8 bitwise_and(s16x8 a, s16x8 b)
    {
        return (v8i16) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline s16x8 bitwise_or(s16x8 a, s16x8 b)
    {
        return (v8i16) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline s16x8 bitwise_xor(s16x8 a, s16x8 b)
    {
        return (v8i16) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline s16x8 bitwise_not(s16x8 a)
    {
        return (v8i16) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask16x8 compare_eq(s16x8 a, s16x8 b)
    {
        return (v8u16) __msa_ceq_h(a, b);
    }

    static inline mask16x8 compare_gt(s16x8 a, s16x8 b)
    {
        return (v8u16) __msa_clt_s_h(b, a);
    }

    static inline mask16x8 compare_neq(s16x8 a, s16x8 b)
    {
        auto mask = (v8u16) __msa_ceq_h(a, b);
        return (v8u16) __msa_nor_v(mask, mask);
    }

    static inline mask16x8 compare_lt(s16x8 a, s16x8 b)
    {
        return (v8u16) __msa_clt_s_h(a, b);
    }

    static inline mask16x8 compare_le(s16x8 a, s16x8 b)
    {
        return (v8u16) __msa_cle_s_h(a, b);
    }

    static inline mask16x8 compare_ge(s16x8 a, s16x8 b)
    {
        return (v8u16) __msa_cle_s_h(b, a);
    }

    static inline s16x8 select(mask16x8 mask, s16x8 a, s16x8 b)
    {
        return (v8i16) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline s16x8 min(s16x8 a, s16x8 b)
    {
        return __msa_min_s_h(a, b);
    }

    static inline s16x8 max(s16x8 a, s16x8 b)
    {
        return __msa_max_s_h(a, b);
    }

    // shift by constant

    template <int Count>
    static inline s16x8 slli(s16x8 a)
    {
        return __msa_slli_h(a, Count);
    }

    template <int Count>
    static inline s16x8 srli(s16x8 a)
    {
        return __msa_srli_h(a, Count);
    }

    template <int Count>
    static inline s16x8 srai(s16x8 a)
    {
        return __msa_srai_h(a, Count);
    }

    // shift by scalar

    static inline s16x8 sll(s16x8 a, int count)
    {
        return __msa_sll_h(a, __msa_fill_h(count));
    }

    static inline s16x8 srl(s16x8 a, int count)
    {
        return __msa_srl_h(a, __msa_fill_h(count));
    }

    static inline s16x8 sra(s16x8 a, int count)
    {
        return __msa_sra_h(a, __msa_fill_h(count));
    }

    // -----------------------------------------------------------------
    // s32x4
    // -----------------------------------------------------------------

    // shuffle

    template <u32 x, u32 y, u32 z, u32 w>
    static inline s32x4 shuffle(s32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const v4i32 control = (v4i32) { x, y, z, w };
        return __msa_vshf_w(control, v, v);
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
        return __msa_insert_w(a, Index, s);
    }

    template <unsigned int Index>
    static inline s32 get_component(s32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        return __msa_copy_s_w(a, Index);
    }

    static inline s32x4 s32x4_zero()
    {
        return __msa_fill_w(0);
    }

    static inline s32x4 s32x4_set(s32 s)
    {
        return __msa_fill_w(s);
    }

    static inline s32x4 s32x4_set(s32 x, s32 y, s32 z, s32 w)
    {
        return (v4i32) { x, y, z, w };
    }

    static inline s32x4 s32x4_uload(const s32* source)
    {
        //return v4u32(__msa_ld_w(source, 0));
        return reinterpret_cast<const v4i32 *>(source)[0];
    }

    static inline void s32x4_ustore(s32* dest, s32x4 a)
    {
        //__msa_st_w(v4i32(a), dest, 0);
        reinterpret_cast<v4i32 *>(dest)[0] = a;
    }

    static inline s32x4 s32x4_load_low(const s32* source)
    {
        return (v4i32) { source[0], source[1], 0, 0 };
    }

    static inline void s32x4_store_low(s32* dest, s32x4 a)
    {
        std::memcpy(dest, &a, 8);
    }

    static inline s32x4 unpacklo(s32x4 a, s32x4 b)
    {
        return __msa_ilvr_w(b, a);
    }

    static inline s32x4 unpackhi(s32x4 a, s32x4 b)
    {
        return __msa_ilvl_w(b, a);
    }

    static inline s32x4 abs(s32x4 a)
    {
        return __msa_add_a_w(a, (v4i32) __msa_xor_v((v16u8) a, (v16u8) a));
    }

    static inline s32x4 neg(s32x4 a)
    {
        const v4i32 zero = (v4i32) __msa_xor_v((v16u8) a, (v16u8) a);
        return __msa_subs_s_w(zero, a);
    }

    static inline s32x4 add(s32x4 a, s32x4 b)
    {
        return __msa_addv_w(a, b);
    }

    static inline s32x4 sub(s32x4 a, s32x4 b)
    {
        return __msa_subv_w(a, b);
    }

    static inline s32x4 adds(s32x4 a, s32x4 b)
    {
        return __msa_adds_s_w(a, b);
    }

    static inline s32x4 subs(s32x4 a, s32x4 b)
    {
        return __msa_subs_s_w(a, b);
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

    static inline s32x4 avg(s32x4 a, s32x4 b)
    {
        return __msa_ave_s_w(a, b);
    }

    static inline s32x4 avg_round(s32x4 a, s32x4 b)
    {
        return __msa_aver_s_w(a, b);
    }

    static inline s32x4 mullo(s32x4 a, s32x4 b)
    {
        return __msa_mulv_w(a, b);
    }

    // bitwise

    static inline s32x4 bitwise_nand(s32x4 a, s32x4 b)
    {
        return (v4i32) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline s32x4 bitwise_and(s32x4 a, s32x4 b)
    {
        return (v4i32) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline s32x4 bitwise_or(s32x4 a, s32x4 b)
    {
        return (v4i32) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline s32x4 bitwise_xor(s32x4 a, s32x4 b)
    {
        return (v4i32) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline s32x4 bitwise_not(s32x4 a)
    {
        return (v4i32) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask32x4 compare_eq(s32x4 a, s32x4 b)
    {
        return (v4u32) __msa_ceq_w(a, b);
    }

    static inline mask32x4 compare_gt(s32x4 a, s32x4 b)
    {
        return (v4u32) __msa_clt_s_w(b, a);
    }

    static inline mask32x4 compare_neq(s32x4 a, s32x4 b)
    {
        auto mask = (v4u32) __msa_ceq_w(a, b);
        return (v4u32) __msa_nor_v(mask, mask);
    }

    static inline mask32x4 compare_lt(s32x4 a, s32x4 b)
    {
        return (v4u32) __msa_clt_s_w(a, b);
    }

    static inline mask32x4 compare_le(s32x4 a, s32x4 b)
    {
        return (v4u32) __msa_cle_s_w(a, b);
    }

    static inline mask32x4 compare_ge(s32x4 a, s32x4 b)
    {
        return (v4u32) __msa_cle_s_w(b, a);
    }

    static inline s32x4 select(mask32x4 mask, s32x4 a, s32x4 b)
    {
        return (v4i32) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline s32x4 min(s32x4 a, s32x4 b)
    {
        return __msa_min_s_w(a, b);
    }

    static inline s32x4 max(s32x4 a, s32x4 b)
    {
        return __msa_max_s_w(a, b);
    }

    // shift by constant

    template <int Count>
    static inline s32x4 slli(s32x4 a)
    {
        return __msa_slli_w(a, Count);
    }

    template <int Count>
    static inline s32x4 srli(s32x4 a)
    {
        return __msa_srli_w(a, Count);
    }

    template <int Count>
    static inline s32x4 srai(s32x4 a)
    {
        return __msa_srai_w(a, Count);
    }

    // shift by scalar

    static inline s32x4 sll(s32x4 a, int count)
    {
        return __msa_sll_w(a, __msa_fill_w(count));
    }

    static inline s32x4 srl(s32x4 a, int count)
    {
        return __msa_srl_w(a, __msa_fill_w(count));
    }

    static inline s32x4 sra(s32x4 a, int count)
    {
        return __msa_sra_w(a, __msa_fill_w(count));
    }

    // shift by vector

    static inline s32x4 sll(s32x4 a, u32x4 count)
    {
        return __msa_sll_w(a, (v4i32)count);
    }

    static inline s32x4 srl(s32x4 a, u32x4 count)
    {
        return __msa_srl_w(a, (v4i32)count);
}

    static inline s32x4 sra(s32x4 a, u32x4 count)
    {
        return __msa_sra_w(a, (v4i32)count);
    }

    static inline u32 pack(s32x4 s)
    {
        u32 x = __msa_copy_s_w(s, 0);
        u32 y = __msa_copy_s_w(s, 1);
        u32 z = __msa_copy_s_w(s, 2);
        u32 w = __msa_copy_s_w(s, 3);
        return (w << 24) | (z << 16) | (y << 8) | x;
    }

    static inline s32x4 unpack(u32 s)
    {
        s32 x = (s >> 0) & 0xff;
        s32 y = (s >> 8) & 0xff;
        s32 z = (s >> 16) & 0xff;
        s32 w = (s >> 24) & 0xff;
        return s32x4_set(x, y, z, w);
    }

    // -----------------------------------------------------------------
    // s64x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s64x2 set_component(s64x2 a, s64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return __msa_insert_d(a, Index, s);
    }

    template <unsigned int Index>
    static inline s64 get_component(s64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return __msa_copy_s_d(a, Index);
    }

    static inline s64x2 s64x2_zero()
    {
        return __msa_fill_d(0);
    }

    static inline s64x2 s64x2_set(s64 s)
    {
        return __msa_fill_d(s);
    }

    static inline s64x2 s64x2_set(s64 x, s64 y)
    {
        return (v2i64) { x, y };
    }

    static inline s64x2 s64x2_uload(const s64* source)
    {
        //return v2u64(__msa_ld_d(source, 0));
        s64x2 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void s64x2_ustore(s64* dest, s64x2 a)
    {
        //__msa_st_d(v2i64(a), dest, 0);
        std::memcpy(dest, &a, sizeof(a));
    }

    static inline s64x2 unpacklo(s64x2 a, s64x2 b)
    {
        return __msa_ilvr_d(b, a);
    }

    static inline s64x2 unpackhi(s64x2 a, s64x2 b)
    {
        return __msa_ilvl_d(b, a);
    }

    static inline s64x2 add(s64x2 a, s64x2 b)
    {
        return __msa_addv_d(a, b);
    }

    static inline s64x2 sub(s64x2 a, s64x2 b)
    {
        return __msa_subv_d(a, b);
    }

    static inline s64x2 avg(s64x2 a, s64x2 b)
    {
        return __msa_ave_s_d(a, b);
    }

    static inline s64x2 avg_round(s64x2 a, s64x2 b)
    {
        return __msa_aver_s_d(a, b);
    }

    // bitwise

    static inline s64x2 bitwise_nand(s64x2 a, s64x2 b)
    {
        return (v2i64) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline s64x2 bitwise_and(s64x2 a, s64x2 b)
    {
        return (v2i64) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline s64x2 bitwise_or(s64x2 a, s64x2 b)
    {
        return (v2i64) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline s64x2 bitwise_xor(s64x2 a, s64x2 b)
    {
        return (v2i64) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline s64x2 bitwise_not(s64x2 a)
    {
        return (v2i64) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    // compare

    static inline mask64x2 compare_eq(s64x2 a, s64x2 b)
    {
        return (v2u64) __msa_ceq_d((v2i64) a, (v2i64) b);
    }

    static inline mask64x2 compare_gt(s64x2 a, s64x2 b)
    {
        return (v2u64) __msa_clti_s_d((v2i64) a, (v2i64) b);
    }

    static inline mask64x2 compare_neq(s64x2 a, s64x2 b)
    {
        return (v2u64) __msa_nor_v(compare_eq(b, a));
    }

    static inline mask64x2 compare_lt(s64x2 a, s64x2 b)
    {
        return compare_gt(b, a);
    }

    static inline mask64x2 compare_le(s64x2 a, s64x2 b)
    {
        return (v2u64) __msa_nor_v(compare_gt(a, b));
    }

    static inline mask64x2 compare_ge(s64x2 a, s64x2 b)
    {
        return (v2u64) __msa_nor_v(compare_gt(b, a));
    }

    static inline s64x2 select(mask64x2 mask, s64x2 a, s64x2 b)
    {
        return (v2i64) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    static inline s64x2 min(s64x2 a, s64x2 b)
    {
        return __msa_min_s_d(a, b);
    }

    static inline s64x2 max(s64x2 a, s64x2 b)
    {
        return __msa_max_s_d(a, b);
    }

    // shift by constant

    template <int Count>
    static inline s64x2 slli(s64x2 a)
    {
        return __msa_slli_d(a, Count);
    }

    template <int Count>
    static inline s64x2 srli(s64x2 a)
    {
        return __msa_srli_d(a, Count);
    }

    // shift by scalar

    static inline s64x2 sll(s64x2 a, int count)
    {
        return __msa_sll_d(a, __msa_fill_d(count));
    }

    static inline s64x2 srl(s64x2 a, int count)
    {
        return __msa_srl_d(a, __msa_fill_d(count));
    }

    // -----------------------------------------------------------------
    // mask8x16
    // -----------------------------------------------------------------

    static inline mask8x16 operator & (mask8x16 a, mask8x16 b)
    {
        return (v16u8) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline mask8x16 operator | (mask8x16 a, mask8x16 b)
    {
        return (v16u8) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline mask8x16 operator ^ (mask8x16 a, mask8x16 b)
    {
        return (v16u8) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline mask8x16 operator ! (mask8x16 a)
    {
        return (v16u8) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    static inline u32 get_mask(mask8x16 a)
    {
        u32 x = __msa_copy_u_w(a, 0) & 0x01800180;
        u32 y = __msa_copy_u_w(a, 1) & 0x01800180;
        u32 z = __msa_copy_u_w(a, 2) & 0x01800180;
        u32 w = __msa_copy_u_w(a, 3) & 0x01800180;
        x = (x >> 21) | (x >> 7);
        y = (y >> 17) | (y >> 3);
        z = (z >> 13) | (z << 1);
        w = (w >>  9) | (w << 5);
        return w | z | y | x;
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
        return (v8u16) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline mask16x8 operator | (mask16x8 a, mask16x8 b)
    {
        return (v8u16) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline mask16x8 operator ^ (mask16x8 a, mask16x8 b)
    {
        return (v8u16) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline mask16x8 operator ! (mask16x8 a)
    {
        return (v8u16) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    static inline u32 get_mask(mask16x8 a)
    {
        u32 x = __msa_copy_u_w(a, 0) & 0x00018000;
        u32 y = __msa_copy_u_w(a, 1) & 0x00018000;
        u32 z = __msa_copy_u_w(a, 2) & 0x00018000;
        u32 w = __msa_copy_u_w(a, 3) & 0x00018000;
        return (w >> 9) | (z >> 11) | (y >> 13) | (x >> 15);
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
        return (v4u32) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline mask32x4 operator | (mask32x4 a, mask32x4 b)
    {
        return (v4u32) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline mask32x4 operator ^ (mask32x4 a, mask32x4 b)
    {
        return (v4u32) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline mask32x4 operator ! (mask32x4 a)
    {
        return (v4u32) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    static inline u32 get_mask(mask32x4 a)
    {
        u32 x = __msa_copy_u_w(a, 0) & 1;
        u32 y = __msa_copy_u_w(a, 1) & 2;
        u32 z = __msa_copy_u_w(a, 2) & 4;
        u32 w = __msa_copy_u_w(a, 3) & 8;
        return w | z | y | x;
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
        return (v2u64) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline mask64x2 operator | (mask64x2 a, mask64x2 b)
    {
        return (v2u64) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline mask64x2 operator ^ (mask64x2 a, mask64x2 b)
    {
        return (v2u64) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline mask64x2 operator ! (mask64x2 a)
    {
        return (v2u64) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    static inline u32 get_mask(mask64x2 a)
    {
        u32 x = __msa_copy_u_d(a, 0) & 1;
        u32 y = __msa_copy_u_d(a, 1) & 2;
        return y | x;
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
