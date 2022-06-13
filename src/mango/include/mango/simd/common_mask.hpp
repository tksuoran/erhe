
#ifdef SIMD_ZEROMASK_INT128

    // min

    static inline u8x16 min(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return select(mask, min(a, b), u8x16_zero());
    }

    static inline u16x8 min(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return select(mask, min(a, b), u16x8_zero());
    }

    static inline u32x4 min(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return select(mask, min(a, b), u32x4_zero());
    }

    static inline u64x2 min(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return select(mask, min(a, b), u64x2_zero());
    }

    static inline s8x16 min(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return select(mask, min(a, b), s8x16_zero());
    }

    static inline s16x8 min(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return select(mask, min(a, b), s16x8_zero());
    }

    static inline s32x4 min(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return select(mask, min(a, b), s32x4_zero());
    }

    static inline s64x2 min(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return select(mask, min(a, b), s64x2_zero());
    }

    // max

    static inline u8x16 max(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return select(mask, max(a, b), u8x16_zero());
    }

    static inline u16x8 max(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return select(mask, max(a, b), u16x8_zero());
    }

    static inline u32x4 max(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return select(mask, max(a, b), u32x4_zero());
    }

    static inline u64x2 max(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return select(mask, max(a, b), u64x2_zero());
    }

    static inline s8x16 max(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return select(mask, max(a, b), s8x16_zero());
    }

    static inline s16x8 max(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return select(mask, max(a, b), s16x8_zero());
    }

    static inline s32x4 max(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return select(mask, max(a, b), s32x4_zero());
    }

    static inline s64x2 max(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return select(mask, max(a, b), s64x2_zero());
    }

    // add

    static inline u8x16 add(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return select(mask, add(a, b), u8x16_zero());
    }

    static inline u16x8 add(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return select(mask, add(a, b), u16x8_zero());
    }

    static inline u32x4 add(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return select(mask, add(a, b), u32x4_zero());
    }

    static inline u64x2 add(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return select(mask, add(a, b), u64x2_zero());
    }

    static inline s8x16 add(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return select(mask, add(a, b), s8x16_zero());
    }

    static inline s16x8 add(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return select(mask, add(a, b), s16x8_zero());
    }

    static inline s32x4 add(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return select(mask, add(a, b), s32x4_zero());
    }

    static inline s64x2 add(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return select(mask, add(a, b), s64x2_zero());
    }

    // sub

    static inline u8x16 sub(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return select(mask, sub(a, b), u8x16_zero());
    }

    static inline u16x8 sub(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return select(mask, sub(a, b), u16x8_zero());
    }

    static inline u32x4 sub(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return select(mask, sub(a, b), u32x4_zero());
    }

    static inline u64x2 sub(u64x2 a, u64x2 b, mask64x2 mask)
    {
        return select(mask, sub(a, b), u64x2_zero());
    }

    static inline s8x16 sub(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return select(mask, sub(a, b), s8x16_zero());
    }

    static inline s16x8 sub(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return select(mask, sub(a, b), s16x8_zero());
    }

    static inline s32x4 sub(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return select(mask, sub(a, b), s32x4_zero());
    }

    static inline s64x2 sub(s64x2 a, s64x2 b, mask64x2 mask)
    {
        return select(mask, sub(a, b), s64x2_zero());
    }

    // adds

    static inline u8x16 adds(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return select(mask, adds(a, b), u8x16_zero());
    }

    static inline u16x8 adds(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return select(mask, adds(a, b), u16x8_zero());
    }

    static inline u32x4 adds(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return select(mask, adds(a, b), u32x4_zero());
    }

    static inline s8x16 adds(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return select(mask, adds(a, b), s8x16_zero());
    }

    static inline s16x8 adds(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return select(mask, adds(a, b), s16x8_zero());
    }

    static inline s32x4 adds(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return select(mask, adds(a, b), s32x4_zero());
    }

    // subs

    static inline u8x16 subs(u8x16 a, u8x16 b, mask8x16 mask)
    {
        return select(mask, subs(a, b), u8x16_zero());
    }

    static inline u16x8 subs(u16x8 a, u16x8 b, mask16x8 mask)
    {
        return select(mask, subs(a, b), u16x8_zero());
    }

    static inline u32x4 subs(u32x4 a, u32x4 b, mask32x4 mask)
    {
        return select(mask, subs(a, b), u32x4_zero());
    }

    static inline s8x16 subs(s8x16 a, s8x16 b, mask8x16 mask)
    {
        return select(mask, subs(a, b), s8x16_zero());
    }

    static inline s16x8 subs(s16x8 a, s16x8 b, mask16x8 mask)
    {
        return select(mask, subs(a, b), s16x8_zero());
    }

    static inline s32x4 subs(s32x4 a, s32x4 b, mask32x4 mask)
    {
        return select(mask, subs(a, b), s32x4_zero());
    }

    // madd

    static inline s32x4 madd(s16x8 a, s16x8 b, mask32x4 mask)
    {
        return select(mask, madd(a, b), s32x4_zero());
    }

    // abs

    static inline s8x16 abs(s8x16 a, mask8x16 mask)
    {
        return select(mask, abs(a), s8x16_zero());
    }

    static inline s16x8 abs(s16x8 a, mask16x8 mask)
    {
        return select(mask, abs(a), s16x8_zero());
    }

    static inline s32x4 abs(s32x4 a, mask32x4 mask)
    {
        return select(mask, abs(a), s32x4_zero());
    }

#endif // SIMD_ZEROMASK_INT128

// -----------------------------------------------------------------

#ifdef SIMD_MASK_INT128

    // min

    static inline u8x16 min(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline u16x8 min(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline u32x4 min(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline u64x2 min(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s8x16 min(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s16x8 min(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s32x4 min(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s64x2 min(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return select(mask, min(a, b), value);
    }

    // max

    static inline u8x16 max(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline u16x8 max(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline u32x4 max(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline u64x2 max(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s8x16 max(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s16x8 max(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s32x4 max(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s64x2 max(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return select(mask, max(a, b), value);
    }

    // add

    static inline u8x16 add(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline u16x8 add(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline u32x4 add(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline u64x2 add(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s8x16 add(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s16x8 add(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s32x4 add(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s64x2 add(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return select(mask, add(a, b), value);
    }

    // sub

    static inline u8x16 sub(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline u16x8 sub(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline u32x4 sub(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline u64x2 sub(u64x2 a, u64x2 b, mask64x2 mask, u64x2 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s8x16 sub(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s16x8 sub(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s32x4 sub(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s64x2 sub(s64x2 a, s64x2 b, mask64x2 mask, s64x2 value)
    {
        return select(mask, sub(a, b), value);
    }

    // adds

    static inline u8x16 adds(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline u16x8 adds(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline u32x4 adds(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline s8x16 adds(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline s16x8 adds(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline s32x4 adds(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return select(mask, adds(a, b), value);
    }

    // subs

    static inline u8x16 subs(u8x16 a, u8x16 b, mask8x16 mask, u8x16 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline u16x8 subs(u16x8 a, u16x8 b, mask16x8 mask, u16x8 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline u32x4 subs(u32x4 a, u32x4 b, mask32x4 mask, u32x4 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline s8x16 subs(s8x16 a, s8x16 b, mask8x16 mask, s8x16 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline s16x8 subs(s16x8 a, s16x8 b, mask16x8 mask, s16x8 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline s32x4 subs(s32x4 a, s32x4 b, mask32x4 mask, s32x4 value)
    {
        return select(mask, subs(a, b), value);
    }

    // madd

    static inline s32x4 madd(s16x8 a, s16x8 b, mask32x4 mask, s32x4 value)
    {
        return select(mask, madd(a, b), value);
    }

    // abs

    static inline s8x16 abs(s8x16 a, mask8x16 mask, s8x16 value)
    {
        return select(mask, abs(a), value);
    }

    static inline s16x8 abs(s16x8 a, mask16x8 mask, s16x8 value)
    {
        return select(mask, abs(a), value);
    }

    static inline s32x4 abs(s32x4 a, mask32x4 mask, s32x4 value)
    {
        return select(mask, abs(a), value);
    }

#endif // SIMD_MASK_INT128

// -----------------------------------------------------------------

#ifdef SIMD_MASK_INT256

    // min

    static inline u8x32 min(u8x32 a, u8x32 b, mask8x32 mask, u8x32 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline u16x16 min(u16x16 a, u16x16 b, mask16x16 mask, u16x16 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline u32x8 min(u32x8 a, u32x8 b, mask32x8 mask, u32x8 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline u64x4 min(u64x4 a, u64x4 b, mask64x4 mask, u64x4 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s8x32 min(s8x32 a, s8x32 b, mask8x32 mask, s8x32 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s16x16 min(s16x16 a, s16x16 b, mask16x16 mask, s16x16 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s32x8 min(s32x8 a, s32x8 b, mask32x8 mask, s32x8 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline s64x4 min(s64x4 a, s64x4 b, mask64x4 mask, s64x4 value)
    {
        return select(mask, min(a, b), value);
    }

    // max

    static inline u8x32 max(u8x32 a, u8x32 b, mask8x32 mask, u8x32 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline u16x16 max(u16x16 a, u16x16 b, mask16x16 mask, u16x16 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline u32x8 max(u32x8 a, u32x8 b, mask32x8 mask, u32x8 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline u64x4 max(u64x4 a, u64x4 b, mask64x4 mask, u64x4 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s8x32 max(s8x32 a, s8x32 b, mask8x32 mask, s8x32 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s16x16 max(s16x16 a, s16x16 b, mask16x16 mask, s16x16 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s32x8 max(s32x8 a, s32x8 b, mask32x8 mask, s32x8 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline s64x4 max(s64x4 a, s64x4 b, mask64x4 mask, s64x4 value)
    {
        return select(mask, max(a, b), value);
    }

    // add

    static inline u8x32 add(u8x32 a, u8x32 b, mask8x32 mask, u8x32 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline u16x16 add(u16x16 a, u16x16 b, mask16x16 mask, u16x16 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline u32x8 add(u32x8 a, u32x8 b, mask32x8 mask, u32x8 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline u64x4 add(u64x4 a, u64x4 b, mask64x4 mask, u64x4 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s8x32 add(s8x32 a, s8x32 b, mask8x32 mask, s8x32 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s16x16 add(s16x16 a, s16x16 b, mask16x16 mask, s16x16 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s32x8 add(s32x8 a, s32x8 b, mask32x8 mask, s32x8 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline s64x4 add(s64x4 a, s64x4 b, mask64x4 mask, s64x4 value)
    {
        return select(mask, add(a, b), value);
    }

    // sub

    static inline u8x32 sub(u8x32 a, u8x32 b, mask8x32 mask, u8x32 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline u16x16 sub(u16x16 a, u16x16 b, mask16x16 mask, u16x16 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline u32x8 sub(u32x8 a, u32x8 b, mask32x8 mask, u32x8 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline u64x4 sub(u64x4 a, u64x4 b, mask64x4 mask, u64x4 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s8x32 sub(s8x32 a, s8x32 b, mask8x32 mask, s8x32 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s16x16 sub(s16x16 a, s16x16 b, mask16x16 mask, s16x16 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s32x8 sub(s32x8 a, s32x8 b, mask32x8 mask, s32x8 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline s64x4 sub(s64x4 a, s64x4 b, mask64x4 mask, s64x4 value)
    {
        return select(mask, sub(a, b), value);
    }

    // adds

    static inline u8x32 adds(u8x32 a, u8x32 b, mask8x32 mask, u8x32 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline u16x16 adds(u16x16 a, u16x16 b, mask16x16 mask, u16x16 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline u32x8 adds(u32x8 a, u32x8 b, mask32x8 mask, u32x8 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline s8x32 adds(s8x32 a, s8x32 b, mask8x32 mask, s8x32 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline s16x16 adds(s16x16 a, s16x16 b, mask16x16 mask, s16x16 value)
    {
        return select(mask, adds(a, b), value);
    }

    static inline s32x8 adds(s32x8 a, s32x8 b, mask32x8 mask, s32x8 value)
    {
        return select(mask, adds(a, b), value);
    }

    // subs

    static inline u8x32 subs(u8x32 a, u8x32 b, mask8x32 mask, u8x32 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline u16x16 subs(u16x16 a, u16x16 b, mask16x16 mask, u16x16 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline u32x8 subs(u32x8 a, u32x8 b, mask32x8 mask, u32x8 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline s8x32 subs(s8x32 a, s8x32 b, mask8x32 mask, s8x32 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline s16x16 subs(s16x16 a, s16x16 b, mask16x16 mask, s16x16 value)
    {
        return select(mask, subs(a, b), value);
    }

    static inline s32x8 subs(s32x8 a, s32x8 b, mask32x8 mask, s32x8 value)
    {
        return select(mask, subs(a, b), value);
    }

    // madd

    static inline s32x8 madd(s16x16 a, s16x16 b, mask32x8 mask, s32x8 value)
    {
        return select(mask, madd(a, b), value);
    }

    // abs

    static inline s8x32 abs(s8x32 a, mask8x32 mask, s8x32 value)
    {
        return select(mask, abs(a), value);
    }

    static inline s16x16 abs(s16x16 a, mask16x16 mask, s16x16 value)
    {
        return select(mask, abs(a), value);
    }

    static inline s32x8 abs(s32x8 a, mask32x8 mask, s32x8 value)
    {
        return select(mask, abs(a), value);
    }

#endif // SIMD_MASK_INT256

// -----------------------------------------------------------------

#ifdef SIMD_ZEROMASK_FLOAT128

    static inline f32x4 min(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return select(mask, min(a, b), f32x4_zero());
    }

    static inline f32x4 max(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return select(mask, max(a, b), f32x4_zero());
    }

    static inline f32x4 add(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return select(mask, add(a, b), f32x4_zero());
    }

    static inline f32x4 sub(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return select(mask, sub(a, b), f32x4_zero());
    }

    static inline f32x4 mul(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return select(mask, mul(a, b), f32x4_zero());
    }

    static inline f32x4 div(f32x4 a, f32x4 b, mask32x4 mask)
    {
        return select(mask, div(a, b), f32x4_zero());
    }

#endif // SIMD_ZEROMASK_FLOAT128

// -----------------------------------------------------------------

#ifdef SIMD_MASK_FLOAT128

    static inline f32x4 min(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline f32x4 max(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline f32x4 add(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline f32x4 sub(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline f32x4 mul(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return select(mask, mul(a, b), value);
    }

    static inline f32x4 div(f32x4 a, f32x4 b, mask32x4 mask, f32x4 value)
    {
        return select(mask, div(a, b), value);
    }

#endif // SIMD_MASK_FLOAT128

// -----------------------------------------------------------------

#ifdef SIMD_ZEROMASK_DOUBLE128

    static inline f64x2 min(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return select(mask, min(a, b), f64x2_zero());
    }

    static inline f64x2 max(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return select(mask, max(a, b), f64x2_zero());
    }

    static inline f64x2 add(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return select(mask, add(a, b), f64x2_zero());
    }

    static inline f64x2 sub(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return select(mask, sub(a, b), f64x2_zero());
    }

    static inline f64x2 mul(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return select(mask, mul(a, b), f64x2_zero());
    }

    static inline f64x2 div(f64x2 a, f64x2 b, mask64x2 mask)
    {
        return select(mask, div(a, b), f64x2_zero());
    }

#endif // SIMD_ZEROMASK_DOUBLE128

// -----------------------------------------------------------------

#ifdef SIMD_MASK_DOUBLE128

    static inline f64x2 min(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return select(mask, min(a, b), value);
    }

    static inline f64x2 max(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return select(mask, max(a, b), value);
    }

    static inline f64x2 add(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return select(mask, add(a, b), value);
    }

    static inline f64x2 sub(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return select(mask, sub(a, b), value);
    }

    static inline f64x2 mul(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return select(mask, mul(a, b), value);
    }

    static inline f64x2 div(f64x2 a, f64x2 b, mask64x2 mask, f64x2 value)
    {
        return select(mask, div(a, b), value);
    }

#endif // SIMD_MASK_DOUBLE128
