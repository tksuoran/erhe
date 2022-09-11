/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/math/vector_float32x8.hpp>

namespace mango::math
{

    template <>
    struct Vector<float, 16>
    {
        using VectorType = simd::f32x16;
        using ScalarType = float;
        enum { VectorSize = 16 };

        union
        {
            simd::f32x16 m;
            LowAccessor<Vector<float, 8>, simd::f32x16> low;
            HighAccessor<Vector<float, 8>, simd::f32x16> high;
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

        Vector(float s)
            : m(simd::f32x16_set(s))
        {
        }

        explicit Vector(float s0, float s1, float s2, float s3, float s4, float s5, float s6, float s7,
            float s8, float s9, float s10, float s11, float s12, float s13, float s14, float s15)
            : m(simd::f32x16_set(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15))
        {
        }

        Vector(simd::f32x16 v)
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

        Vector& operator = (simd::f32x16 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (float s)
        {
            m = simd::f32x16_set(s);
            return *this;
        }

        operator simd::f32x16 () const
        {
            return m;
        }

#ifdef simd_float512_is_hardware_vector
        operator simd::f32x16::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_OPERATORS(float, 16, f32x16);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_FLOAT_FUNCTIONS(float, 16, f32x16, mask32x16);

    MATH_SIMD_BITWISE_FUNCTIONS(float, 16);
    MATH_SIMD_COMPARE_FUNCTIONS(float, 16, mask32x16);

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    Vector<float, 16> sin(Vector<float, 16> a);
    Vector<float, 16> cos(Vector<float, 16> a);
    Vector<float, 16> tan(Vector<float, 16> a);
    Vector<float, 16> exp(Vector<float, 16> a);
    Vector<float, 16> exp2(Vector<float, 16> a);
    Vector<float, 16> log(Vector<float, 16> a);
    Vector<float, 16> log2(Vector<float, 16> a);
    Vector<float, 16> asin(Vector<float, 16> a);
    Vector<float, 16> acos(Vector<float, 16> a);
    Vector<float, 16> atan(Vector<float, 16> a);
    Vector<float, 16> atan2(Vector<float, 16> a, Vector<float, 16> b);
    Vector<float, 16> pow(Vector<float, 16> a, Vector<float, 16> b);

} // namespace mango::math
