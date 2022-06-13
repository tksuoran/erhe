/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // f32x2
    // -----------------------------------------------------------------

    template <u32 x, u32 y>
    static inline f32x2 shuffle(f32x2 v)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        float32x2_t result;
	    result = vmov_n_f32(vget_lane_f32(v, x));
	    result = vset_lane_f32(vget_lane_f32(v, y), result, 1);
        return result;
    }

    template <u32 x, u32 y>
    static inline f32x2 shuffle(f32x2 a, f32x2 b)
    {
        static_assert(x < 2 && y < 2, "Index out of range.");
        float32x2_t result;
	    result = vmov_n_f32(vget_lane_f32(a, x));
	    result = vset_lane_f32(vget_lane_f32(b, y), result, 1);
        return result;
    }

    // indexed access

    template <unsigned int Index>
    static inline f32x2 set_component(f32x2 a, f32 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return vset_lane_f32(s, a, Index);
    }

    template <unsigned int Index>
    static inline f32 get_component(f32x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return vget_lane_f32(a, Index);
    }

    static inline f32x2 f32x2_zero()
    {
        return vdup_n_f32(0.0f);
    }

    static inline f32x2 f32x2_set(f32 s)
    {
        return vdup_n_f32(s);
    }

    static inline f32x2 f32x2_set(f32 x, f32 y)
    {
        float32x2_t temp = { x, y };
        return temp;
    }

#if defined(MANGO_COMPILER_GCC)

    static inline f32x2 f32x2_uload(const f32* source)
    {
        f32x2 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void f32x2_ustore(f32* dest, f32x2 a)
    {
        std::memcpy(dest, &a, sizeof(a));
    }

#else

    static inline f32x2 f32x2_uload(const f32* source)
    {
        return vld1_f32(source);
    }

    static inline void f32x2_ustore(f32* dest, f32x2 a)
    {
        vst1_f32(dest, a);
    }

#endif

    static inline f32x2 unpacklo(f32x2 a, f32x2 b)
    {
        float32x2x2_t v = vzip_f32(a, b);
        return v.val[1];
    }

    static inline f32x2 unpackhi(f32x2 a, f32x2 b)
    {
        float32x2x2_t v = vzip_f32(a, b);
        return v.val[0];
    }

    // bitwise

    static inline f32x2 bitwise_nand(f32x2 a, f32x2 b)
    {
        return vreinterpret_f32_s32(vbic_s32(vreinterpret_s32_f32(a), vreinterpret_s32_f32(b)));
    }

    static inline f32x2 bitwise_and(f32x2 a, f32x2 b)
    {
        return vreinterpret_f32_s32(vand_s32(vreinterpret_s32_f32(a), vreinterpret_s32_f32(b)));
    }

    static inline f32x2 bitwise_or(f32x2 a, f32x2 b)
    {
        return vreinterpret_f32_s32(vorr_s32(vreinterpret_s32_f32(a), vreinterpret_s32_f32(b)));
    }

    static inline f32x2 bitwise_xor(f32x2 a, f32x2 b)
    {
        return vreinterpret_f32_s32(veor_s32(vreinterpret_s32_f32(a), vreinterpret_s32_f32(b)));
    }

    static inline f32x2 bitwise_not(f32x2 a)
    {
        return vreinterpret_f32_u32(veor_u32(vreinterpret_u32_f32(a), vceq_f32(a, a)));
    }

    static inline f32x2 min(f32x2 a, f32x2 b)
    {
        return vmin_f32(a, b);
    }

    static inline f32x2 max(f32x2 a, f32x2 b)
    {
        return vmax_f32(a, b);
    }

    static inline f32x2 abs(f32x2 a)
    {
        return vabs_f32(a);
    }

    static inline f32x2 neg(f32x2 a)
    {
        return vneg_f32(a);
    }

    static inline f32x2 sign(f32x2 a)
    {
        auto i = vreinterpret_u32_f32(a);
        auto value_mask = vmvn_u32(vceq_u32(i, vdup_n_u32(0)));
        auto sign_bits = vand_u32(i, vdup_n_u32(0x80000000));
        auto value_bits = vand_u32(vdup_n_u32(0x3f800000), value_mask);
        return vreinterpret_f32_u32(vorr_u32(value_bits, sign_bits));
    }

    static inline f32x2 add(f32x2 a, f32x2 b)
    {
        return vadd_f32(a, b);
    }

    static inline f32x2 sub(f32x2 a, f32x2 b)
    {
        return vsub_f32(a, b);
    }

    static inline f32x2 mul(f32x2 a, f32x2 b)
    {
        return vmul_f32(a, b);
    }

#ifdef __aarch64__

    static inline f32x2 div(f32x2 a, f32x2 b)
    {
        return vdiv_f32(a, b);
    }

    static inline f32x2 div(f32x2 a, f32 b)
    {
        f32x2 s = vdup_n_f32(b);
        return vdiv_f32(a, s);
    }

#else

    static inline f32x2 div(f32x2 a, f32x2 b)
    {
        f32x2 n = vrecpe_f32(b);
        n = vmul_f32(vrecps_f32(n, b), n);
        n = vmul_f32(vrecps_f32(n, b), n);
        return vmul_f32(a, n);
    }

    static inline f32x2 div(f32x2 a, f32 b)
    {
        f32x2 s = vdup_n_f32(b);
        f32x2 n = vrecpe_f32(s);
        n = vmul_f32(vrecps_f32(n, s), n);
        n = vmul_f32(vrecps_f32(n, s), n);
        return vmul_f32(a, n);
    }

#endif

    static inline f32x2 hadd(f32x2 a, f32x2 b)
    {
        return vpadd_f32(a, b);
    }

    static inline f32x2 hsub(f32x2 a, f32x2 b)
    {
        b = vneg_f32(b);
        return vpadd_f32(a, b);
    }

    static inline f32x2 madd(f32x2 a, f32x2 b, f32x2 c)
    {
        // a + b * c
        return vmla_f32(a, b, c);
    }

    static inline f32x2 msub(f32x2 a, f32x2 b, f32x2 c)
    {
        // b * c - a
        return vneg_f32(vmls_f32(a, b, c));
    }

    static inline f32x2 nmadd(f32x2 a, f32x2 b, f32x2 c)
    {
        // a - b * c
        return vmls_f32(a, b, c);
    }

    static inline f32x2 nmsub(f32x2 a, f32x2 b, f32x2 c)
    {
        // -(a + b * c)
        return vneg_f32(vmla_f32(a, b, c));
    }

    static inline f32x2 lerp(f32x2 a, f32x2 b, f32x2 s)
    {
        // a * (1.0 - s) + b * s
        // (a - a * s) + (b * s)
        return madd(nmadd(a, a, s), b, s);
    }

#if defined(MANGO_FAST_MATH)

    static inline f32x2 rcp(f32x2 a)
    {
        f32x2 e = vrecpe_f32(a);
        e = vmul_f32(vrecps_f32(a, e), e);
        return e;
    }

    static inline f32x2 rsqrt(f32x2 a)
    {
        f32x2 e = vrsqrte_f32(a);
        e = vmul_f32(vrsqrts_f32(vmul_f32(a, e), e), e);
        return e;
    }

#ifdef __aarch64__

    static inline f32x2 sqrt(f32x2 a)
    {
        return vsqrt_f32(a);
    }

#else

    static inline f32x2 sqrt(f32x2 a)
    {
        f32x2 e = vrsqrte_f32(a);
        e = vmul_f32(vrsqrts_f32(vmul_f32(a, e), e), e);
        return vmul_f32(a, e);
    }

#endif

#else // MANGO_FAST_MATH

    static inline f32x2 rcp(f32x2 a)
    {
        f32x2 e = vrecpe_f32(a);
        e = vmul_f32(vrecps_f32(a, e), e);
        e = vmul_f32(vrecps_f32(a, e), e);
        return e;
    }

    static inline f32x2 rsqrt(f32x2 a)
    {
        f32x2 e = vrsqrte_f32(a);
        e = vmul_f32(vrsqrts_f32(vmul_f32(a, e), e), e);
        e = vmul_f32(vrsqrts_f32(vmul_f32(a, e), e), e);
        return e;
    }

#ifdef __aarch64__
    
    static inline f32x2 sqrt(f32x2 a)
    {
        return vsqrt_f32(a);
    }

#else

    static inline f32x2 sqrt(f32x2 a)
    {
        f32x2 e = vrsqrte_f32(a);
        e = vmul_f32(vrsqrts_f32(vmul_f32(a, e), e), e);
        e = vmul_f32(vrsqrts_f32(vmul_f32(a, e), e), e);
        return vmul_f32(a, e);
    }

#endif

#endif // MANGO_FAST_MATH

#ifdef __aarch64__

    static inline f32 dot2(f32x2 a, f32x2 b)
    {
        float32x2_t prod = vmul_f32(a, b);
        return vaddv_f32(prod);
    }

#else

    static inline f32 dot2(f32x2 a, f32x2 b)
    {
        const float32x2_t prod = vmul_f32(a, b);
        return vget_lane_f32(vpadd_f32(prod, prod), 0);
    }

#endif

    // rounding

#if __ARM_ARCH >= 8

    static inline f32x2 round(f32x2 s)
    {
        return vrnda_f32(s);
    }

    static inline f32x2 trunc(f32x2 s)
    {
        return vrnd_f32(s);
    }

    static inline f32x2 floor(f32x2 s)
    {
        return vrndm_f32(s);
    }

    static inline f32x2 ceil(f32x2 s)
    {
        return vrndp_f32(s);
    }

#else

    static inline f32x2 round(f32x2 s)
    {
        float32x2_t magic = vdup_n_f32(12582912.0f); // 1.5 * (1 << 23)
        float32x2_t result = vsub_f32(vadd_f32(s, magic), magic);
        uint32x2_t mask = vcle_f32(vabs_f32(s), vreinterpret_f32_u32(vdup_n_u32(0x4b000000)));
        return vbsl_f32(mask, result, s);
    }

    static inline f32x2 trunc(f32x2 s)
    {
        int32x2_t truncated = vcvt_s32_f32(s);
        float32x2_t result = vcvt_f32_s32(truncated);
        uint32x2_t mask = vcle_f32(vabs_f32(s), vreinterpret_f32_u32(vdup_n_u32(0x4b000000)));
        return vbsl_f32(mask, result, s);
    }

    static inline f32x2 floor(f32x2 s)
    {
        const f32x2 temp = round(s);
        const uint32x2_t mask = vclt_f32(s, temp);
        const uint32x2_t one = vdup_n_u32(0x3f800000);
        return vsub_f32(temp, vreinterpret_f32_u32(vand_u32(mask, one)));
    }

    static inline f32x2 ceil(f32x2 s)
    {
        const f32x2 temp = round(s);
        const uint32x2_t mask = vcgt_f32(s, temp);
        const uint32x2_t one = vdup_n_u32(0x3f800000);
        return vadd_f32(temp, vreinterpret_f32_u32(vand_u32(mask, one)));
    }

#endif

    static inline f32x2 fract(f32x2 s)
    {
        return sub(s, floor(s));
    }

} // namespace mango::simd
