/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/math/vector_float32x4.hpp>

namespace mango::math
{

    template <>
    struct Vector<float16, 4>
    {
        using VectorType = simd::f16x4;
        using ScalarType = float16;
        enum { VectorSize = 4 };

        simd::f16x4 m;

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

        explicit Vector(const Vector<float, 4>& v)
            : m(simd::convert<simd::f16x4>(v.m))
        {
        }

        Vector(simd::f32x4 v)
            : m(simd::convert<simd::f16x4>(v))
        {
        }

        Vector(simd::f16x4 v)
            : m(v)
        {
        }

        explicit Vector(float16 x, float16 y, float16 z, float16 w)
        {
            float16* v = data();
            v[0] = x;
            v[1] = y;
            v[2] = z;
            v[3] = w;
        }

        explicit Vector(float x, float y, float z, float w)
            : m(simd::convert<simd::f16x4>(simd::f32x4_set(x, y, z, w)))
        {
        }

        Vector& operator = (const Vector<float, 4>& v)
        {
            m = simd::convert<simd::f16x4>(v.m);
            return *this;
        }

        Vector& operator = (simd::f32x4 v)
        {
            m = simd::convert<simd::f16x4>(v);
            return *this;
        }

        Vector& operator = (simd::f16x4 v)
        {
            m = v;
            return *this;
        }

        Vector(const Vector& v) = default;

        Vector& operator = (const Vector& v)
        {
            m = v.m;
            return *this;
        }

        operator Vector<float, 4> () const
        {
            return Vector<float, 4>(simd::convert<simd::f32x4>(m));
        }

        operator const simd::f16x4& () const
        {
            return m;
        }

        operator simd::f16x4& ()
        {
            return m;
        }
    };

} // namespace mango::math
