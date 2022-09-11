/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    // ------------------------------------------------------------------
    // Vector<double, 2>
    // ------------------------------------------------------------------

    template <>
    struct Vector<double, 2>
    {
        using VectorType = simd::f64x2;
        using ScalarType = double;
        enum { VectorSize = 2 };

        union
        {
            simd::f64x2 m {};

            ScalarAccessor<double, simd::f64x2, 0> x;
            ScalarAccessor<double, simd::f64x2, 1> y;

            // generate 2 component accessors
#define VECTOR2_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor2<double, simd::f64x2, A, B> NAME
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

        Vector(double s)
            : m(simd::f64x2_set(s))
        {
        }

        explicit Vector(double x, double y)
            : m(simd::f64x2_set(x, y))
        {
        }

        Vector(simd::f64x2 v)
            : m(v)
        {
        }

        template <int X, int Y>
        Vector(const ShuffleAccessor2<double, simd::f64x2, X, Y>& p)
        {
            m = p;
        }

        template <int X, int Y>
        Vector& operator = (const ShuffleAccessor2<double, simd::f64x2, X, Y>& p)
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

        Vector& operator = (simd::f64x2 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (double s)
        {
            m = simd::f64x2_set(s);
            return *this;
        }

        operator simd::f64x2 () const
        {
            return m;
        }

#ifdef simd_float128_is_hardware_vector
        operator simd::f64x2::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0.0, 1.0);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_OPERATORS(double, 2, f64x2);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_FUNCTIONS(double, 2, f64x2, mask64x2);

    static inline double square(Vector<double, 2> a)
    {
        return simd::dot2(a, a);
    }

    static inline double length(Vector<double, 2> a)
    {
        return std::sqrt(simd::dot2(a, a));
    }

    static inline Vector<double, 2> normalize(Vector<double, 2> a)
    {
        return simd::mul(a, simd::rsqrt(simd::f64x2_set(simd::dot2(a, a))));
    }

    static inline double dot(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::dot2(a, b);
    }

    template <int x, int y>
    static inline Vector<double, 2> shuffle(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::shuffle<x, y>(a, b);
    }

    MATH_SIMD_BITWISE_FUNCTIONS(double, 2);
    MATH_SIMD_COMPARE_FUNCTIONS(double, 2, mask64x2);

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    /* These come from default implementation:

    Vector<double, 2> sin(Vector<double, 2> a);
    Vector<double, 2> cos(Vector<double, 2> a);
    Vector<double, 2> tan(Vector<double, 2> a);
    Vector<double, 2> exp(Vector<double, 2> a);
    Vector<double, 2> exp2(Vector<double, 2> a);
    Vector<double, 2> log(Vector<double, 2> a);
    Vector<double, 2> log2(Vector<double, 2> a);
    Vector<double, 2> asin(Vector<double, 2> a);
    Vector<double, 2> acos(Vector<double, 2> a);
    Vector<double, 2> atan(Vector<double, 2> a);
    Vector<double, 2> atan2(Vector<double, 2> a, Vector<double, 2> b);
    Vector<double, 2> pow(Vector<double, 2> a, Vector<double, 2> b);

    */

} // namespace mango::math
