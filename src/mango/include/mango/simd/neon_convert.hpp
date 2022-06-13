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
            uint32x4_t data;

            reinterpret_vector() = default;

            reinterpret_vector(s8x16 v)
                : data(vreinterpretq_u32_s8(v))
            {
            }

            reinterpret_vector(s16x8 v)
                : data(vreinterpretq_u32_s16(v))
            {
            }

            reinterpret_vector(s32x4 v)
                : data(vreinterpretq_u32_s32(v))
            {
            }

            reinterpret_vector(s64x2 v)
                : data(vreinterpretq_u32_s64(v))
            {
            }

            reinterpret_vector(u8x16 v)
                : data(vreinterpretq_u32_u8(v))
            {
            }

            reinterpret_vector(u16x8 v)
                : data(vreinterpretq_u32_u16(v))
            {
            }

            reinterpret_vector(u32x4 v)
                : data(v)
            {
            }

            reinterpret_vector(u64x2 v)
                : data(vreinterpretq_u32_u64(v))
            {
            }

            reinterpret_vector(f32x4 v)
                : data(vreinterpretq_u32_f32(v))
            {
            }

#ifdef __aarch64__

            reinterpret_vector(f64x2 v)
                : data(vreinterpretq_u32_f64(v))
            {
            }

#else

            reinterpret_vector(f64x2 v)
            {
                std::memcpy(&data, &v, 16);
            }

#endif

            operator s8x16 ()
            {
                return vreinterpretq_s8_u32(data);
            }

            operator s16x8 ()
            {
                return vreinterpretq_s16_u32(data);
            }

            operator s32x4 ()
            {
                return vreinterpretq_s32_u32(data);
            }

            operator s64x2 ()
            {
                return vreinterpretq_s64_u32(data);
            }

            operator u8x16 ()
            {
                return vreinterpretq_u8_u32(data);
            }

            operator u16x8 ()
            {
                return vreinterpretq_u16_u32(data);
            }

            operator u32x4 ()
            {
                return data;
            }

            operator u64x2 ()
            {
                return vreinterpretq_u64_u32(data);
            }

            operator f32x4 ()
            {
                return vreinterpretq_f32_u32(data);
            }

#ifdef __aarch64__

            operator f64x2 ()
            {
                return vreinterpretq_f64_u32(data);
            }

#else

            operator f64x2 ()
            {
                f64x2 temp;
                std::memcpy(&temp, &data, 16);
                return temp;
            }

#endif
        };

        template <>
        struct reinterpret_vector<256>
        {
            reinterpret_vector<128> lo;
            reinterpret_vector<128> hi;

            reinterpret_vector() = default;

            template <typename T>
            reinterpret_vector(composite_vector<T> v)
                : lo(v.lo)
                , hi(v.hi)
            {
            }

            reinterpret_vector(f64x4 v)
            {
                std::memcpy(this, &v, 32);
            }

            template <typename T>
            operator composite_vector<T> ()
            {
                return composite_vector<T>(lo, hi);
            }

            operator f64x4 ()
            {
                f64x4 temp;
                std::memcpy(&temp, this, 32);
                return temp;
            }
        };

        template <>
        struct reinterpret_vector<512>
        {
            reinterpret_vector<256> lo;
            reinterpret_vector<256> hi;

            reinterpret_vector() = default;

            template <typename T>
            reinterpret_vector(composite_vector<T> v)
                : lo(v.lo)
                , hi(v.hi)
            {
            }

            reinterpret_vector(f64x8 v)
            {
                std::memcpy(this, &v, 64);
            }

            template <typename T>
            operator composite_vector<T> ()
            {
                return composite_vector<T>(lo, hi);
            }

            operator f64x8 ()
            {
                f64x8 temp;
                std::memcpy(&temp, this, 64);
                return temp;
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

	template <typename D, typename S0, int S1>
	inline D reinterpret(scalar_vector<S0, S1> s)
	{
        static_assert(sizeof(scalar_vector<S0, S1>) == sizeof(D), "Vectors must be same size.");
		return D(detail::reinterpret_vector<scalar_vector<S0, S1>::vector_bits>(s));
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
        return vmovl_u8(vget_low_u8(s));
    }

    static inline u32x4 extend32x4(u8x16 s)
    {
        const auto temp16 = vmovl_u8(vget_low_u8(s));
        return vmovl_u16(vget_low_u16(temp16));
    }

    static inline u64x2 extend64x2(u8x16 s)
    {
        const auto temp16 = vmovl_u8(vget_low_u8(s));
        const auto temp32 = vmovl_u16(vget_low_u16(temp16));
        return vmovl_u32(vget_low_u32(temp32));
    }

    static inline u32x4 extend32x4(u16x8 s)
    {
        return vmovl_u16(vget_low_u16(s));
    }

    static inline u64x2 extend64x2(u16x8 s)
    {
        const auto temp32 = vmovl_u16(vget_low_u16(s));
        return vmovl_u32(vget_low_u32(temp32));
    }

    static inline u64x2 extend64x2(u32x4 s)
    {
        return vmovl_u32(vget_low_u32(s));
    }

    // 256 <- 128

    static inline u16x16 extend16x16(u8x16 s)
    {
        u16x16 result;
        result.lo = vmovl_u8(vget_low_u8(s));
        result.hi = vmovl_u8(vget_high_u8(s));
        return result;
    }

    static inline u32x8 extend32x8(u8x16 s)
    {
        const auto temp16x8 = vmovl_u8(vget_low_u8(s));
        u32x8 result;
        result.lo = vmovl_u16(vget_low_u16(temp16x8));
        result.hi = vmovl_u16(vget_high_u16(temp16x8));
        return result;
    }

    static inline u64x4 extend64x4(u8x16 s)
    {
        const auto temp16x8 = vmovl_u8(vget_low_u8(s));
        const auto temp32x4 = vmovl_u16(vget_low_u16(temp16x8));
        u64x4 result;
        result.lo = vmovl_u32(vget_low_u32(temp32x4));
        result.hi = vmovl_u32(vget_high_u32(temp32x4));
        return result;
    }

    static inline u32x8 extend32x8(u16x8 s)
    {
        u32x8 result;
        result.lo = vmovl_u16(vget_low_u16(s));
        result.hi = vmovl_u16(vget_high_u16(s));
        return result;
    }

    static inline u64x4 extend64x4(u16x8 s)
    {
        const auto temp32x4 = vmovl_u16(vget_low_u16(s));
        u64x4 result;
        result.lo = vmovl_u32(vget_low_u32(temp32x4));
        result.hi = vmovl_u32(vget_high_u32(temp32x4));
        return result;
    }

    static inline u64x4 extend64x4(u32x4 s)
    {
        u64x4 result;
        result.lo = vmovl_u32(vget_low_u32(s));
        result.hi = vmovl_u32(vget_high_u32(s));
        return result;
    }

    // -----------------------------------------------------------------
    // sign extend
    // -----------------------------------------------------------------

    // 128 <- 128

    static inline s16x8 extend16x8(s8x16 s)
    {
        return vmovl_s8(vget_low_s8(s));
    }

    static inline s32x4 extend32x4(s8x16 s)
    {
        const auto temp16 = vmovl_s8(vget_low_s8(s));
        return vmovl_s16(vget_low_s16(temp16));
    }

    static inline s64x2 extend64x2(s8x16 s)
    {
        const auto temp16 = vmovl_s8(vget_low_s8(s));
        const auto temp32 = vmovl_s16(vget_low_s16(temp16));
        return vmovl_s32(vget_low_s32(temp32));
    }

    static inline s32x4 extend32x4(s16x8 s)
    {
        return vmovl_s16(vget_low_s16(s));
    }

    static inline s64x2 extend64x2(s16x8 s)
    {
        const auto temp32 = vmovl_s16(vget_low_s16(s));
        return vmovl_s32(vget_low_s32(temp32));
    }

    static inline s64x2 extend64x2(s32x4 s)
    {
        return vmovl_s32(vget_low_s32(s));
    }

    // 256 <- 128

    static inline s16x16 extend16x16(s8x16 s)
    {
        s16x16 result;
        result.lo = vmovl_s8(vget_low_s8(s));
        result.hi = vmovl_s8(vget_high_s8(s));
        return result;
    }

    static inline s32x8 extend32x8(s8x16 s)
    {
        const auto temp16x8 = vmovl_s8(vget_low_s8(s));
        s32x8 result;
        result.lo = vmovl_s16(vget_low_s16(temp16x8));
        result.hi = vmovl_s16(vget_high_s16(temp16x8));
        return result;
    }

    static inline s64x4 extend64x4(s8x16 s)
    {
        const auto temp16x8 = vmovl_s8(vget_low_s8(s));
        const auto temp32x4 = vmovl_s16(vget_low_s16(temp16x8));
        s64x4 result;
        result.lo = vmovl_s32(vget_low_s32(temp32x4));
        result.hi = vmovl_s32(vget_high_s32(temp32x4));
        return result;
    }

    static inline s32x8 extend32x8(s16x8 s)
    {
        s32x8 result;
        result.lo = vmovl_s16(vget_low_s16(s));
        result.hi = vmovl_s16(vget_high_s16(s));
        return result;
    }

    static inline s64x4 extend64x4(s16x8 s)
    {
        const auto temp32x4 = vmovl_s16(vget_low_s16(s));
        s64x4 result;
        result.lo = vmovl_s32(vget_low_s32(temp32x4));
        result.hi = vmovl_s32(vget_high_s32(temp32x4));
        return result;
    }

    static inline s64x4 extend64x4(s32x4 s)
    {
        s64x4 result;
        result.lo = vmovl_s32(vget_low_s32(s));
        result.hi = vmovl_s32(vget_high_s32(s));
        return result;
    }

    // -----------------------------------------------------------------
    // narrow
    // -----------------------------------------------------------------

    static inline u8x16 narrow(u16x8 a, u16x8 b)
    {
        return vcombine_u8(vqmovn_u16(a), vqmovn_u16(b));
    }

    static inline u16x8 narrow(u32x4 a, u32x4 b)
    {
        return vcombine_u16(vqmovn_u32(a), vqmovn_u32(b));
    }

    static inline s8x16 narrow(s16x8 a, s16x8 b)
    {
        return vcombine_s8(vqmovn_s16(a), vqmovn_s16(b));
    }

    static inline s16x8 narrow(s32x4 a, s32x4 b)
    {
        return vcombine_s16(vqmovn_s32(a), vqmovn_s32(b));
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
        return vcvtq_f32_u32(s);
    }

    template <>
    inline f32x4 convert<f32x4>(s32x4 s)
    {
        return vcvtq_f32_s32(s);
    }

#if __ARM_ARCH >= 8 //&& !defined(MANGO_COMPILER_CLANG)

    template <>
    inline u32x4 convert<u32x4>(f32x4 s)
    {
        return vcvtnq_u32_f32(s);
    }

    template <>
    inline s32x4 convert<s32x4>(f32x4 s)
    {
        return vcvtnq_s32_f32(s);
    }

#else

    template <>
    inline u32x4 convert<u32x4>(f32x4 s)
    {
        return vcvtq_u32_f32(round(s));
    }

    template <>
    inline s32x4 convert<s32x4>(f32x4 s)
    {
        return vcvtq_s32_f32(round(s));
    }

#endif

    template <>
    inline s32x4 truncate<s32x4>(f32x4 s)
    {
        return vcvtq_s32_f32(s);
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
        return { a, b };
    }

#ifdef __aarch64__

    // 256 <- 128

    template <>
    inline f64x4 convert<f64x4>(s32x4 s)
    {
        f64x4 result;
        result.lo = vcvtq_f64_s64(vmovl_s32(vget_low_s32(s)));
        result.hi = vcvtq_f64_s64(vmovl_s32(vget_high_s32(s)));
        return result;
    }

    template <>
    inline f64x4 convert<f64x4>(u32x4 s)
    {
        f64x4 result;
        result.lo = vcvtq_f64_u64(vmovl_u32(vget_low_u32(s)));
        result.hi = vcvtq_f64_u64(vmovl_u32(vget_high_u32(s)));
        return result;
    }

    template <>
    inline f64x4 convert<f64x4>(f32x4 s)
    {
        f64x4 result;
        result.lo = vcvt_f64_f32(vget_low_f32(s));
        result.hi = vcvt_high_f64_f32(s);
        return result;
    }

    // 128 <- 256

    template <>
    inline s32x4 truncate<s32x4>(f64x4 s)
    {
        int64x2_t lo = vcvtnq_s64_f64(vrndq_f64(s.lo));
        int64x2_t hi = vcvtnq_s64_f64(vrndq_f64(s.hi));
        return vcombine_s32(vqmovn_s64(lo), vqmovn_s64(hi));
    }

    template <>
    inline s32x4 convert<s32x4>(f64x4 s)
    {
        int64x2_t lo = vcvtnq_s64_f64(s.lo);
        int64x2_t hi = vcvtnq_s64_f64(s.hi);
        return vcombine_s32(vqmovn_s64(lo), vqmovn_s64(hi));
    }

    template <>
    inline u32x4 convert<u32x4>(f64x4 s)
    {
        uint64x2_t lo = vcvtnq_u64_f64(s.lo);
        uint64x2_t hi = vcvtnq_u64_f64(s.hi);
        return vcombine_u32(vqmovn_u64(lo), vqmovn_u64(hi));
    }

    template <>
    inline f32x4 convert<f32x4>(f64x4 s)
    {
        float32x2_t lo = vcvt_f32_f64(s.lo);
        float32x2_t hi = vcvt_f32_f64(s.hi);
        return vcombine_f32(lo, hi);
    }

    template <>
    inline f32x4 convert<f32x4>(s64x4 s)
    {
        float64x2_t lo64 = vcvtq_f64_s64(s.lo);
        float64x2_t hi64 = vcvtq_f64_s64(s.hi);
        float32x2_t lo32 = vcvt_f32_f64(lo64);
        float32x2_t hi32 = vcvt_f32_f64(hi64);
        return vcombine_f32(lo32, hi32);
    }

    template <>
    inline f32x4 convert<f32x4>(u64x4 s)
    {
        float64x2_t lo64 = vcvtq_f64_u64(s.lo);
        float64x2_t hi64 = vcvtq_f64_u64(s.hi);
        float32x2_t lo32 = vcvt_f32_f64(lo64);
        float32x2_t hi32 = vcvt_f32_f64(hi64);
        return vcombine_f32(lo32, hi32);
    }

    // 128 <- 128

    template <>
    inline s64x2 convert<s64x2>(f64x2 v)
    {
        return vcvtnq_s64_f64(v);
    }

    template <>
    inline u64x2 convert<u64x2>(f64x2 v)
    {
        return vcvtnq_u64_f64(v);
    }

    template <>
    inline s64x2 truncate<s64x2>(f64x2 v)
    {
        return vcvtq_s64_f64(v);
    }

    template <>
    inline u64x2 truncate<u64x2>(f64x2 v)
    {
        return vcvtq_u64_f64(v);
    }

    template <>
    inline f64x2 convert<f64x2>(s64x2 v)
    {
        return vcvtq_f64_s64(v);
    }

    template <>
    inline f64x2 convert<f64x2>(u64x2 v)
    {
        return vcvtq_f64_u64(v);
    }

#else

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
    inline f64x4 convert<f64x4>(u32x4 s)
    {
        f64 x = unsignedIntToDouble(get_component<0>(s));
        f64 y = unsignedIntToDouble(get_component<1>(s));
        f64 z = unsignedIntToDouble(get_component<2>(s));
        f64 w = unsignedIntToDouble(get_component<3>(s));
        return f64x4_set(x, y, z, w);
    }

    template <>
    inline f64x4 convert<f64x4>(f32x4 s)
    {
        f64 x = f64(get_component<0>(s));
        f64 y = f64(get_component<1>(s));
        f64 z = f64(get_component<2>(s));
        f64 w = f64(get_component<3>(s));
        return f64x4_set(x, y, z, w);
    }

    // 128 <- 256

    template <>
    inline s32x4 truncate<s32x4>(f64x4 s)
    {
        s32 x = s32(get_component<0>(s.lo));
        s32 y = s32(get_component<1>(s.lo));
        s32 z = s32(get_component<0>(s.hi));
        s32 w = s32(get_component<1>(s.hi));
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline s32x4 convert<s32x4>(f64x4 s)
    {
        s32 x = s32(get_component<0>(s.lo) + 0.5);
        s32 y = s32(get_component<1>(s.lo) + 0.5);
        s32 z = s32(get_component<0>(s.hi) + 0.5);
        s32 w = s32(get_component<1>(s.hi) + 0.5);
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline u32x4 convert<u32x4>(f64x4 d)
    {
        u32 x = doubleToUnsignedInt(get_component<0>(d.lo));
        u32 y = doubleToUnsignedInt(get_component<1>(d.lo));
        u32 z = doubleToUnsignedInt(get_component<0>(d.hi));
        u32 w = doubleToUnsignedInt(get_component<1>(d.hi));
        return u32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(f64x4 s)
    {
        f32 x = f32(get_component<0>(s.lo));
        f32 y = f32(get_component<1>(s.lo));
        f32 z = f32(get_component<0>(s.hi));
        f32 w = f32(get_component<1>(s.hi));
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(s64x4 s)
    {
        f32 x = f32(get_component<0>(s.lo));
        f32 y = f32(get_component<1>(s.lo));
        f32 z = f32(get_component<0>(s.hi));
        f32 w = f32(get_component<1>(s.hi));
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(u64x4 s)
    {
        f32 x = f32(get_component<0>(s.lo));
        f32 y = f32(get_component<1>(s.lo));
        f32 z = f32(get_component<0>(s.hi));
        f32 w = f32(get_component<1>(s.hi));
        return f32x4_set(x, y, z, w);
    }

    // 128 <- 128

    template <>
    inline s64x2 convert<s64x2>(f64x2 v)
    {
        s64 x = s64(get_component<0>(v) + 0.5);
        s64 y = s64(get_component<1>(v) + 0.5);
        return s64x2_set(x, y);
    }

    template <>
    inline u64x2 convert<u64x2>(f64x2 v)
    {
        u64 x = u64(get_component<0>(v) + 0.5);
        u64 y = u64(get_component<1>(v) + 0.5);
        return u64x2_set(x, y);
    }

    template <>
    inline s64x2 truncate<s64x2>(f64x2 v)
    {
        v = trunc(v);
        s64 x = s64(get_component<0>(v));
        s64 y = s64(get_component<1>(v));
        return s64x2_set(x, y);
    }

    template <>
    inline u64x2 truncate<u64x2>(f64x2 v)
    {
        v = trunc(v);
        u64 x = u64(get_component<0>(v));
        u64 y = u64(get_component<1>(v));
        return u64x2_set(x, y);
    }

    template <>
    inline f64x2 convert<f64x2>(s64x2 v)
    {
        f64 x = f64(get_component<0>(v));
        f64 y = f64(get_component<1>(v));
        return f64x2_set(x, y);
    }

    template <>
    inline f64x2 convert<f64x2>(u64x2 v)
    {
        f64 x = f64(get_component<0>(v));
        f64 y = f64(get_component<1>(v));
        return f64x2_set(x, y);
    }

#endif

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

#ifdef MANGO_ENABLE_ARM_FP16

    template <>
    inline f32x4 convert<f32x4>(f16x4 s)
    {
        return vcvt_f32_f16(s);
    }

    template <>
    inline f16x4 convert<f16x4>(f32x4 s)
    {
        return vcvt_f16_f32(s);
    }

#else

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

#endif // MANGO_ENABLE_ARM_FP16

} // namespace mango::simd
