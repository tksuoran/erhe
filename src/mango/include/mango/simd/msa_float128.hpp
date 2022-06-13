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

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f32x4 shuffle(f32x4 a, f32x4 b)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const v4i32 control = (v4i32) { x, y, z + 4, w + 4 };
        return (v4f32) __msa_vshf_w(control, (v4i32) a, (v4i32) b);
    }

    template <u32 x, u32 y, u32 z, u32 w>
    static inline f32x4 shuffle(f32x4 v)
    {
        static_assert(x < 4 && y < 4 && z < 4 && w < 4, "Index out of range.");
        const v4i32 control = (v4i32) { x, y, z, w };
        return (v4f32) __msa_vshf_w(control, (v4i32) v, (v4i32) v);
    }

    template <>
    inline f32x4 shuffle<0, 1, 2, 3>(f32x4 v)
    {
        // .xyzw
        return v;
    }

    // indexed access

    template <unsigned int Index>
    static inline f32x4 set_component(f32x4 a, f32 s)
    {
        static_assert(Index < 4, "Index out of range.");
        s32 temp;
        std::memcpy(&temp, &s, 4);
        return (v4f32) __msa_insert_w((v4i32) a, Index, temp);
    }

    template <int Index>
    static inline f32 get_component(f32x4 a)
    {
        static_assert(Index < 4, "Index out of range.");
        u32 temp = __msa_copy_u_w((v4u32) a, Index);
        f32 s;
        std::memcpy(&s, &temp, 4);
        return s;
    }

    static inline f32x4 f32x4_zero()
    {
        return (v4f32) { 0.0f, 0.0f, 0.0f, 0.0f };
    }

    static inline f32x4 f32x4_set(f32 s)
    {
        return (v4f32) { s, s, s, s };
    }

    static inline f32x4 f32x4_set(f32 x, f32 y, f32 z, f32 w)
    {
        return (v4f32) { x, y, z, w };
    }

    static inline f32x4 f32x4_uload(const f32* source)
    {
        //return v4f32(__msa_ld_w(source, 0));
        return reinterpret_cast<const v4f32 *>(source)[0];
    }

    static inline void f32x4_ustore(f32* dest, f32x4 a)
    {
        //__msa_st_w(v4i32(a), dest, 0);
        reinterpret_cast<v4f32 *>(dest)[0] = a;
    }

    static inline f32x4 movelh(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_ilvl_d((v2i64) b, (v2i64) a);
    }

    static inline f32x4 movehl(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_ilvr_d((v2i64) b, (v2i64) a);
    }

    static inline f32x4 unpackhi(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_ilvr_w((v4i32) b, (v4i32) a);
    }

    static inline f32x4 unpacklo(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_ilvl_w((v4i32) b, (v4i32) a);
    }

    // bitwise

    static inline f32x4 bitwise_nand(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_and_v((v16u8)a, __msa_nor_v((v16u8)b, (v16u8)b));
    }

    static inline f32x4 bitwise_and(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_and_v((v16u8) a, (v16u8) b);
    }

    static inline f32x4 bitwise_or(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_or_v((v16u8) a, (v16u8) b);
    }

    static inline f32x4 bitwise_xor(f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_xor_v((v16u8) a, (v16u8) b);
    }

    static inline f32x4 bitwise_not(f32x4 a)
    {
        return (v4f32) __msa_nor_v((v16u8) a, (v16u8) a);
    }

    static inline f32x4 min(f32x4 a, f32x4 b)
    {
        return __msa_fmin_w(a, b);
    }

    static inline f32x4 max(f32x4 a, f32x4 b)
    {
        return __msa_fmax_w(a, b);
    }

    static inline f32x4 hmin(f32x4 a)
    {
        auto temp = __msa_fmin_w(a, shuffle<2, 3, 0, 1>(a, a));
        return __msa_fmin_w(temp, shuffle<1, 0, 3, 2>(temp, temp));
    }

    static inline f32x4 hmax(f32x4 a)
    {
        auto temp = __msa_fmax_w(a, shuffle<2, 3, 0, 1>(a, a));
        return __msa_fmax_w(temp, shuffle<1, 0, 3, 2>(temp, temp));
    }

    static inline f32x4 abs(f32x4 a)
    {
        return __msa_fmax_a_w(a, a);
    }

    static inline f32x4 neg(f32x4 a)
    {
        return __msa_fsub_w(f32x4_zero(), a);
    }

    static inline f32x4 sign(f32x4 a)
    {
        v16u8 sign_mask = (v16u8) f32x4_set(-0.0f);
        v16u8 value_mask = (v16u8) __msa_fcune_w(a, f32x4_zero());
        v16u8 sign_bits = __msa_and_v((v16u8) a, sign_mask);
        v16u8 value_bits = __msa_and_v(value_mask, (v16u8) f32x4_set(1.0f));
        return (v4f32) __msa_or_v(value_bits, sign_bits);
    }

    static inline f32x4 add(f32x4 a, f32x4 b)
    {
        return __msa_fadd_w(a, b);
    }

    static inline f32x4 sub(f32x4 a, f32x4 b)
    {
        return __msa_fsub_w(a, b);
    }

    static inline f32x4 mul(f32x4 a, f32x4 b)
    {
        return __msa_fmul_w(a, b);
    }

    static inline f32x4 div(f32x4 a, f32x4 b)
    {
        return __msa_fdiv_w(a, b);
    }

    static inline f32x4 div(f32x4 a, f32 b)
    {
        return __msa_fdiv_w(a, f32x4_set(b));
    }

    static inline f32x4 hadd(f32x4 a, f32x4 b)
    {
        return add(shuffle<0, 2, 0, 2>(a, b),
                   shuffle<1, 3, 1, 3>(a, b));
    }

    static inline f32x4 hsub(f32x4 a, f32x4 b)
    {
        return sub(shuffle<0, 2, 0, 2>(a, b),
                   shuffle<1, 3, 1, 3>(a, b));
    }

    static inline f32x4 madd(f32x4 a, f32x4 b, f32x4 c)
    {
        // a + b * c
        return __msa_fmadd_w(a, b, c);
    }

    static inline f32x4 msub(f32x4 a, f32x4 b, f32x4 c)
    {
        // b * c - a
        return __msa_fsub_w(f32x4_zero(), __msa_fmsub_w(a, b, c));
    }

    static inline f32x4 nmadd(f32x4 a, f32x4 b, f32x4 c)
    {
        // a - b * c
        return __msa_fmsub_w(a, b, c);
    }

    static inline f32x4 nmsub(f32x4 a, f32x4 b, f32x4 c)
    {
        // -(a + b * c)
        return __msa_fsub_w(f32x4_zero(), __msa_fmadd_w(a, b, c));
    }

    static inline f32x4 lerp(f32x4 a, f32x4 b, f32x4 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f32x4 rcp(f32x4 a)
    {
        return __msa_frcp_w(a);
    }

    static inline f32x4 rsqrt(f32x4 a)
    {
        return __msa_frsqrt_w(a);
    }

#else

    static inline f32x4 rcp(f32x4 a)
    {
        auto estimate = __msa_frcp_w(a);
        auto temp = __msa_fmul_w(a, estimate);
        return __msa_fmul_w(estimate, __msa_fsub_w(f32x4_set(2.0f), temp));
    }

    static inline f32x4 rsqrt(f32x4 a)
    {
        f32x4 n = __msa_frsqrt_w(a);
        f32x4 e = __msa_fmul_w(__msa_fmul_w(n, n), a);
        n = __msa_fmul_w(f32x4_set(0.5f), n);
        e = __msa_fsub_w(f32x4_set(3.0f), e);
        return __msa_fmul_w(n, e);
    }

#endif

    static inline f32x4 sqrt(f32x4 a)
    {
        return __msa_fsqrt_w(a);
    }

    static inline f32 dot3(f32x4 a, f32x4 b)
    {
        f32x4 s = mul(a, b);
        s = add(shuffle<0, 0, 0, 0>(s),
            add(shuffle<1, 1, 1, 1>(s), shuffle<2, 2, 2, 2>(s)));
        return get_component<0>(f32x4(s));
    }

    static inline f32 dot4(f32x4 a, f32x4 b)
    {
        f32x4 s = mul(a, b);
        s = add(s, shuffle<2, 3, 0, 1>(s));
        s = add(s, shuffle<1, 0, 3, 2>(s));
        return get_component<0>(s);
    }

    static inline f32x4 cross3(f32x4 a, f32x4 b)
    {
        f32x4 c = sub(mul(a, shuffle<1, 2, 0, 3>(b)),
                          mul(b, shuffle<1, 2, 0, 3>(a)));
        return shuffle<1, 2, 0, 3>(c);
    }

    // compare

    static inline mask32x4 compare_neq(f32x4 a, f32x4 b)
    {
        return (v4u32) __msa_fcune_w(a, b);
    }

    static inline mask32x4 compare_eq(f32x4 a, f32x4 b)
    {
        return (v4u32) __msa_fceq_w(a, b);
    }

    static inline mask32x4 compare_lt(f32x4 a, f32x4 b)
    {
        return (v4u32) __msa_fclt_w(a, b);
    }

    static inline mask32x4 compare_le(f32x4 a, f32x4 b)
    {
        return (v4u32) __msa_fcle_w(a, b);
    }

    static inline mask32x4 compare_gt(f32x4 a, f32x4 b)
    {
        return (v4u32) __msa_fclt_w(b, a);
    }

    static inline mask32x4 compare_ge(f32x4 a, f32x4 b)
    {
        return (v4u32) __msa_fcle_w(b, a);
    }

    static inline f32x4 select(mask32x4 mask, f32x4 a, f32x4 b)
    {
        return (v4f32) __msa_bsel_v((v16u8) mask, (v16u8) a, (v16u8) b);
    }

    // rounding

    static inline f32x4 round(f32x4 s)
    {
        v4i32 temp = __msa_ftint_s_w(s);
        return __msa_ffint_s_w(temp);
    }

    static inline f32x4 trunc(f32x4 s)
    {
        v4i32 temp = __msa_ftrunc_s_w(s);
        return __msa_ffint_s_w(temp);
    }

    static inline f32x4 floor(f32x4 s)
    {
        f32x4 temp = round(s);
        v16u8 mask = (v16u8) __msa_fclt_w(s, temp);
        v16u8 one = (v16u8) __msa_fill_w(0x3f800000);
        return __msa_fsub_w(temp, (f32x4) __msa_and_v(mask, one));
    }

    static inline f32x4 ceil(f32x4 s)
    {
        f32x4 temp = round(s);
        v16u8 mask = (v16u8) __msa_fclt_w(temp, s);
        v16u8 one = (v16u8) __msa_fill_w(0x3f800000);
        return __msa_fadd_w(temp, (f32x4) __msa_and_v(mask, one));
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
