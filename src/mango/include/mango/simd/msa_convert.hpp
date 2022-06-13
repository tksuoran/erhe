/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>
#include <mango/simd/common.hpp>

namespace mango::simd
{
    namespace detail
    {

        template <int bits>
        struct reinterpret_vector;

        template <>
        struct reinterpret_vector<128>
        {
            u32x4::vector data;

            reinterpret_vector() = default;

            reinterpret_vector(s8x16 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(s16x8 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(s32x4 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(s64x2 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(u8x16 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(u16x8 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(u32x4 v)
                : data(v.data)
            {
            }

            reinterpret_vector(u64x2 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(f32x4 v)
                : data((u32x4::vector) v.data)
            {
            }

            reinterpret_vector(f64x2 v)
            {
                std::memcpy(&data, &v, 16);
            }

            operator s8x16 ()
            {
                return (s8x16::vector) data;
            }

            operator s16x8 ()
            {
                return (s16x8::vector) data;
            }

            operator s32x4 ()
            {
                return (s32x4::vector) data;
            }

            operator s64x2 ()
            {
                return (s64x2::vector) data;
            }

            operator u8x16 ()
            {
                return (u8x16::vector) data;
            }

            operator u16x8 ()
            {
                return (u16x8::vector) data;
            }

            operator u32x4 ()
            {
                return data;
            }

            operator u64x2 ()
            {
                return (u64x2::vector) data;
            }

            operator f32x4 ()
            {
                return (f32x4::vector) data;
            }

            operator f64x2 ()
            {
                return (f64x2::vector) data;
            }
        };

        template <>
        struct reinterpret_vector<256>
        {
            reinterpret_vector<128> lo;
            reinterpret_vector<128> hi;

            template <typename T>
            reinterpret_vector(composite_vector<T> v)
                : lo(v.lo)
                , hi(v.hi)
            {
            }

            template <typename T>
            operator composite_vector<T> ()
            {
                return composite_vector<T>(lo, hi);
            }
        };

        template <>
        struct reinterpret_vector<512>
        {
            reinterpret_vector<256> lo;
            reinterpret_vector<256> hi;

            template <typename T>
            reinterpret_vector(composite_vector<T> v)
                : lo(v.lo)
                , hi(v.hi)
            {
            }

            template <typename T>
            operator composite_vector<T> ()
            {
                return composite_vector<T>(lo, hi);
            }
        };

    } // namespace detail

    // -----------------------------------------------------------------
    // reinterpret
    // -----------------------------------------------------------------

	template <typename D, typename S0, int S1, typename S2>
	inline D reinterpret(hardware_vector<S0, S1, S2> s)
	{
        static_assert(sizeof(hardware_vector<S0, S1, S2>) == sizeof(D), "Vectors must be same size.");
		return D(detail::reinterpret_vector<hardware_vector<S0, S1, S2>::vector_bits>(s));
	}

	template <typename D, typename S>
	inline D reinterpret(composite_vector<S> s)
	{
        static_assert(sizeof(composite_vector<S>) == sizeof(D), "Vectors must be same size.");
		return D(detail::reinterpret_vector<composite_vector<S>::vector_bits>(s));
	}

    // -----------------------------------------------------------------
    // convert
    // -----------------------------------------------------------------

	template <typename D, typename S>
	inline D convert(S)
	{
		D::undefined_conversion();
	}

	template <typename D, typename S>
	inline D truncate(S)
	{
		D::undefined_conversion();
	}

    // -----------------------------------------------------------------
    // zero extend
    // -----------------------------------------------------------------

    // 128 <- 128

    static inline u16x8 extend16x8(u8x16 s)
    {
        return (v16u8) __msa_ilvr_b(__msa_fill_b(0), (v16i8)s);
    }

    static inline u32x4 extend32x4(u8x16 s)
    {
        v8u16 temp16x8 = __msa_ilvr_b(__msa_fill_b(0), (v16i8)s);
        return (v4u32) __msa_ilvr_h(__msa_fill_h(0), (v8i16)temp16x8);
    }

    static inline u64x2 extend64x2(u8x16 s)
    {
        v8u16 temp16x8 = __msa_ilvr_b(__msa_fill_b(0), (v16i8)s);
        v4u32 temp32x4 = __msa_ilvr_h(__msa_fill_h(0), (v8i16)temp16x8);
        return (v2u64) __msa_ilvr_w(__msa_fill_w(0), (v4i32)temp32x4);
    }

    static inline u32x4 extend32x4(u16x8 s)
    {
        return (v4u32) __msa_ilvr_h(__msa_fill_h(0), (v8i16)s);
    }

    static inline u64x2 extend64x2(u16x8 s)
    {
        v4u32 temp32x4 = __msa_ilvr_h(__msa_fill_h(0), (v8i16)s);
        return (v2u64) __msa_ilvr_w(__msa_fill_w(0), (v4i32)temp32x4);
    }

    static inline u64x2 extend64x2(u32x4 s)
    {
        return (v2u64) __msa_ilvr_w(__msa_fill_w(0), (v4i32)s);
    }

    // 256 <- 128

    static inline u16x16 extend16x16(u8x16 s)
    {
        u8x16 high = (v16u8) __msa_ilvl_d((v2i64)s, (v2i64)s);

        u16x16 result;
        result.lo = extend16x8(s);
        result.hi = extend16x8(high);
        return result;
    }

    static inline u32x8 extend32x8(u16x8 s)
    {
        u16x8 high = (v8u16) __msa_ilvl_d((v2i64)s, (v2i64)s);

        u32x8 result;
        result.lo = extend32x4(s);
        result.hi = extend32x4(high);
        return result;
    }

    static inline u32x8 extend32x8(u8x16 s)
    {
        const u16x8 temp = extend16x8(s);
        return extend32x8(temp);
    }

    static inline u64x4 extend64x4(u32x4 s)
    {
        u32x4 high = (v4u32) __msa_ilvl_d((v2i64)s, (v2i64)s);

        u64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

    static inline u64x4 extend64x4(u16x8 s)
    {
        const u32x4 temp = extend32x4(s);
        return extend64x4(temp);
    }

    static inline u64x4 extend64x4(u8x16 s)
    {
        const u16x8 temp16x8 = extend16x8(s);
        const u32x4 temp32x4 = extend32x4(temp16x8);
        return extend64x4(temp32x4);
    }

    // -----------------------------------------------------------------
    // sign extend
    // -----------------------------------------------------------------

    // 128 <- 128

    static inline s16x8 extend16x8(s8x16 s)
    {
        v16i8 sign = __msa_clt_s_b(s, __msa_fill_b(0));
        return (v16i8) __msa_ilvr_b(sign, (v16i8)s);
    }

    static inline s32x4 extend32x4(s8x16 s)
    {
        v16i8 temp16x8 = __msa_ilvr_b(__msa_clt_s_b(s, __msa_fill_b(0)), (v16i8)s);
        return (v4i32) __msa_ilvr_h(__msa_clt_s_h(s, __msa_fill_h(0)), (v8i16)temp16x8);
    }

    static inline s64x2 extend64x2(s8x16 s)
    {
        v16i8 sign = __msa_clt_s_h(s, __msa_fill_b(0));
        const auto temp16x8 = (v8i16) __msa_ilvr_b(sign, (v16i8)s);
        const auto temp32x4 = (v4i32) __msa_ilvr_h((v8i16)sign, temp16x8);
        return (v2i64) __msa_ilvr_w((v4i32)sign, temp32x4);
    }

    static inline s32x4 extend32x4(s16x8 s)
    {
        v8i16 sign = __msa_clt_s_h(s, __msa_fill_h(0));
        return (v4i32) __msa_ilvr_h(sign, (v8i16)s);
    }

    static inline s64x2 extend64x2(s16x8 s)
    {
        v8i16 sign = __msa_clt_s_h(s, __msa_fill_h(0));
        const auto temp32x4 = (v4i32) __msa_ilvr_h(sign, (v8i16)s);
        return (v2i64) __msa_ilvr_w((v4i32)sign, temp32x4);
    }

    static inline s64x2 extend64x2(s32x4 s)
    {
        v4i32 sign = __msa_clt_s_w(s, __msa_fill_w(0));
        return (v2i64) __msa_ilvr_w(sign, (v4i32)s);
    }

    // 256 <- 128

    static inline s16x16 extend16x16(s8x16 s)
    {
        s8x16 high = (v16i8) __msa_ilvl_d((v2i64)s, (v2i64)s);

        s16x16 result;
        result.lo = extend16x8(s);
        result.hi = extend16x8(high);
        return result;
    }

    static inline s32x8 extend32x8(s16x8 s)
    {
        s16x8 high = (v8i16) __msa_ilvl_d((v2i64)s, (v2i64)s);

        s32x8 result;
        result.lo = extend32x4(s);
        result.hi = extend32x4(high);
        return result;
    }

    static inline s32x8 extend32x8(s8x16 s)
    {
        const s16x8 temp = extend16x8(s);
        return extend32x8(temp);
    }

    static inline s64x4 extend64x4(s32x4 s)
    {
        s32x4 high = (v4i32) __msa_ilvl_d((v2i64)s, (v2i64)s);

        s64x4 result;
        result.lo = extend64x2(s);
        result.hi = extend64x2(high);
        return result;
    }

    static inline s64x4 extend64x4(s16x8 s)
    {
        const s32x4 temp = extend32x4(s);
        return extend64x4(temp);
    }

    static inline s64x4 extend64x4(s8x16 s)
    {
        const s16x8 temp16x8 = extend16x8(s);
        const s32x4 temp32x4 = extend32x4(temp16x8);
        return extend64x4(temp32x4);
    }

    // -----------------------------------------------------------------
    // narrow
    // -----------------------------------------------------------------

    static inline u8x16 narrow(u16x8 a, u16x8 b)
    {
        const v16i8 control = (v16i8) { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 };
        return (v16u8) __msa_vshf_b(control, (v16i8) a, (v16i8) b);
    }

    static inline u16x8 narrow(u32x4 a, u32x4 b)
    {
        const v8i16 control = (v8i16) { 0, 2, 4, 6, 8, 10, 12, 14 };
        return (v8u16) __msa_vshf_h(control, (v8i16) a, (v8i16) b);
    }

    static inline s8x16 narrow(s16x8 a, s16x8 b)
    {
        const v16i8 control = (v16i8) { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 };
        return (v16i8) __msa_vshf_b(control, (v16i8) a, (v16i8) b);
    }

    static inline s16x8 narrow(s32x4 a, s32x4 b)
    {
        const v8i16 control = (v8i16) { 0, 2, 4, 6, 8, 10, 12, 14 };
        return (v8i16) __msa_vshf_h(control, (v8i16) a, (v8i16) b);
    }

    // -----------------------------------------------------------------
    // u32
    // -----------------------------------------------------------------

    static inline u32x4 get_low(u32x8 a)
    {
        return a.lo;
    }

    static inline u32x4 get_high(u32x8 a)
    {
        return a.hi;
    }

    static inline u32x8 set_low(u32x8 a, u32x4 low)
    {
        a.lo = low;
        return a;
    }

    static inline u32x8 set_high(u32x8 a, u32x4 high)
    {
        a.hi = high;
        return a;
    }

    static inline u32x8 combine(u32x4 a, u32x4 b)
    {
        u32x8 v;
        v.lo = a;
        v.hi = b;
        return v;
    }

    // -----------------------------------------------------------------
    // s32
    // -----------------------------------------------------------------

    static inline s32x4 get_low(s32x8 a)
    {
        return a.lo;
    }

    static inline s32x4 get_high(s32x8 a)
    {
        return a.hi;
    }

    static inline s32x8 set_low(s32x8 a, s32x4 low)
    {
        a.lo = low;
        return a;
    }

    static inline s32x8 set_high(s32x8 a, s32x4 high)
    {
        a.hi = high;
        return a;
    }

    static inline s32x8 combine(s32x4 a, s32x4 b)
    {
        s32x8 v;
        v.lo = a;
        v.hi = b;
        return v;
    }

    // -----------------------------------------------------------------
    // f32
    // -----------------------------------------------------------------

    static inline f32x4 get_low(f32x8 a)
    {
        return a.lo;
    }

    static inline f32x4 get_high(f32x8 a)
    {
        return a.hi;
    }

    static inline f32x8 set_low(f32x8 a, f32x4 low)
    {
        a.lo = low;
        return a;
    }

    static inline f32x8 set_high(f32x8 a, f32x4 high)
    {
        a.hi = high;
        return a;
    }

    static inline f32x8 combine(f32x4 a, f32x4 b)
    {
        f32x8 result;
        result.lo = a;
        result.hi = b;
        return result;
    }

    // 128 bit convert

    template <>
    inline f32x4 convert<f32x4>(u32x4 s)
    {
        return __msa_ffint_u_w(s);
    }

    template <>
    inline f32x4 convert<f32x4>(s32x4 s)
    {
        return __msa_ffint_s_w(s);
    }

    template <>
    inline u32x4 convert<u32x4>(f32x4 s)
    {
        return __msa_ftint_u_w(s);
    }

    template <>
    inline s32x4 convert<s32x4>(f32x4 s)
    {
        return __msa_ftint_s_w(s);
    }

    template <>
    inline s32x4 truncate<s32x4>(f32x4 s)
    {
        return __msa_ftrunc_s_w(s);
    }

    // 256 bit convert

    template <>
    inline s32x8 convert<s32x8>(f32x8 s)
    {
        s32x8 result;
        result.lo = convert<s32x4>(s.lo);
        result.hi = convert<s32x4>(s.hi);
        return result;
    }

    template <>
    inline f32x8 convert<f32x8>(s32x8 s)
    {
        f32x8 result;
        result.lo = convert<f32x4>(s.lo);
        result.hi = convert<f32x4>(s.hi);
        return result;
    }

    template <>
    inline u32x8 convert<u32x8>(f32x8 s)
    {
        u32x8 result;
        result.lo = convert<u32x4>(s.lo);
        result.hi = convert<u32x4>(s.hi);
        return result;
    }

    template <>
    inline f32x8 convert<f32x8>(u32x8 s)
    {
        f32x8 result;
        result.lo = convert<f32x4>(s.lo);
        result.hi = convert<f32x4>(s.hi);
        return result;
    }

    template <>
    inline s32x8 truncate<s32x8>(f32x8 s)
    {
        s32x8 result;
        result.lo = truncate<s32x4>(s.lo);
        result.hi = truncate<s32x4>(s.hi);
        return result;
    }

    // 512 bit convert

    template <>
    inline s32x16 convert<s32x16>(f32x16 s)
    {
        s32x16 result;
        result.lo = convert<s32x8>(s.lo);
        result.hi = convert<s32x8>(s.hi);
        return result;
    }

    template <>
    inline f32x16 convert<f32x16>(s32x16 s)
    {
        f32x16 result;
        result.lo = convert<f32x8>(s.lo);
        result.hi = convert<f32x8>(s.hi);
        return result;
    }

    template <>
    inline u32x16 convert<u32x16>(f32x16 s)
    {
        u32x16 result;
        result.lo = convert<u32x8>(s.lo);
        result.hi = convert<u32x8>(s.hi);
        return result;
    }

    template <>
    inline f32x16 convert<f32x16>(u32x16 s)
    {
        f32x16 result;
        result.lo = convert<f32x8>(s.lo);
        result.hi = convert<f32x8>(s.hi);
        return result;
    }

    template <>
    inline s32x16 truncate<s32x16>(f32x16 s)
    {
        s32x16 result;
        result.lo = truncate<s32x8>(s.lo);
        result.hi = truncate<s32x8>(s.hi);
        return result;
    }

    // -----------------------------------------------------------------
    // f64
    // -----------------------------------------------------------------

    static inline f64x2 get_low(f64x4 a)
    {
        return a.lo;
    }

    static inline f64x2 get_high(f64x4 a)
    {
        return a.hi;
    }

    static inline f64x4 set_low(f64x4 a, f64x2 low)
    {
        a.lo = low;
        return a;
    }

    static inline f64x4 set_high(f64x4 a, f64x2 high)
    {
        a.hi = high;
        return a;
    }

    static inline f64x4 combine(f64x2 a, f64x2 b)
    {
        f64x4 result;
        result.lo = a;
        result.hi = b;
        return result;
    }

    // 256 <- 128

    template <>
    inline f64x4 convert<f64x4>(s32x4 s)
    {
        f64 x = f64(get_component<0>(s));
        f64 y = f64(get_component<1>(s));
        f64 z = f64(get_component<2>(s));
        f64 w = f64(get_component<3>(s));
        return f64x4_set(x, y, z, w);
    }

    template <>
    inline f64x4 convert<f64x4>(u32x4 ui)
    {
        f64 x = f64(get_component<0>(ui));
        f64 y = f64(get_component<1>(ui));
        f64 z = f64(get_component<2>(ui));
        f64 w = f64(get_component<3>(ui));
        return f64x4_set(x, y, z, w);
    }

    template <>
    inline f64x4 convert<f64x4>(f32x4 s)
    {
        f64x4 result;
        result.lo = __msa_fexupr_d(s);
        result.hi = __msa_fexupl_d(s);
        return result;
    }

    // 128 <- 256

    template <>
    inline s32x4 truncate<s32x4>(f64x4 s)
    {
        s64x2 lo = __msa_ftrunc_s_d(s.lo);
        s64x2 hi = __msa_ftrunc_s_d(s.hi);
        s32 x = s32(get_component<0>(lo));
        s32 y = s32(get_component<1>(lo));
        s32 z = s32(get_component<0>(hi));
        s32 w = s32(get_component<1>(hi));
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline s32x4 convert<s32x4>(f64x4 s)
    {
        s32 x = s32(get_component<0>(s));
        s32 y = s32(get_component<1>(s));
        s32 z = s32(get_component<2>(s));
        s32 w = s32(get_component<3>(s));
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline u32x4 convert<u32x4>(f64x4 d)
    {
        u32 x = u32(get_component<0>(d));
        u32 y = u32(get_component<1>(d));
        u32 z = u32(get_component<2>(d));
        u32 w = u32(get_component<3>(d));
        return u32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(f64x4 s)
    {
        return __msa_fexdo_w(s.lo, s.hi);
    }

    template <>
    inline f32x4 convert<f32x4>(s64x4 s)
    {
        f32 x = f32(get_component<0>(s));
        f32 y = f32(get_component<1>(s));
        f32 z = f32(get_component<2>(s));
        f32 w = f32(get_component<3>(s));
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(u64x4 s)
    {
        f32 x = f32(get_component<0>(s));
        f32 y = f32(get_component<1>(s));
        f32 z = f32(get_component<2>(s));
        f32 w = f32(get_component<3>(s));
        return f32x4_set(x, y, z, w);
    }

    // 128 <- 128

    template <>
    inline s64x2 convert<s64x2>(f64x2 v)
    {
        return __msa_ftint_s_d(v);
    }

    template <>
    inline u64x2 convert<u64x2>(f64x2 v)
    {
        return __msa_ftint_u_d(v);
    }

    template <>
    inline s64x2 truncate<s64x2>(f64x2 v)
    {
        return __msa_ftrunc_s_d(v);
    }

    template <>
    inline u64x2 truncate<u64x2>(f64x2 v)
    {
        return __msa_ftrunc_u_d(v);
    }

    template <>
    inline f64x2 convert<f64x2>(s64x2 v)
    {
        return __msa_ffint_s_d(v);
    }

    template <>
    inline f64x2 convert<f64x2>(u64x2 v)
    {
        return __msa_ffint_u_d(v);
    }

    // 256 <- 256

    template <>
    inline s64x4 convert<s64x4>(f64x4 v)
    {
        auto lo = convert<s64x2>(v.lo);
        auto hi = convert<s64x2>(v.hi);
        return { lo, hi };
    }

    template <>
    inline u64x4 convert<u64x4>(f64x4 v)
    {
        auto lo = convert<u64x2>(v.lo);
        auto hi = convert<u64x2>(v.hi);
        return { lo, hi };
    }

    template <>
    inline s64x4 truncate<s64x4>(f64x4 v)
    {
        auto lo = truncate<s64x2>(v.lo);
        auto hi = truncate<s64x2>(v.hi);
        return { lo, hi };
    }

    template <>
    inline u64x4 truncate<u64x4>(f64x4 v)
    {
        auto lo = truncate<u64x2>(v.lo);
        auto hi = truncate<u64x2>(v.hi);
        return { lo, hi };
    }

    template <>
    inline f64x4 convert<f64x4>(s64x4 v)
    {
        auto lo = convert<f64x2>(v.lo);
        auto hi = convert<f64x2>(v.hi);
        return { lo, hi };
    }

    template <>
    inline f64x4 convert<f64x4>(u64x4 v)
    {
        auto lo = convert<f64x2>(v.lo);
        auto hi = convert<f64x2>(v.hi);
        return { lo, hi };
    }

    // 512 <- 512

    template <>
    inline s64x8 convert<s64x8>(f64x8 v)
    {
        auto lo = convert<s64x4>(v.lo);
        auto hi = convert<s64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline u64x8 convert<u64x8>(f64x8 v)
    {
        auto lo = convert<u64x4>(v.lo);
        auto hi = convert<u64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline s64x8 truncate<s64x8>(f64x8 v)
    {
        auto lo = truncate<s64x4>(v.lo);
        auto hi = truncate<s64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline u64x8 truncate<u64x8>(f64x8 v)
    {
        auto lo = truncate<u64x4>(v.lo);
        auto hi = truncate<u64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline f64x8 convert<f64x8>(s64x8 v)
    {
        auto lo = convert<f64x4>(v.lo);
        auto hi = convert<f64x4>(v.hi);
        return { lo, hi };
    }

    template <>
    inline f64x8 convert<f64x8>(u64x8 v)
    {
        auto lo = convert<f64x4>(v.lo);
        auto hi = convert<f64x4>(v.hi);
        return { lo, hi };
    }

    // -----------------------------------------------------------------
    // f16
    // -----------------------------------------------------------------

    template <>
    inline f32x4 convert<f32x4>(f16x4 s)
    {
        f32 x = s[0];
        f32 y = s[1];
        f32 z = s[2];
        f32 w = s[3];
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f16x4 convert<f16x4>(f32x4 s)
    {
        f16x4 v;
        v[0] = get_component<0>(s);
        v[1] = get_component<1>(s);
        v[2] = get_component<2>(s);
        v[3] = get_component<3>(s);
        return v;
    }

} // namespace mango::simd
