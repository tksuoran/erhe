/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s64, 8>
    {
        using VectorType = simd::s64x8;
        using ScalarType = s64;
        enum { VectorSize = 8 };

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

        Vector(s64 s)
            : m(simd::s64x8_set(s))
        {
        }

        explicit Vector(s64 s0, s64 s1, s64 s2, s64 s3, s64 s4, s64 s5, s64 s6, s64 s7)
            : m(simd::s64x8_set(s0, s1, s2, s3, s4, s5, s6, s7))
        {
        }

        Vector(simd::s64x8 v)
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

        Vector& operator = (simd::s64x8 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s64 s)
        {
            m = simd::s64x8_set(s);
            return *this;
        }

        operator simd::s64x8 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::s64x8::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7);
        }
    };

    static inline const Vector<s64, 8> operator + (Vector<s64, 8> v)
    {
        return v;
    }

    static inline Vector<s64, 8> operator - (Vector<s64, 8> v)
    {
        return simd::sub(simd::s64x8_zero(), v);
    }

    static inline Vector<s64, 8>& operator += (Vector<s64, 8>& a, Vector<s64, 8> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s64, 8>& operator -= (Vector<s64, 8>& a, Vector<s64, 8> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s64, 8> operator + (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s64, 8> operator - (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s64, 8> unpacklo(Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s64, 8> unpackhi(Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s64, 8> add(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s64, 8> add(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask, Vector<s64, 8> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s64, 8> sub(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s64, 8> sub(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask, Vector<s64, 8> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s64, 8> min(Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s64, 8> min(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s64, 8> min(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask, Vector<s64, 8> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s64, 8> max(Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s64, 8> max(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s64, 8> max(Vector<s64, 8> a, Vector<s64, 8> b, mask64x8 mask, Vector<s64, 8> value)
    {
        return simd::max(a, b, mask, value);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s64, 8> nand(Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s64, 8> operator & (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s64, 8> operator | (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s64, 8> operator ^ (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s64, 8> operator ~ (Vector<s64, 8> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask64x8 operator > (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask64x8 operator >= (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask64x8 operator < (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask64x8 operator <= (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask64x8 operator == (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask64x8 operator != (Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s64, 8> select(mask64x8 mask, Vector<s64, 8> a, Vector<s64, 8> b)
    {
        return simd::select(mask, a, b);
    }

    // ------------------------------------------------------------------
	// shift
    // ------------------------------------------------------------------

    static inline Vector<s64, 8> operator << (Vector<s64, 8> a, int b)
    {
        return simd::sll(a, b);
    }

} // namespace mango::math
