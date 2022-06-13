/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s64, 4>
    {
        using VectorType = simd::s64x4;
        using ScalarType = s64;
        enum { VectorSize = 4 };

        union
        {
            simd::s64x4 m;

            ScalarAccessor<s64, simd::s64x4, 0> x;
            ScalarAccessor<s64, simd::s64x4, 1> y;
            ScalarAccessor<s64, simd::s64x4, 2> z;
            ScalarAccessor<s64, simd::s64x4, 3> w;

            // generate 2 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor4x2<s64, simd::s64x4, A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR2

            // generate 3 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR3(A, B, C, NAME) \
            ShuffleAccessor4x3<s64, simd::s64x4, A, B, C> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR3

            // generate 4 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR4(A, B, C, D, NAME) \
            ShuffleAccessor4<s64, simd::s64x4, A, B, C, D> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR4
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
            : m(simd::s64x4_set(s))
        {
        }

        explicit Vector(s64 x, s64 y, s64 z, s64 w)
            : m(simd::s64x4_set(x, y, z, w))
        {
        }

        Vector(simd::s64x4 v)
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

        Vector& operator = (simd::s64x4 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s64 s)
        {
            m = simd::s64x4_set(s);
            return *this;
        }

        operator simd::s64x4 () const
        {
            return m;
        }

#ifdef simd_int256_is_hardware_vector
        operator simd::s64x4::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3);
        }
    };

    static inline const Vector<s64, 4> operator + (Vector<s64, 4> v)
    {
        return v;
    }

    static inline Vector<s64, 4> operator - (Vector<s64, 4> v)
    {
        return simd::sub(simd::s64x4_zero(), v);
    }

    static inline Vector<s64, 4>& operator += (Vector<s64, 4>& a, Vector<s64, 4> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s64, 4>& operator -= (Vector<s64, 4>& a, Vector<s64, 4> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s64, 4> operator + (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s64, 4> operator - (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s64, 4> unpacklo(Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s64, 4> unpackhi(Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s64, 4> add(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s64, 4> add(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask, Vector<s64, 4> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s64, 4> sub(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s64, 4> sub(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask, Vector<s64, 4> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s64, 4> min(Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s64, 4> min(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s64, 4> min(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask, Vector<s64, 4> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s64, 4> max(Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s64, 4> max(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s64, 4> max(Vector<s64, 4> a, Vector<s64, 4> b, mask64x4 mask, Vector<s64, 4> value)
    {
        return simd::max(a, b, mask, value);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s64, 4> nand(Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s64, 4> operator & (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s64, 4> operator | (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s64, 4> operator ^ (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s64, 4> operator ~ (Vector<s64, 4> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask64x4 operator > (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask64x4 operator >= (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask64x4 operator < (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask64x4 operator <= (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask64x4 operator == (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask64x4 operator != (Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s64, 4> select(mask64x4 mask, Vector<s64, 4> a, Vector<s64, 4> b)
    {
        return simd::select(mask, a, b);
    }

    // ------------------------------------------------------------------
	// shift
    // ------------------------------------------------------------------

    static inline Vector<s64, 4> operator << (Vector<s64, 4> a, int b)
    {
        return simd::sll(a, b);
    }

} // namespace mango::math
