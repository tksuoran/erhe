/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/math/vector_float64x2.hpp>

namespace mango::math
{

    // ------------------------------------------------------------------
    // Vector<double, 4>
    // ------------------------------------------------------------------

    template <>
    struct Vector<double, 4>
    {
        using VectorType = simd::f64x4;
        using ScalarType = double;
        enum { VectorSize = 4 };

        union
        {
            simd::f64x4 m {};

            LowAccessor<Vector<double, 2>, simd::f64x4> low;
            HighAccessor<Vector<double, 2>, simd::f64x4> high;

            ScalarAccessor<double, simd::f64x4, 0> x;
            ScalarAccessor<double, simd::f64x4, 1> y;
            ScalarAccessor<double, simd::f64x4, 2> z;
            ScalarAccessor<double, simd::f64x4, 3> w;

            // generate 2 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor4x2<double, simd::f64x4, A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR2

            // generate 3 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR3(A, B, C, NAME) \
            ShuffleAccessor4x3<double, simd::f64x4, A, B, C> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR3

            // generate 4 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR4(A, B, C, D, NAME) \
            ShuffleAccessor4<double, simd::f64x4, A, B, C, D> NAME
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

        Vector(double s)
            : m(simd::f64x4_set(s))
        {
        }

        explicit Vector(double s0, double s1, double s2, double s3)
            : m(simd::f64x4_set(s0, s1, s2, s3))
        {
        }

        explicit Vector(const Vector<double, 2>& v, double s0, double s1)
            : m(simd::f64x4_set(v.x, v.y, s0, s1))
        {
        }

        explicit Vector(double s0, double s1, const Vector<double, 2>& v)
            : m(simd::f64x4_set(s0, s1, v.x, v.y))
        {
        }

        explicit Vector(double s0, const Vector<double, 2>& v, double s1)
            : m(simd::f64x4_set(s0, v.x, v.y, s1))
        {
        }

        explicit Vector(const Vector<double, 2>& v0, const Vector<double, 2>& v1)
            : m(simd::combine(v0, v1))
        {
        }

        explicit Vector(const Vector<double, 3>& v, double s)
            : m(simd::f64x4_set(v.x, v.y, v.z, s))
        {
        }

        explicit Vector(double s, const Vector<double, 3>& v)
            : m(simd::f64x4_set(s, v.x, v.y, v.z))
        {
        }

        Vector(simd::f64x4 v)
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

        Vector& operator = (simd::f64x4 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (double s)
        {
            m = simd::f64x4_set(s);
            return *this;
        }

        operator simd::f64x4 () const
        {
            return m;
        }

#ifdef simd_float256_is_hardware_vector
        operator simd::f64x4::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0.0, 1.0, 2.0, 3.0);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_OPERATORS(double, 4, f64x4);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_FUNCTIONS(double, 4, f64x4, mask64x4);

    static inline double square(Vector<double, 4> a)
    {
        return simd::dot4(a, a);
    }

    static inline double length(Vector<double, 4> a)
    {
        return std::sqrt(simd::dot4(a, a));
    }

    static inline Vector<double, 4> normalize(Vector<double, 4> a)
    {
        return simd::mul(a, simd::rsqrt(simd::f64x4_set(simd::dot4(a, a))));
    }

    static inline double dot(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::dot4(a, b);
    }

    static inline Vector<double, 4> hmin(Vector<double, 4> v)
    {
        return simd::hmin(v);
    }

    static inline Vector<double, 4> hmax(Vector<double, 4> v)
    {
        return simd::hmax(v);
    }

    template <int x, int y, int z, int w>
    static inline Vector<double, 4> shuffle(Vector<double, 4> v)
    {
        return simd::shuffle<x, y, z, w>(v);
    }

    MATH_SIMD_BITWISE_FUNCTIONS(double, 4);
    MATH_SIMD_COMPARE_FUNCTIONS(double, 4, mask64x4);

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    /* These come from default implementation:

    Vector<double, 4> sin(Vector<double, 4> a);
    Vector<double, 4> cos(Vector<double, 4> a);
    Vector<double, 4> tan(Vector<double, 4> a);
    Vector<double, 4> exp(Vector<double, 4> a);
    Vector<double, 4> exp2(Vector<double, 4> a);
    Vector<double, 4> log(Vector<double, 4> a);
    Vector<double, 4> log2(Vector<double, 4> a);
    Vector<double, 4> asin(Vector<double, 4> a);
    Vector<double, 4> acos(Vector<double, 4> a);
    Vector<double, 4> atan(Vector<double, 4> a);
    Vector<double, 4> atan2(Vector<double, 4> a, Vector<double, 4> b);
    Vector<double, 4> pow(Vector<double, 4> a, Vector<double, 4> b);

    */

} // namespace mango::math
