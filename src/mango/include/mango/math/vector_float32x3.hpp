/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/math/vector_float32x2.hpp>

namespace mango::math
{

    // ------------------------------------------------------------------
    // Vector<float, 3>
    // ------------------------------------------------------------------

    template <>
    struct Vector<float, 3>
    {
        using VectorType = void;
        using ScalarType = float;
        enum { VectorSize = 3 };

        template <int X, int Y>
        struct ShuffleAccessor2
        {
			float v[3];

			operator Vector<float, 2> () const
			{
				return Vector<float, 2>(v[X], v[Y]);
			}
        };

        template <int X, int Y, int Z>
        struct ShuffleAccessor3
        {
			float v[3];

			operator Vector<float, 3> () const
			{
				return Vector<float, 3>(v[X], v[Y], v[Z]);
			}
        };

        union
        {
            struct { float x, y, z; };

            // generate 2 component accessors
#define VECTOR3_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor2<A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR3_SHUFFLE_ACCESSOR2

            // generate 3 component accessors
#define VECTOR3_SHUFFLE_ACCESSOR3(A, B, C, NAME) \
            ShuffleAccessor3<A, B, C> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR3_SHUFFLE_ACCESSOR3
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
        {
			x = s;
			y = s;
			z = s;
        }

        explicit Vector(float s0, float s1, float s2)
        {
			x = s0;
			y = s1;
			z = s2;
        }

		explicit Vector(const Vector<float, 2>& v, float s)
		{
			x = v.x;
			y = v.y;
			z = s;
		}

		explicit Vector(float s, const Vector<float, 2>& v)
		{
			x = s;
			y = v.x;
			z = v.y;
		}

        Vector(const Vector& v)
        {
			x = v.x;
			y = v.y;
			z = v.z;
        }

#if 0
        template <int X, int Y, int Z>
        Vector(const ShuffleAccessor3<X, Y, Z>& p)
        {
			const float* v = p.v;
			x = v[X];
			y = v[Y];
			z = v[Z];
        }

        template <int X, int Y, int Z>
        Vector& operator = (const ShuffleAccessor3<X, Y, Z>& p)
        {
			const float* v = p.v;
			x = v[X];
			y = v[Y];
			z = v[Z];
            return *this;
        }
#endif

        Vector& operator = (float s)
        {
			x = s;
			y = s;
			z = s;
            return *this;
        }

        Vector& operator = (const Vector& v)
        {
			x = v.x;
			y = v.y;
			z = v.z;
            return *this;
        }

        static Vector ascend()
        {
            return Vector(0.0f, 1.0f, 2.0f);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    static inline const Vector<float, 3>& operator + (const Vector<float, 3>& v)
    {
        return v;
    }

    static inline Vector<float, 3> operator - (const Vector<float, 3>& v)
    {
        return Vector<float, 3>(-v.x, -v.y, -v.z);
    }

    static inline Vector<float, 3>& operator += (Vector<float, 3>& a, const Vector<float, 3>& b)
    {
        a.x += b.x;
        a.y += b.y;
        a.z += b.z;
        return a;
    }

    static inline Vector<float, 3>& operator -= (Vector<float, 3>& a, const Vector<float, 3>& b)
    {
        a.x -= b.x;
        a.y -= b.y;
        a.z -= b.z;
        return a;
    }

    static inline Vector<float, 3>& operator *= (Vector<float, 3>& a, const Vector<float, 3>& b)
    {
        a.x *= b.x;
        a.y *= b.y;
        a.z *= b.z;
        return a;
    }

    static inline Vector<float, 3>& operator *= (Vector<float, 3>& a, float b)
    {
        a.x *= b;
        a.y *= b;
        a.z *= b;
        return a;
    }

    template <typename VT, int I>
    static inline Vector<float, 3>& operator /= (Vector<float, 3>& a, ScalarAccessor<float, VT, I> b)
    {
        float s = b;
        a.x /= s;
        a.y /= s;
        a.z /= s;
        return a;
    }

    static inline Vector<float, 3>& operator /= (Vector<float, 3>& a, const Vector<float, 3>& b)
    {
        a.x /= b.x;
        a.y /= b.y;
        a.z /= b.z;
        return a;
    }

    static inline Vector<float, 3>& operator /= (Vector<float, 3>& a, float b)
    {
        b = 1.0f / b;
        a.x *= b;
        a.y *= b;
        a.z *= b;
        return a;
    }

    static inline Vector<float, 3> operator + (Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = a.x + b.x;
        float y = a.y + b.y;
        float z = a.z + b.z;
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> operator - (Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = a.x - b.x;
        float y = a.y - b.y;
        float z = a.z - b.z;
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> operator * (Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = a.x * b.x;
        float y = a.y * b.y;
        float z = a.z * b.z;
        return Vector<float, 3>(x, y, z);
    }

    template <typename VT, int I>
    static inline Vector<float, 3> operator / (Vector<float, 3> a, ScalarAccessor<float, VT, I> b)
    {
        float s = b;
        float x = a.x / s;
        float y = a.y / s;
        float z = a.z / s;
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> operator / (Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = a.x / b.x;
        float y = a.y / b.y;
        float z = a.z / b.z;
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> operator / (Vector<float, 3> a, float b)
    {
        float s = 1.0f / b;
        float x = a.x * s;
        float y = a.y * s;
        float z = a.z * s;
        return Vector<float, 3>(x, y, z);
    }

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    static inline Vector<float, 3> abs(Vector<float, 3> a)
    {
        float x = std::abs(a.x);
        float y = std::abs(a.y);
        float z = std::abs(a.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline float square(Vector<float, 3> a)
    {
        return a.x * a.x + a.y * a.y + a.z * a.z;
    }

    static inline float length(Vector<float, 3> a)
    {
        return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    }

    static inline Vector<float, 3> normalize(Vector<float, 3> a)
    {
        float s = 1.0f / length(a);
        return a * s;
    }

    static inline Vector<float, 3> min(Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = std::min(a.x, b.x);
        float y = std::min(a.y, b.y);
        float z = std::min(a.z, b.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> max(Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = std::max(a.x, b.x);
        float y = std::max(a.y, b.y);
        float z = std::max(a.z, b.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline float dot(Vector<float, 3> a, Vector<float, 3> b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    static inline Vector<float, 3> cross(Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = a.y * b.z - a.z * b.y;
        float y = a.z * b.x - a.x * b.z;
        float z = a.x * b.y - a.y * b.x;
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> clamp(const Vector<float, 3>& a, const Vector<float, 3>& a_min, const Vector<float, 3>& a_max)
    {
        const float x = std::max(a_min.x, std::min(a_max.x, a.x));
        const float y = std::max(a_min.y, std::min(a_max.y, a.y));
        const float z = std::max(a_min.z, std::min(a_max.z, a.z));
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> lerp(const Vector<float, 3>& a, const Vector<float, 3>& b, float factor)
    {
        const float x = a.x + (b.x - a.x) * factor;
        const float y = a.y + (b.y - a.y) * factor;
        const float z = a.z + (b.z - a.z) * factor;
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> lerp(const Vector<float, 3>& a, const Vector<float, 3>& b, const Vector<float, 3>& factor)
    {
        const float x = a.x + (b.x - a.x) * factor.x;
        const float y = a.y + (b.y - a.y) * factor.y;
        const float z = a.z + (b.z - a.z) * factor.z;
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> hmin(const Vector<float, 3>& v)
    {
        const float s = std::min(std::min(v.x, v.y), v.z);
        return Vector<float, 3>(s);
    }

    static inline Vector<float, 3> hmax(const Vector<float, 3>& v)
    {
        const float s = std::max(std::max(v.x, v.y), v.z);
        return Vector<float, 3>(s);
    }

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    static inline Vector<float, 3> sin(Vector<float, 3> v)
    {
        float x = std::sin(v.x);
        float y = std::sin(v.y);
        float z = std::sin(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> cos(Vector<float, 3> v)
    {
        float x = std::cos(v.x);
        float y = std::cos(v.y);
        float z = std::cos(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> tan(Vector<float, 3> v)
    {
        float x = std::tan(v.x);
        float y = std::tan(v.y);
        float z = std::tan(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> exp(Vector<float, 3> v)
    {
        float x = std::exp(v.x);
        float y = std::exp(v.y);
        float z = std::exp(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> exp2(Vector<float, 3> v)
    {
        float x = std::exp2(v.x);
        float y = std::exp2(v.y);
        float z = std::exp2(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> log(Vector<float, 3> v)
    {
        float x = std::log(v.x);
        float y = std::log(v.y);
        float z = std::log(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> log2(Vector<float, 3> v)
    {
        float x = std::log2(v.x);
        float y = std::log2(v.y);
        float z = std::log2(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> asin(Vector<float, 3> v)
    {
        float x = std::asin(v.x);
        float y = std::asin(v.y);
        float z = std::asin(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> acos(Vector<float, 3> v)
    {
        float x = std::acos(v.x);
        float y = std::acos(v.y);
        float z = std::acos(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> atan(Vector<float, 3> v)
    {
        float x = std::atan(v.x);
        float y = std::atan(v.y);
        float z = std::atan(v.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> atan2(Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = std::atan2(a.x, b.x);
        float y = std::atan2(a.y, b.y);
        float z = std::atan2(a.z, b.z);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 3> pow(Vector<float, 3> a, Vector<float, 3> b)
    {
        float x = std::pow(a.x, b.x);
        float y = std::pow(a.y, b.y);
        float z = std::pow(a.z, b.z);
        return Vector<float, 3>(x, y, z);
    }

} // namespace mango::math
