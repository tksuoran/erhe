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
        return vset_lane_u32(s, a, Index);
    }

    template <unsigned int Index>
    static inline u32 get_component(u32x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return vget_lane_u32(a, Index);
    }

    static inline u32x2 u32x2_zero()
    {
        return vdup_n_u32(0);
    }

    static inline u32x2 u32x2_set(u32 s)
    {
        return vdup_n_u32(s);
    }

    static inline u32x2 u32x2_set(u32 x, u32 y)
    {
        uint32x2_t temp = { x, y };
        return temp;
    }

#if defined(MANGO_COMPILER_GCC)

    static inline u32x2 u32x2_uload(const u32* s)
    {
        u32x2 temp;
        std::memcpy(&temp, source, sizeof(temp));
        return temp;
    }

    static inline void u32x2_ustore(u32* dest, u32x2 a)
    {
        std::memcpy(dest, &a, sizeof(a));
    }

#else

    static inline u32x2 u32x2_uload(const u32* s)
    {
        return vld1_u32(s);
    }

    static inline void u32x2_ustore(u32* dest, u32x2 a)
    {
        vst1_u32(dest, a);
    }

#endif

    static inline u32x2 add(u32x2 a, u32x2 b)
    {
        return vadd_u32(a, b);
    }

    static inline u32x2 sub(u32x2 a, u32x2 b)
    {
        return vsub_u32(a, b);
    }

    static inline u32x2 bitwise_nand(u32x2 a, u32x2 b)
    {
        return vbic_u32(b, a);
    }

    static inline u32x2 bitwise_and(u32x2 a, u32x2 b)
    {
        return vand_u32(a, b);
    }

    static inline u32x2 bitwise_or(u32x2 a, u32x2 b)
    {
        return vorr_u32(a, b);
    }

    static inline u32x2 bitwise_xor(u32x2 a, u32x2 b)
    {
        return veor_u32(a, b);
    }

    static inline u32x2 bitwise_not(u32x2 a)
    {
        return vmvn_u32(a);
    }

    static inline u32x2 min(u32x2 a, u32x2 b)
    {
        return vmin_u32(a, b);
    }

    static inline u32x2 max(u32x2 a, u32x2 b)
    {
        return vmax_u32(a, b);
    }

    // -----------------------------------------------------------------
    // s32x2
    // -----------------------------------------------------------------

    template <unsigned int Index>
    static inline s32x2 set_component(s32x2 a, s32 s)
    {
        static_assert(Index < 2, "Index out of range.");
        return vset_lane_s32(s, a, Index);
    }

    template <unsigned int Index>
    static inline s32 get_component(s32x2 a)
    {
        static_assert(Index < 2, "Index out of range.");
        return vget_lane_s32(a, Index);
    }

    static inline s32x2 s32x2_zero()
    {
        return vdup_n_s32(0);
    }

    static inline s32x2 s32x2_set(s32 s)
    {
        return vdup_n_s32(s);
    }

    static inline s32x2 s32x2_set(s32 x, s32 y)
    {
        int32x2_t temp = { x, y };
        return temp;
    }

#if defined(MANGO_COMPILER_GCC)

    static inline s32x2 s32x2_uload(const s32* s)
    {
        s32x2 temp;
        std::memcpy(&temp, s, sizeof(temp));
        return temp;
    }

    static inline void s32x2_ustore(s32* dest, s32x2 a)
    {
        std::memcpy(dest, &a, sizeof(a));
    }

#else

    static inline s32x2 s32x2_uload(const s32* s)
    {
        return vld1_s32(s);
    }

    static inline void s32x2_ustore(s32* dest, s32x2 a)
    {
        vst1_s32(dest, a);
    }

#endif

    static inline s32x2 add(s32x2 a, s32x2 b)
    {
        return vadd_s32(a, b);
    }

    static inline s32x2 sub(s32x2 a, s32x2 b)
    {
        return vsub_s32(a, b);
    }

    static inline s32x2 bitwise_nand(s32x2 a, s32x2 b)
    {
        return vbic_s32(b, a);
    }

    static inline s32x2 bitwise_and(s32x2 a, s32x2 b)
    {
        return vand_s32(a, b);
    }

    static inline s32x2 bitwise_or(s32x2 a, s32x2 b)
    {
        return vorr_s32(a, b);
    }

    static inline s32x2 bitwise_xor(s32x2 a, s32x2 b)
    {
        return veor_s32(a, b);
    }

    static inline s32x2 bitwise_not(s32x2 a)
    {
        return vmvn_s32(a);
    }

    static inline s32x2 min(s32x2 a, s32x2 b)
    {
        return vmin_s32(a, b);
    }

    static inline s32x2 max(s32x2 a, s32x2 b)
    {
        return vmax_s32(a, b);
    }

} // namespace mango::simd
