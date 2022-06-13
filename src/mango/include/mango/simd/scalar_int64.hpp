/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/scalar_detail.hpp>

namespace mango::simd
{

    // -----------------------------------------------------------------
    // u32x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline u32x2 set_component(u32x2 a, u32 s)
    {
        static_assert(Index < 2, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline u32 get_component(u32x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return a[Index];
    }

    static inline u32x2 u32x2_zero()
    {
        return detail::scalar_set<u32, 2>(0);
    }

    static inline u32x2 u32x2_set(u32 s)
    {
        return detail::scalar_set<u32, 2>(s);
    }

    static inline u32x2 u32x2_set(u32 x, u32 y)
    {
        return {{ x, y }};
    }

    static inline u32x2 u32x2_uload(const u32* s)
    {
        return u32x2_set(s[0], s[1]);
    }

    static inline void u32x2_ustore(u32* dest, u32x2 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline u32x2 add(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline u32x2 sub(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline u32x2 bitwise_nand(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline u32x2 bitwise_and(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline u32x2 bitwise_or(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline u32x2 bitwise_xor(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline u32x2 bitwise_not(u32x2 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    static inline u32x2 min(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline u32x2 max(u32x2 a, u32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

    // -----------------------------------------------------------------
    // s32x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s32x2 set_component(s32x2 a, s32 s)
    {
        static_assert(Index < 2, "Index out of range.");
        a[Index] = s;
        return a;
    }

    template <unsigned int Index>
    static inline s32 get_component(s32x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return a[Index];
    }

    static inline s32x2 s32x2_zero()
    {
        return detail::scalar_set<s32, 2>(0);
    }

    static inline s32x2 s32x2_set(s32 s)
    {
        return detail::scalar_set<s32, 2>(s);
    }

    static inline s32x2 s32x2_set(s32 x, s32 y)
    {
        return {{ x, y }};
    }

    static inline s32x2 s32x2_uload(const s32* s)
    {
        return s32x2_set(s[0], s[1]);
    }

    static inline void s32x2_ustore(s32* dest, s32x2 a)
    {
        dest[0] = a[0];
        dest[1] = a[1];
    }

    static inline s32x2 add(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_add, a, b);
    }

    static inline s32x2 sub(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_sub, a, b);
    }

    static inline s32x2 bitwise_nand(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_nand, a, b);
    }

    static inline s32x2 bitwise_and(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_and, a, b);
    }

    static inline s32x2 bitwise_or(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_or, a, b);
    }

    static inline s32x2 bitwise_xor(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_xor, a, b);
    }

    static inline s32x2 bitwise_not(s32x2 a)
    {
        return detail::scalar_unroll(detail::scalar_not, a);
    }

    static inline s32x2 min(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_min, a, b);
    }

    static inline s32x2 max(s32x2 a, s32x2 b)
    {
        return detail::scalar_unroll(detail::scalar_max, a, b);
    }

} // namespace mango::simd
