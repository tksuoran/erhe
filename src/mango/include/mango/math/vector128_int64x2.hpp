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
            simd::s64x2 m {};

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

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(s64, 2);
    MATH_SIMD_SIGNED_INTEGER_OPERATORS(s64, 2);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(s64, 2, mask64x2);

    MATH_SIMD_BITWISE_FUNCTIONS(s64, 2);
    MATH_SIMD_COMPARE_FUNCTIONS(s64, 2, mask64x2);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<s64, 2> operator << (Vector<s64, 2> a, int b)
    {
        return simd::sll(a, b);
    }

} // namespace mango::math
