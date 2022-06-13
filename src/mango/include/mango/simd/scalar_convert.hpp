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

        template <typename D, typename S>
        inline D extend(S s)
        {
            D temp;
            for (int i = 0; i < D::size; ++i)
            {
                temp[i] = s[i];
            }
            return temp;
        }

        template <typename D, typename S>
        inline D extend_composite(S s)
        {
            constexpr int halfsize = D::size / 2;
            D temp;
            for (int i = 0; i < halfsize; ++i)
            {
                temp.lo[i] = s[i + 0];
                temp.hi[i] = s[i + halfsize];
            }
            return temp;
        }

    } // namespace detail

    // -----------------------------------------------------------------
    // reinterpret
    // -----------------------------------------------------------------

    template <typename D, typename S0, int S1>
    inline D reinterpret(scalar_vector<S0, S1> s)
    {
        static_assert(sizeof(scalar_vector<S0, S1>) == sizeof(D), "Vectors must be same size.");
        D temp;
        std::memcpy(reinterpret_cast<void*>(&temp), &s, sizeof(D));
        return temp;
    }

    template <typename D, typename S>
    inline D reinterpret(composite_vector<S> s)
    {
        static_assert(sizeof(composite_vector<S>) == sizeof(D), "Vectors must be same size.");
        D temp;
        std::memcpy(reinterpret_cast<void*>(&temp), &s, sizeof(D));
        return temp;
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
        return detail::extend<u16x8, u8x16>(s);
    }

    static inline u32x4 extend32x4(u8x16 s)
    {
        return detail::extend<u32x4, u8x16>(s);
    }

    static inline u64x2 extend64x2(u8x16 s)
    {
        return detail::extend<u64x2, u8x16>(s);
    }

    static inline u32x4 extend32x4(u16x8 s)
    {
        return detail::extend<u32x4, u16x8>(s);
    }

    static inline u64x2 extend64x2(u16x8 s)
    {
        return detail::extend<u64x2, u16x8>(s);
    }

    static inline u64x2 extend64x2(u32x4 s)
    {
        return detail::extend<u64x2, u32x4>(s);
    }

    // 256 <- 128

    static inline u16x16 extend16x16(u8x16 s)
    {
        return detail::extend_composite<u16x16, u8x16>(s);
    }

    static inline u32x8 extend32x8(u8x16 s)
    {
        return detail::extend_composite<u32x8, u8x16>(s);
    }

    static inline u64x4 extend64x4(u8x16 s)
    {
        return detail::extend_composite<u64x4, u8x16>(s);
    }

    static inline u32x8 extend32x8(u16x8 s)
    {
        return detail::extend_composite<u32x8, u16x8>(s);
    }

    static inline u64x4 extend64x4(u16x8 s)
    {
        return detail::extend_composite<u64x4, u16x8>(s);
    }

    static inline u64x4 extend64x4(u32x4 s)
    {
        return detail::extend_composite<u64x4, u32x4>(s);
    }

    // -----------------------------------------------------------------
    // sign extend
    // -----------------------------------------------------------------

    // 128 <- 128

    static inline s16x8 extend16x8(s8x16 s)
    {
        return detail::extend<s16x8, s8x16>(s);
    }

    static inline s32x4 extend32x4(s8x16 s)
    {
        return detail::extend<s32x4, s8x16>(s);
    }

    static inline s64x2 extend64x2(s8x16 s)
    {
        return detail::extend<s64x2, s8x16>(s);
    }

    static inline s32x4 extend32x4(s16x8 s)
    {
        return detail::extend<s32x4, s16x8>(s);
    }

    static inline s64x2 extend64x2(s16x8 s)
    {
        return detail::extend<s64x2, s16x8>(s);
    }

    static inline s64x2 extend64x2(s32x4 s)
    {
        return detail::extend<s64x2, s32x4>(s);
    }

    // 256 <- 128

    static inline s16x16 extend16x16(s8x16 s)
    {
        return detail::extend_composite<s16x16, s8x16>(s);
    }

    static inline s32x8 extend32x8(s8x16 s)
    {
        return detail::extend_composite<s32x8, s8x16>(s);
    }

    static inline s64x4 extend64x4(s8x16 s)
    {
        return detail::extend_composite<s64x4, s8x16>(s);
    }

    static inline s32x8 extend32x8(s16x8 s)
    {
        return detail::extend_composite<s32x8, s16x8>(s);
    }

    static inline s64x4 extend64x4(s16x8 s)
    {
        return detail::extend_composite<s64x4, s16x8>(s);
    }

    static inline s64x4 extend64x4(s32x4 s)
    {
        return detail::extend_composite<s64x4, s32x4>(s);
    }

    // -----------------------------------------------------------------
    // narrow
    // -----------------------------------------------------------------

    static inline u8x16 narrow(u16x8 a, u16x8 b)
    {
        u8x16 v;
        for (int i = 0; i < 8; ++i)
        {
            v[i + 0] = u8(std::min(u16(0xff), a[i]));
            v[i + 8] = u8(std::min(u16(0xff), b[i]));
        }
        return v;
    }

    static inline u16x8 narrow(u32x4 a, u32x4 b)
    {
        u16x8 v;
        for (int i = 0; i < 4; ++i)
        {
            v[i + 0] = u16(std::min(u32(0xffff), a[i]));
            v[i + 4] = u16(std::min(u32(0xffff), b[i]));
        }
        return v;
    }

    static inline s8x16 narrow(s16x8 a, s16x8 b)
    {
        s8x16 v;
        for (int i = 0; i < 8; ++i)
        {
            v[i + 0] = s8(std::min(s16(0x7f), std::max(s16(0 - 0x80), a[i])));
            v[i + 8] = s8(std::min(s16(0x7f), std::max(s16(0 - 0x80), b[i])));
        }
        return v;
    }

    static inline s16x8 narrow(s32x4 a, s32x4 b)
    {
        s16x8 v;
        for (int i = 0; i < 4; ++i)
        {
            v[i + 0] = s16(std::min(s32(0x7fff), std::max(s32(0 - 0x8000), a[i])));
            v[i + 4] = s16(std::min(s32(0x7fff), std::max(s32(0 - 0x8000), b[i])));
        }
        return v;
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
        f32x8 v;
        v.lo = a;
        v.hi = b;
        return v;
    }

    // 128 bit convert

    template <>
    inline f32x4 convert<f32x4>(u32x4 s)
    {
        f32x4 v;
        v[0] = f32(s[0]);
        v[1] = f32(s[1]);
        v[2] = f32(s[2]);
        v[3] = f32(s[3]);
        return v;
    }

    template <>
    inline f32x4 convert<f32x4>(s32x4 s)
    {
        f32x4 v;
        v[0] = f32(s[0]);
        v[1] = f32(s[1]);
        v[2] = f32(s[2]);
        v[3] = f32(s[3]);
        return v;
    }

    template <>
    inline u32x4 convert<u32x4>(f32x4 s)
    {
        u32x4 v;
        v[0] = u32(s[0] + 0.5f);
        v[1] = u32(s[1] + 0.5f);
        v[2] = u32(s[2] + 0.5f);
        v[3] = u32(s[3] + 0.5f);
        return v;
    }

    template <>
    inline s32x4 convert<s32x4>(f32x4 s)
    {
        s32x4 v;
        v[0] = s32(s[0] + 0.5f);
        v[1] = s32(s[1] + 0.5f);
        v[2] = s32(s[2] + 0.5f);
        v[3] = s32(s[3] + 0.5f);
        return v;
    }

    template <>
    inline s32x4 truncate<s32x4>(f32x4 s)
    {
        s32x4 v;
        v[0] = s32(s[0]);
        v[1] = s32(s[1]);
        v[2] = s32(s[2]);
        v[3] = s32(s[3]);
        return v;
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
        f64x4 v;
        v.lo = a;
        v.hi = b;
        return v;
    }

    // 256 <- 128

    template <>
    inline f64x4 convert<f64x4>(s32x4 s)
    {
        f64x4 v;
        v.lo[0] = f64(s[0]);
        v.lo[1] = f64(s[1]);
        v.hi[0] = f64(s[2]);
        v.hi[1] = f64(s[3]);
        return v;
    }

    template <>
    inline f64x4 convert<f64x4>(u32x4 u)
    {
        f64x4 v;
        v.lo[0] = unsignedIntToDouble(u[0]);
        v.lo[1] = unsignedIntToDouble(u[1]);
        v.hi[0] = unsignedIntToDouble(u[2]);
        v.hi[1] = unsignedIntToDouble(u[3]);
        return v;
    }

    template <>
    inline f64x4 convert<f64x4>(f32x4 s)
    {
        f64x4 v;
        v.lo[0] = f64(s[0]);
        v.lo[1] = f64(s[1]);
        v.hi[0] = f64(s[2]);
        v.hi[1] = f64(s[3]);
        return v;
    }

    // 128 <- 256

    template <>
    inline s32x4 truncate<s32x4>(f64x4 s)
    {
        s32 x = s32(s.lo[0]);
        s32 y = s32(s.lo[1]);
        s32 z = s32(s.hi[0]);
        s32 w = s32(s.hi[1]);
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline s32x4 convert<s32x4>(f64x4 s)
    {
        s32 x = s32(s.lo[0] + 0.5);
        s32 y = s32(s.lo[1] + 0.5);
        s32 z = s32(s.hi[0] + 0.5);
        s32 w = s32(s.hi[1] + 0.5);
        return s32x4_set(x, y, z, w);
    }

    template <>
    inline u32x4 convert<u32x4>(f64x4 d)
    {
        u32 x = doubleToUnsignedInt(d.lo[0]);
        u32 y = doubleToUnsignedInt(d.lo[1]);
        u32 z = doubleToUnsignedInt(d.hi[0]);
        u32 w = doubleToUnsignedInt(d.hi[1]);
        return u32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(f64x4 s)
    {
        f32 x = f32(s.lo[0]);
        f32 y = f32(s.lo[1]);
        f32 z = f32(s.hi[0]);
        f32 w = f32(s.hi[1]);
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(s64x4 s)
    {
        f32 x = f32(s.lo[0]);
        f32 y = f32(s.lo[1]);
        f32 z = f32(s.hi[0]);
        f32 w = f32(s.hi[1]);
        return f32x4_set(x, y, z, w);
    }

    template <>
    inline f32x4 convert<f32x4>(u64x4 s)
    {
        f32 x = f32(s.lo[0]);
        f32 y = f32(s.lo[1]);
        f32 z = f32(s.hi[0]);
        f32 w = f32(s.hi[1]);
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
    inline f32x4 convert<f32x4>(f16x4 s)
    {
        f32x4 v;
        v[0] = s[0];
        v[1] = s[1];
        v[2] = s[2];
        v[3] = s[3];
        return v;
    }

    template <>
    inline f16x4 convert<f16x4>(f32x4 s)
    {
        f16x4 v;
        v[0] = s[0];
        v[1] = s[1];
        v[2] = s[2];
        v[3] = s[3];
        return v;
    }

} // namespace mango::simd
