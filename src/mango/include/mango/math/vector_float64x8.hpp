/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/math/vector_float64x2.hpp>

namespace mango::math
{

    template <>
    struct Vector<double, 8>
    {
        using VectorType = simd::f64x8;
        using ScalarType = double;
        enum { VectorSize = 8 };

        union
        {
            simd::f64x8 m;
            LowAccessor<Vector<double, 4>, simd::f64x8> low;
            HighAccessor<Vector<double, 4>, simd::f64x8> high;
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
            : m(simd::f64x8_set(s))
        {
        }

        explicit Vector(double s0, double s1, double s2, double s3, double s4, double s5, double s6, double s7)
            : m(simd::f64x8_set(s0, s1, s2, s3, s4, s5, s6, s7))
        {
        }

        Vector(simd::f64x8 v)
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

        Vector& operator = (simd::f64x8 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (double s)
        {
            m = simd::f64x8_set(s);
            return *this;
        }

        operator simd::f64x8 () const
        {
            return m;
        }

#ifdef simd_float512_is_hardware_vector
        operator simd::f64x8::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_OPERATORS(double, 8, f64x8);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_FUNCTIONS(double, 8, f64x8, mask64x8);

    MATH_SIMD_BITWISE_FUNCTIONS(double, 8);
    MATH_SIMD_COMPARE_FUNCTIONS(double, 8, mask64x8);

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    /* These come from the default implementation:

    Vector<double, 8> sin(Vector<double, 8> a);
    Vector<double, 8> cos(Vector<double, 8> a);
    Vector<double, 8> tan(Vector<double, 8> a);
    Vector<double, 8> exp(Vector<double, 8> a);
    Vector<double, 8> exp2(Vector<double, 8> a);
    Vector<double, 8> log(Vector<double, 8> a);
    Vector<double, 8> log2(Vector<double, 8> a);
    Vector<double, 8> asin(Vector<double, 8> a);
    Vector<double, 8> acos(Vector<double, 8> a);
    Vector<double, 8> atan(Vector<double, 8> a);
    Vector<double, 8> atan2(Vector<double, 8> a, Vector<double, 8> b);
    Vector<double, 8> pow(Vector<double, 8> a, Vector<double, 8> b);

    */

} // namespace mango::math
