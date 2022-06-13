/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<u16, 32>
    {
        using VectorType = simd::u16x32;
        using ScalarType = u16;
        enum { VectorSize = 32 };

        VectorType m;

        ScalarType& operator [] (size_t index)
        {
            assert(index < VectorSize);
            return data()[index];
        }

        ScalarType operator [] (size_t index) const
        {
            assert(index < VectorSize);
            return data()[index];
        }

        ScalarType* data()
        {
            return reinterpret_cast<ScalarType *>(this);
        }

        const ScalarType* data() const
        {
            return reinterpret_cast<const ScalarType *>(this);
        }

        explicit Vector() {}

        Vector(u16 s)
            : m(simd::u16x32_set(s))
        {
        }

        Vector(
            u16 s00, u16 s01, u16 s02, u16 s03, u16 s04, u16 s05, u16 s06, u16 s07, 
            u16 s08, u16 s09, u16 s10, u16 s11, u16 s12, u16 s13, u16 s14, u16 s15,
            u16 s16, u16 s17, u16 s18, u16 s19, u16 s20, u16 s21, u16 s22, u16 s23, 
            u16 s24, u16 s25, u16 s26, u16 s27, u16 s28, u16 s29, u16 s30, u16 s31)
            : m(simd::u16x32_set(
                s00, s01, s02, s03, s04, s05, s06, s07, s08, s09, s10, s11, s12, s13, s14, s15, 
                s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30, s31))
        {
        }

        Vector(simd::u16x32 v)
            : m(v)
        {
        }

        template <typename T, int I>
        Vector& operator = (const ScalarAccessor<ScalarType, T, I>& accessor)
        {
            *this = ScalarType(accessor);
            return *this;
        }

        Vector(const Vector& v) = default;

        Vector& operator = (const Vector& v)
        {
            m = v.m;
            return *this;
        }

        Vector& operator = (simd::u16x32 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (u16 s)
        {
            m = simd::u16x32_set(s);
            return *this;
        }

        operator simd::u16x32 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::u16x32::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
        }
    };

    static inline const Vector<u16, 32> operator + (Vector<u16, 32> v)
    {
        return v;
    }

    static inline Vector<u16, 32> operator - (Vector<u16, 32> v)
    {
        return simd::sub(simd::u16x32_zero(), v);
    }

    static inline Vector<u16, 32>& operator += (Vector<u16, 32>& a, Vector<u16, 32> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<u16, 32>& operator -= (Vector<u16, 32>& a, Vector<u16, 32> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<u16, 32> operator + (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<u16, 32> operator - (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<u16, 32> unpacklo(Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<u16, 32> unpackhi(Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<u16, 32> add(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<u16, 32> add(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask, Vector<u16, 32> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<u16, 32> sub(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<u16, 32> sub(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask, Vector<u16, 32> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<u16, 32> adds(Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::adds(a, b);
    }

    static inline Vector<u16, 32> adds(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask)
    {
        return simd::adds(a, b, mask);
    }

    static inline Vector<u16, 32> adds(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask, Vector<u16, 32> value)
    {
        return simd::adds(a, b, mask, value);
    }

    static inline Vector<u16, 32> subs(Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::subs(a, b);
    }

    static inline Vector<u16, 32> subs(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask)
    {
        return simd::subs(a, b, mask);
    }

    static inline Vector<u16, 32> subs(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask, Vector<u16, 32> value)
    {
        return simd::subs(a, b, mask, value);
    }

    static inline Vector<u16, 32> min(Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<u16, 32> min(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<u16, 32> min(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask, Vector<u16, 32> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<u16, 32> max(Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<u16, 32> max(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<u16, 32> max(Vector<u16, 32> a, Vector<u16, 32> b, mask16x32 mask, Vector<u16, 32> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<u16, 32> clamp(Vector<u16, 32> a, Vector<u16, 32> low, Vector<u16, 32> high)
    {
        return simd::clamp(a, low, high);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<u16, 32> nand(Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<u16, 32> operator & (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<u16, 32> operator | (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<u16, 32> operator ^ (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<u16, 32> operator ~ (Vector<u16, 32> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask16x32 operator > (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask16x32 operator < (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::compare_gt(b, a);
    }

    static inline mask16x32 operator == (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask16x32 operator >= (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask16x32 operator <= (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::compare_le(b, a);
    }

    static inline mask16x32 operator != (Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<u16, 32> select(mask16x32 mask, Vector<u16, 32> a, Vector<u16, 32> b)
    {
        return simd::select(mask, a, b);
    }

    // ------------------------------------------------------------------
	// shift
    // ------------------------------------------------------------------

    static inline Vector<u16, 32> operator << (Vector<u16, 32> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u16, 32> operator >> (Vector<u16, 32> a, int b)
    {
        return simd::srl(a, b);
    }

} // namespace mango::math
