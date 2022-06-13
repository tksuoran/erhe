/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s64, 2>
    {
        using VectorType = simd::s64x2;
        using ScalarType = s64;
        enum { VectorSize = 2 };

        union
        {
            simd::s64x2 m;

            ScalarAccessor<s64, simd::s64x2, 0> x;
            ScalarAccessor<s64, simd::s64x2, 1> y;

            // generate 2 component accessors
#define VECTOR2_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor2<s64, simd::s64x2, A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR2_SHUFFLE_ACCESSOR2
        };

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
            : m(simd::s64x2_set(s))
        {
        }

        explicit Vector(s64 x, s64 y)
            : m(simd::s64x2_set(x, y))
        {
        }

        Vector(simd::s64x2 v)
            : m(v)
        {
        }

        template <int X, int Y>
        Vector(const ShuffleAccessor2<s64, simd::s64x2, X, Y>& p)
        {
            m = p;
        }

        template <int X, int Y>
        Vector& operator = (const ShuffleAccessor2<s64, simd::s64x2, X, Y>& p)
        {
            m = p;
            return *this;
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

        Vector& operator = (simd::s64x2 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s64 s)
        {
            m = simd::s64x2_set(s);
            return *this;
        }

        operator simd::s64x2 () const
        {
            return m;
        }

#ifdef simd_int128_is_hardware_vector
        operator simd::s64x2::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1);
        }
    };

    static inline const Vector<s64, 2> operator + (Vector<s64, 2> v)
    {
        return v;
    }

    static inline Vector<s64, 2> operator - (Vector<s64, 2> v)
    {
        return simd::sub(simd::s64x2_zero(), v);
    }

    static inline Vector<s64, 2>& operator += (Vector<s64, 2>& a, Vector<s64, 2> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s64, 2>& operator -= (Vector<s64, 2>& a, Vector<s64, 2> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s64, 2> operator + (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s64, 2> operator - (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s64, 2> unpacklo(Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s64, 2> unpackhi(Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s64, 2> add(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s64, 2> add(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask, Vector<s64, 2> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s64, 2> sub(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s64, 2> sub(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask, Vector<s64, 2> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s64, 2> min(Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s64, 2> min(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s64, 2> min(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask, Vector<s64, 2> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s64, 2> max(Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s64, 2> max(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s64, 2> max(Vector<s64, 2> a, Vector<s64, 2> b, mask64x2 mask, Vector<s64, 2> value)
    {
        return simd::max(a, b, mask, value);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s64, 2> nand(Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s64, 2> operator & (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s64, 2> operator | (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s64, 2> operator ^ (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s64, 2> operator ~ (Vector<s64, 2> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask64x2 operator > (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask64x2 operator >= (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask64x2 operator < (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask64x2 operator <= (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask64x2 operator == (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask64x2 operator != (Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s64, 2> select(mask64x2 mask, Vector<s64, 2> a, Vector<s64, 2> b)
    {
        return simd::select(mask, a, b);
    }

    // ------------------------------------------------------------------
	// shift
    // ------------------------------------------------------------------

    static inline Vector<s64, 2> operator << (Vector<s64, 2> a, int b)
    {
        return simd::sll(a, b);
    }

} // namespace mango::math
