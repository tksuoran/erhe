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
        return (u16x8::vector) vec_mergel(vec_xor(s.data, s.data), s.data);
    }

    static inline u32x4 extend32x4(u8x16 s)
    {
        auto temp16x8 = vec_mergel(vec_xor(s.data, s.data), s.data);
        return (u32x4::vector) vec_mergel(vec_xor(temp16x8, temp16x8), temp16x8);
    }

    static inline u64x2 extend64x2(u8x16 s)
    {
        auto temp16x8 = vec_mergel(vec_xor(s.data, s.data), s.data);
        auto temp32x4 = vec_mergel(vec_xor(temp16x8, temp16x8), temp16x8);
        return (u64x2::vector) vec_mergel(vec_xor(temp32x4, temp32x4), temp32x4);
    }

    static inline u32x4 extend32x4(u16x8 s)
    {
        return (u32x4::vector) vec_mergel(vec_xor(s.data, s.data), s.data);
    }

    static inline u64x2 extend64x2(u16x8 s)
    {
        auto temp32x4 = vec_mergel(vec_xor(s.data, s.data), s.data);
        return (u64x2::vector) vec_mergel(vec_xor(temp32x4, temp32x4), temp32x4);
    }

    static inline u64x2 extend64x2(u32x4 s)
    {
        return (u64x2::vector) vec_mergel(vec_xor(s.data, s.data), s.data);
    }

    // 256 <- 128

    static inline u16x16 extend16x16(u8x16 s)
    {
        u16x16 result;
        result.lo = vec_mergel(vec_xor(s.data, s.data), s.data);
        result.hi = vec_mergeh(vec_xor(s.data, s.data), s.data);
        return result;
    }

    static inline u32x8 extend32x8(u8x16 s)
    {
        u32x8 result;
        result.lo = vec_mergel(vec_xor(s.data, s.data), s.data);
        result.hi = vec_mergeh(vec_xor(s.data, s.data), s.data);
        return result;
    }

    static inline u64x4 extend64x4(u8x16 s)
    {
        auto temp16x8 = vec_mergel(vec_xor(s.data, s.data), s.data);
        auto temp32x4 = vec_mergel(vec_xor(temp16x8, temp16x8), temp16x8);

        // NOTE: _ARCH_PWR8 required
        u64x4 result;
        result.lo = vec_mergel(vec_xor(temp32x4, temp32x4), temp32x4);
        result.hi = vec_mergeh(vec_xor(temp32x4, temp32x4), temp32x4);
        return result;
    }

    static inline u32x8 extend32x8(u16x8 s)
    {
        u32x8 result;
        result.lo = vec_mergel(vec_xor(s.data, s.data), s.data);
        result.hi = vec_mergeh(vec_xor(s.data, s.data), s.data);
        return result;
    }

    static inline u64x4 extend64x4(u16x8 s)
    {
        auto temp32x4 = vec_mergel(vec_xor(s.data, s.data), s.data);

        // NOTE: _ARCH_PWR8 required
        u64x4 result;
        result.lo = vec_mergel(vec_xor(temp32x4, temp32x4), temp32x4);
        result.hi = vec_mergeh(vec_xor(temp32x4, temp32x4), temp32x4);
        return result;
    }

    static inline u64x4 extend64x4(u32x4 s)
    {
        // NOTE: _ARCH_PWR8 required
        u64x4 result;
        result.lo = vec_mergel(vec_xor(s.data, s.data), s.data);
        result.hi = vec_mergeh(vec_xor(s.data, s.data), s.data);
        return result;
    }

    // -----------------------------------------------------------------
    // sign extend
    // -----------------------------------------------------------------

    // 128 <- 128

    static inline s16x8 extend16x8(s8x16 s)
    {
        return vec_unpackl(s.data);
    }

    static inline s32x4 extend32x4(s8x16 s)
    {
        return vec_unpackl(vec_unpackl(s.data));
    }

    static inline s64x2 extend64x2(s8x16 s)
    {
        return vec_unpackl(vec_unpackl(vec_unpackl(s.data)));
    }

    static inline s32x4 extend32x4(s16x8 s)
    {
        return vec_unpackl(s.data);
    }

    static inline s64x2 extend64x2(s16x8 s)
    {
        return vec_unpackl(vec_unpackl(s.data));
    }

    static inline s64x2 extend64x2(s32x4 s)
    {
        return vec_unpackl(s.data);
    }

    // 256 <- 128

    static inline s16x16 extend16x16(s8x16 s)
    {
        s16x16 result;
        result.lo = vec_unpackl(s.data);
        result.hi = vec_unpackh(s.data);
        return result;
    }

    static inline s32x8 extend32x8(s8x16 s)
    {
        const auto temp16x8 = vec_unpackl(s.data);

        s32x8 result;
        result.lo = vec_unpackl(temp16x8);
        result.hi = vec_unpackh(temp16x8);
        return result;
    }

    static inline s64x4 extend64x4(s8x16 s)
    {
        const auto temp16x8 = vec_unpackl(s.data);
        const auto temp32x4 = vec_unpackl(temp16x8);

        s64x4 result;
        result.lo = vec_unpackl(temp32x4);
        result.hi = vec_unpackh(temp32x4);
        return result;
    }

    static inline s32x8 extend32x8(s16x8 s)
    {
        s32x8 result;
        result.lo = vec_unpackl(s.data);
        result.hi = vec_unpackh(s.data);
        return result;
    }

    static inline s64x4 extend64x4(s16x8 s)
    {
        const auto temp32x4 = vec_unpackl(s.data);

        s64x4 result;
        result.lo = vec_unpackl(temp32x4);
        result.hi = vec_unpackh(temp32x4);
        return result;
    }

    static inline s64x4 extend64x4(s32x4 s)
    {
        s64x4 result;
        result.lo = vec_unpackl(s.data);
        result.hi = vec_unpackh(s.data);
        return result;
    }

    // -----------------------------------------------------------------
    // narrow
    // -----------------------------------------------------------------

    static inline u8x16 narrow(u16x8 a, u16x8 b)
    {
        return vec_pack(a.data, b.data);
    }

    static inline u16x8 narrow(u32x4 a, u32x4 b)
    {
        return vec_pack(a.data, b.data);
    }

    static inline s8x16 narrow(s16x8 a, s16x8 b)
    {
        return vec_pack(a.data, b.data);
    }

    static inline s16x8 narrow(s32x4 a, s32x4 b)
    {
        return vec_pack(a.data, b.data);
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
        return vec_ctf(s.data, 0);
    }

    template <>
    inline f32x4 convert<f32x4>(s32x4 s)
    {
        return vec_ctf(s.data, 0);
    }

    template <>
    inline u32x4 convert<u32x4>(f32x4 s)
    {
        s = add(s.data, f32x4_set(0.5f).data);
        return vec_ctu(s.data, 0);
    }

    template <>
    inline s32x4 convert<s32x4>(f32x4 s)
    {
        s = add(s.data, f32x4_set(0.5f).data);
        return vec_cts(s.data, 0);
    }

    template <>
    inline s32x4 truncate<s32x4>(f32x4 s)
    {
        return vec_cts(s.data, 0);
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
    inline f64x4 convert<f64x4>(s32x4 v)
    {
        f64 x = f64(get_component<0>(v));
        f64 y = f64(get_component<1>(v));
        f64 z = f64(get_component<2>(v));
        f64 w = f64(get_component<3>(v));
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
        f64 x = f64(get_component<0>(s));
        f64 y = f64(get_component<1>(s));
        f64 z = f64(get_component<2>(s));
        f64 w = f64(get_component<3>(s));
        return f64x4_set(x, y, z, w);
    }

    // 128 <- 256

    template <>
    inline s32x4 truncate<s32x4>(f64x4 v)
    {
        s32 x = s32(get_component<0>(v));
        s32 y = s32(get_component<1>(v));
        s32 z = s32(get_component<2>(v));
        s32 w = s32(get_component<3>(v));
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline s32x4 convert<s32x4>(f64x4 s)
    {
        s32 x = s32(get_component<0>(s) + 0.5);
        s32 y = s32(get_component<1>(s) + 0.5);
        s32 z = s32(get_component<2>(s) + 0.5);
        s32 w = s32(get_component<3>(s) + 0.5);
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline u32x4 convert<u32x4>(f64x4 v)
    {
        u32 x = u32(get_component<0>(v) + 0.5);
        u32 y = u32(get_component<1>(v) + 0.5);
        u32 z = u32(get_component<2>(v) + 0.5);
        u32 w = u32(get_component<3>(v) + 0.5);
        return u32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(f64x4 s)
    {
        f32 x = f32(get_component<0>(s));
        f32 y = f32(get_component<1>(s));
        f32 z = f32(get_component<2>(s));
        f32 w = f32(get_component<3>(s));
        return f32x4_set(x, y, z, w);
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
    inline f32x4 convert<f32x4>(f16x4 h)
    {
        f32 x = f32(h[0]);
        f32 y = f32(h[1]);
        f32 z = f32(h[2]);
        f32 w = f32(h[3]);
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f16x4 convert<f16x4>(f32x4 f)
    {
        f16 x = f16(get_component<0>(f));
        f16 y = f16(get_component<1>(f));
        f16 z = f16(get_component<2>(f));
        f16 w = f16(get_component<3>(f));
        return {{ x, y, z, w }};
    }

} // namespace mango::simd
