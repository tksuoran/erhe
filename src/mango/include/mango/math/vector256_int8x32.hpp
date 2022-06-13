/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s8, 32>
    {
        using VectorType = simd::s8x32;
        using ScalarType = s8;
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

        Vector(s8 s)
            : m(simd::s8x32_set(s))
        {
        }

        Vector(s8 s00, s8 s01, s8 s02, s8 s03, s8 s04, s8 s05, s8 s06, s8 s07, 
               s8 s08, s8 s09, s8 s10, s8 s11, s8 s12, s8 s13, s8 s14, s8 s15,
               s8 s16, s8 s17, s8 s18, s8 s19, s8 s20, s8 s21, s8 s22, s8 s23, 
               s8 s24, s8 s25, s8 s26, s8 s27, s8 s28, s8 s29, s8 s30, s8 s31)
            : m(simd::s8x32_set(s00, s01, s02, s03, s04, s05, s06, s07, s08, s09, s10, s11, s12, s13, s14, s15,
                                s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30, s31))
        {
        }

        Vector(simd::s8x32 v)
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

        Vector& operator = (simd::s8x32 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s8 s)
        {
            m = simd::s8x32_set(s);
            return *this;
        }

        operator simd::s8x32 () const
        {
            return m;
        }

#ifdef simd_int256_is_hardware_vector
        operator simd::s8x32::vector () const
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

    static inline const Vector<s8, 32> operator + (Vector<s8, 32> v)
    {
        return v;
    }

    static inline Vector<s8, 32> operator - (Vector<s8, 32> v)
    {
        return simd::neg(v);
    }

    static inline Vector<s8, 32>& operator += (Vector<s8, 32>& a, Vector<s8, 32> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s8, 32>& operator -= (Vector<s8, 32>& a, Vector<s8, 32> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s8, 32> operator + (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s8, 32> operator - (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s8, 32> unpacklo(Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s8, 32> unpackhi(Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s8, 32> abs(Vector<s8, 32> a)
    {
        return simd::abs(a);
    }

    static inline Vector<s8, 32> abs(Vector<s8, 32> a, mask8x32 mask)
    {
        return simd::abs(a, mask);
    }

    static inline Vector<s8, 32> abs(Vector<s8, 32> a, mask8x32 mask, Vector<s8, 32> value)
    {
        return simd::abs(a, mask, value);
    }

    static inline Vector<s8, 32> add(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s8, 32> add(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask, Vector<s8, 32> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s8, 32> sub(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s8, 32> sub(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask, Vector<s8, 32> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s8, 32> adds(Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::adds(a, b);
    }

    static inline Vector<s8, 32> adds(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask)
    {
        return simd::adds(a, b, mask);
    }

    static inline Vector<s8, 32> adds(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask, Vector<s8, 32> value)
    {
        return simd::adds(a, b, mask, value);
    }

    static inline Vector<s8, 32> subs(Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::subs(a, b);
    }

    static inline Vector<s8, 32> subs(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask)
    {
        return simd::subs(a, b, mask);
    }

    static inline Vector<s8, 32> subs(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask, Vector<s8, 32> value)
    {
        return simd::subs(a, b, mask, value);
    }

    static inline Vector<s8, 32> min(Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s8, 32> min(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s8, 32> min(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask, Vector<s8, 32> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s8, 32> max(Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s8, 32> max(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s8, 32> max(Vector<s8, 32> a, Vector<s8, 32> b, mask8x32 mask, Vector<s8, 32> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<s8, 32> clamp(Vector<s8, 32> a, Vector<s8, 32> low, Vector<s8, 32> high)
    {
        return simd::clamp(a, low, high);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s8, 32> nand(Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s8, 32> operator & (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s8, 32> operator | (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s8, 32> operator ^ (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s8, 32> operator ~ (Vector<s8, 32> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask8x32 operator > (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask8x32 operator < (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::compare_gt(b, a);
    }

    static inline mask8x32 operator == (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask8x32 operator >= (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask8x32 operator <= (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::compare_le(b, a);
    }

    static inline mask8x32 operator != (Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s8, 32> select(mask8x32 mask, Vector<s8, 32> a, Vector<s8, 32> b)
    {
        return simd::select(mask, a, b);
    }

} // namespace mango::math
