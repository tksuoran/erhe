/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s16, 16>
    {
        using VectorType = simd::s16x16;
        using ScalarType = s16;
        enum { VectorSize = 16 };

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

        Vector(s16 s)
            : m(simd::s16x16_set(s))
        {
        }

        Vector(s16 s00, s16 s01, s16 s02, s16 s03, s16 s04, s16 s05, s16 s06, s16 s07,
               s16 s08, s16 s09, s16 s10, s16 s11, s16 s12, s16 s13, s16 s14, s16 s15)
            : m(simd::s16x16_set(s00, s01, s02, s03, s04, s05, s06, s07, 
                                 s08, s09, s10, s11, s12, s13, s14, s15))
        {
        }

        Vector(simd::s16x16 v)
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

        Vector& operator = (simd::s16x16 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s16 s)
        {
            m = simd::s16x16_set(s);
            return *this;
        }

        operator simd::s16x16 () const
        {
            return m;
        }

#ifdef simd_int256_is_hardware_vector
        operator simd::s16x16::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        }
    };

    static inline const Vector<s16, 16> operator + (Vector<s16, 16> v)
    {
        return v;
    }

    static inline Vector<s16, 16> operator - (Vector<s16, 16> v)
    {
        return simd::neg(v);
    }

    static inline Vector<s16, 16>& operator += (Vector<s16, 16>& a, Vector<s16, 16> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s16, 16>& operator -= (Vector<s16, 16>& a, Vector<s16, 16> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s16, 16> operator + (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s16, 16> operator - (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s16, 16> unpacklo(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s16, 16> unpackhi(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s16, 16> abs(Vector<s16, 16> a)
    {
        return simd::abs(a);
    }

    static inline Vector<s16, 16> abs(Vector<s16, 16> a, mask16x16 mask)
    {
        return simd::abs(a, mask);
    }

    static inline Vector<s16, 16> abs(Vector<s16, 16> a, mask16x16 mask, Vector<s16, 16> value)
    {
        return simd::abs(a, mask, value);
    }

    static inline Vector<s16, 16> add(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s16, 16> add(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask, Vector<s16, 16> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s16, 16> sub(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s16, 16> sub(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask, Vector<s16, 16> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s16, 16> adds(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::adds(a, b);
    }

    static inline Vector<s16, 16> adds(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask)
    {
        return simd::adds(a, b, mask);
    }

    static inline Vector<s16, 16> adds(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask, Vector<s16, 16> value)
    {
        return simd::adds(a, b, mask, value);
    }

    static inline Vector<s16, 16> subs(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::subs(a, b);
    }

    static inline Vector<s16, 16> subs(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask)
    {
        return simd::subs(a, b, mask);
    }

    static inline Vector<s16, 16> subs(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask, Vector<s16, 16> value)
    {
        return simd::subs(a, b, mask, value);
    }

    static inline Vector<s16, 16> hadd(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::hadd(a, b);
    }

    static inline Vector<s16, 16> hsub(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::hsub(a, b);
    }

    static inline Vector<s16, 16> hadds(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::hadds(a, b);
    }

    static inline Vector<s16, 16> hsubs(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::hsubs(a, b);
    }

    static inline Vector<s16, 16> min(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s16, 16> min(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s16, 16> min(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask, Vector<s16, 16> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s16, 16> max(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s16, 16> max(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s16, 16> max(Vector<s16, 16> a, Vector<s16, 16> b, mask16x16 mask, Vector<s16, 16> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<s16, 16> clamp(Vector<s16, 16> a, Vector<s16, 16> low, Vector<s16, 16> high)
    {
        return simd::clamp(a, low, high);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s16, 16> nand(Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s16, 16> operator & (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s16, 16> operator | (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s16, 16> operator ^ (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s16, 16> operator ~ (Vector<s16, 16> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask16x16 operator > (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask16x16 operator < (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::compare_gt(b, a);
    }

    static inline mask16x16 operator == (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask16x16 operator >= (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask16x16 operator <= (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::compare_le(b, a);
    }

    static inline mask16x16 operator != (Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s16, 16> select(mask16x16 mask, Vector<s16, 16> a, Vector<s16, 16> b)
    {
        return simd::select(mask, a, b);
    }

    // ------------------------------------------------------------------
	// shift
    // ------------------------------------------------------------------

    static inline Vector<s16, 16> operator << (Vector<s16, 16> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<s16, 16> operator >> (Vector<s16, 16> a, int b)
    {
        return simd::sra(a, b);
    }

} // namespace mango::math
