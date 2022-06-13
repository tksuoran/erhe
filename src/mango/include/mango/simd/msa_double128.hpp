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

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 v)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        const v4i32 control = (v4i32) { x * 2 + 0, x * 2 + 1, y * 2 + 0, y * 2 + 1 };
        return (v2f64) __msa_vshf_w(control, (v4i32) v, (v4i32) v);
    }

    template <u32 x, u32 y>
    static inline f64x2 shuffle(f64x2 a, f64x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        const v4i32 control = (v4i32) { x * 2 + 0, x * 2 + 1, y * 2 + 4, y * 2 + 5 };
        return (v2f64) __msa_vshf_w(control, (v4i32) a, (v4i32) b);
    }

    // indexed access

    template <int Index>
    static inline f64x2 set_component(f64x2 a, f64 s)
    {
        static_assert(Index < 2, "Index out of range.");
        s64 temp;
        std::memcpy(&temp, &s, 8);
        return (v2f64) __msa_insert_d((v2i64) a, Index, temp);
    }

    template <int Index>
    static inline f64 get_component(f64x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        u64 temp = __msa_copy_u_d((v2u64) a, Index);
        f64 s;
        std::memcpy(&s, &temp, 8);
        return s;
    }

    static inline f64x2 f64x2_zero()
    {
        return (v2f64) { 0.0, 0.0 };
    }

    static inline f64x2 f64x2_set(f64 s)
    {
        return (v2f64) { s, s };
    }

    static inline f64x2 f64x2_set(f64 x, f64 y)
    {
        return (v2f64) { x, y };
    }

    static inline f64x2 f64x2_uload(const f64* source)
    {
        //return v2f64(__msa_ld_d(source, 0));
        return reinterpret_cast<const v2f64 *>(source)[0];
    }

    static inline void f64x2_ustore(f64* dest, f64x2 a)
    {
        //__msa_st_d(v2i64(a), dest, 0);
        reinterpret_cast<v2f64 *>(dest)[0] = a;
    }

    static inline f64x2 unpackhi(f64x2 a, f64x2 b)
    {
        return (v2f64) __msa_ilvr_d((v2i64) b, (v2i64) a);
    }

    static inline f64x2 unpacklo(f64x2 a, f64x2 b)
    {
        return (v2f64) __msa_ilvl_d((v2i64) b, (v2i64) a);
    }

    // bitwise

    static inline f64x2 bitwise_nand(f64x2 a, f64x2 b)
    {
        return (v2f64) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline f64x2 bitwise_and(f64x2 a, f64x2 b)
    {
        return (v2f64) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline f64x2 bitwise_or(f64x2 a, f64x2 b)
    {
        return (v2f64) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline f64x2 bitwise_xor(f64x2 a, f64x2 b)
    {
        return (v2f64) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline f64x2 bitwise_not(f64x2 a)
    {
        return (v2f64) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    static inline f64x2 min(f64x2 a, f64x2 b)
    {
        return __msa_fmin_d(a, b);
    }

    static inline f64x2 max(f64x2 a, f64x2 b)
    {
        return __msa_fmax_d(a, b);
    }

    static inline f64x2 abs(f64x2 a)
    {
        return __msa_fmax_a_d(a, a);
    }

    static inline f64x2 neg(f64x2 a)
    {
        return __msa_fsub_d(f64x2_zero(), a);
    }

    static inline f64x2 sign(f64x2 a)
    {
        v16u8 sign_mask = (v16u8) f64x2_set(-0.0);
        v16u8 value_mask = (v16u8) __msa_fcune_d(a, f64x2_zero());
        v16u8 sign_bits = __msa_and_v((v16u8) a, sign_mask);
        v16u8 value_bits = __msa_and_v(value_mask, (v16u8) f64x2_set(1.0));
        return (v2f64) __msa_or_v(value_bits, sign_bits);
    }

    static inline f64x2 add(f64x2 a, f64x2 b)
    {
        return __msa_fadd_d(a, b);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b)
    {
        return __msa_fsub_d(a, b);
    }

    static inline f64x2 mul(f64x2 a, f64x2 b)
    {
        return __msa_fmul_d(a, b);
    }

    static inline f64x2 div(f64x2 a, f64x2 b)
    {
        return __msa_fdiv_d(a, b);
    }

    static inline f64x2 div(f64x2 a, f64 b)
    {
        return __msa_fdiv_d(a, f64x2_set(b));
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
        return __msa_fmadd_d(a, b, c);
    }

    static inline f64x2 msub(f64x2 a, f64x2 b, f64x2 c)
    {
        // b * c - a
        return neg(__msa_fmsub_d(a, b, c));
    }

    static inline f64x2 nmadd(f64x2 a, f64x2 b, f64x2 c)
    {
        // a - b * c
        return __msa_fmsub_d(a, b, c);
    }

    static inline f64x2 nmsub(f64x2 a, f64x2 b, f64x2 c)
    {
        // -(a + b * c)
        return neg(__msa_fmadd_w(a, b, c));
    }

    static inline f64x2 lerp(f64x2 a, f64x2 b, f64x2 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f64x2 rcp(f64x2 a)
    {
        return __msa_frcp_d(a);
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        return __msa_frsqrt_d(a);
    }

#else

    static inline f64x2 rcp(f64x2 a)
    {
        auto estimate = __msa_frcp_d(a);
        auto temp = __msa_fmul_d(a, estimate);
        return __msa_fmul_d(estimate, __msa_fsub_d(f64x2_set(2.0), temp));
    }

    static inline f64x2 rsqrt(f64x2 a)
    {
        auto n = __msa_frsqrt_d(a);
        auto e = __msa_fmul_d(__msa_fmul_d(n, n), a);
        n = __msa_fmul_d(f64x2_set(0.5), n);
        e = __msa_fsub_d(f64x2_set(3.0), e);
        return __msa_fmul_d(n, e);
    }

#endif

    static inline f64x2 sqrt(f64x2 a)
    {
        return __msa_fsqrt_d(a);
    }

    static inline f64 dot2(f64x2 a, f64x2 b)
    {
        f64x2 xy = __msa_fmul_d(a, b);
        f64x2 yx = shuffle<1, 0>(xy);
        f64x2 s = __msa_fadd_d(xy, yx);
        return get_component<0>(s);
    }

    // compare

    static inline mask64x2 compare_neq(f64x2 a, f64x2 b)
    {
        return (v2u64) __msa_fcune_d(a, b);
    }

    static inline mask64x2 compare_eq(f64x2 a, f64x2 b)
    {
        return (v2u64) __msa_fceq_d(a, b);
    }

    static inline mask64x2 compare_lt(f64x2 a, f64x2 b)
    {
        return (v2u64) __msa_fclt_d(a, b);
    }

    static inline mask64x2 compare_le(f64x2 a, f64x2 b)
    {
        return (v2u64) __msa_fcle_d(a, b);
    }

    static inline mask64x2 compare_gt(f64x2 a, f64x2 b)
    {
        return (v2u64) __msa_fclt_d(b, a);
    }

    static inline mask64x2 compare_ge(f64x2 a, f64x2 b)
    {
        return (v2u64) __msa_fcle_d(b, a);
    }

    static inline f64x2 select(mask64x2 mask, f64x2 a, f64x2 b)
    {
        return (v2f64) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    // rounding

    static inline f64x2 round(f64x2 s)
    {
        v2i64 temp = __msa_ftint_s_d(s);
        return __msa_ffint_s_d(temp);
    }

    static inline f64x2 trunc(f64x2 s)
    {
        v2i64 temp = __msa_ftrunc_s_d(s);
        return __msa_ffint_s_d(temp);
    }

    static inline f64x2 floor(f64x2 s)
    {
        f64x2 temp = round(s);
        v16u8 mask = (v16u8) __msa_fclt_d(s, temp);
        v16u8 one = (v16u8) __msa_fill_d(0x3ff0000000000000);
        return __msa_fsub_d(temp, (f64x2) __msa_and_v(mask, one));
    }

    static inline f64x2 ceil(f64x2 s)
    {
        f64x2 temp = round(s);
        v16u8 mask = (v16u8) __msa_fclt_d(temp, s);
        v16u8 one = (v16u8) __msa_fill_d(0x3ff0000000000000);
        return __msa_fadd_d(temp, (f64x2) __msa_and_v(mask, one));
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
