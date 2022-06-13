/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    // ------------------------------------------------------------------
    // Vector<float, 2>
    // ------------------------------------------------------------------

    template <>
    struct Vector<float, 2>
    {
        using VectorType = void;
        using ScalarType = float;
        enum { VectorSize = 2 };

        template <int X, int Y>
        struct ShuffleAccessor2
        {
			float v[2];

			operator Vector<float, 2> () const
			{
				return Vector<float, 2>(v[X], v[Y]);
			}
        };

        union
        {
            struct { float x, y; };

            // generate 2 component accessors
#define VECTOR2_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor2<A, B> NAME
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

        Vector(float s)
        {
			x = s;
			y = s;
        }

        explicit Vector(float s0, float s1)
        {
			x = s0;
			y = s1;
        }

        Vector(const Vector& v)
        {
			x = v.x;
			y = v.y;
        }

#if 0
        template <int X, int Y>
        Vector(const ShuffleAccessor2<X, Y>& p)
        {
			const float* v = p.v;
			x = v[X];
			y = v[Y];
        }

        template <int X, int Y>
        Vector& operator = (const ShuffleAccessor2<X, Y>& p)
        {
			const float* v = p.v;
			x = v[X];
			y = v[Y];
            return *this;
        }
#endif

        Vector& operator = (float s)
        {
			x = s;
			y = s;
            return *this;
        }

        Vector& operator = (const Vector& v)
        {
			x = v.x;
			y = v.y;
            return *this;
        }

        static Vector ascend()
        {
            return Vector(0.0f, 1.0f);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    static inline const Vector<float, 2>& operator + (const Vector<float, 2>& v)
    {
        return v;
    }

    static inline Vector<float, 2> operator - (const Vector<float, 2>& v)
    {
        return Vector<float, 2>(-v.x, -v.y);
    }

    static inline Vector<float, 2>& operator += (Vector<float, 2>& a, const Vector<float, 2>& b)
    {
        a.x += b.x;
        a.y += b.y;
        return a;
    }

    static inline Vector<float, 2>& operator -= (Vector<float, 2>& a, const Vector<float, 2>& b)
    {
        a.x -= b.x;
        a.y -= b.y;
        return a;
    }

    static inline Vector<float, 2>& operator *= (Vector<float, 2>& a, const Vector<float, 2>& b)
    {
        a.x *= b.x;
        a.y *= b.y;
        return a;
    }

    static inline Vector<float, 2>& operator *= (Vector<float, 2>& a, float b)
    {
        a.x *= b;
        a.y *= b;
        return a;
    }

    template <typename VT, int I>
    static inline Vector<float, 2>& operator /= (Vector<float, 2>& a, ScalarAccessor<float, VT, I> b)
    {
        float s = b;
        a.x /= s;
        a.y /= s;
        return a;
    }

    static inline Vector<float, 2>& operator /= (Vector<float, 2>& a, const Vector<float, 2>& b)
    {
        a.x /= b.x;
        a.y /= b.y;
        return a;
    }

    static inline Vector<float, 2>& operator /= (Vector<float, 2>& a, float b)
    {
        b = 1.0f / b;
        a.x *= b;
        a.y *= b;
        return a;
    }

    static inline Vector<float, 2> operator + (Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = a.x + b.x;
        float y = a.y + b.y;
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> operator - (Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = a.x - b.x;
        float y = a.y - b.y;
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> operator * (Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = a.x * b.x;
        float y = a.y * b.y;
        return Vector<float, 2>(x, y);
    }

    template <typename VT, int I>
    static inline Vector<float, 2> operator / (Vector<float, 2> a, ScalarAccessor<float, VT, I> b)
    {
        float s = b;
        float x = a.x / s;
        float y = a.y / s;
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> operator / (Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = a.x / b.x;
        float y = a.y / b.y;
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> operator / (Vector<float, 2> a, float b)
    {
        float s = 1.0f / b;
        float x = a.x * s;
        float y = a.y * s;
        return Vector<float, 2>(x, y);
    }

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    static inline Vector<float, 2> abs(Vector<float, 2> a)
    {
        float x = std::abs(a.x);
        float y = std::abs(a.y);
        return Vector<float, 2>(x, y);
    }

    static inline float square(Vector<float, 2> a)
    {
        return a.x * a.x + a.y * a.y;
    }

    static inline float length(Vector<float, 2> a)
    {
        return std::sqrt(a.x * a.x + a.y * a.y);
    }

    static inline Vector<float, 2> normalize(Vector<float, 2> a)
    {
        float s = 1.0f / length(a);
        return a * s;
    }

    static inline Vector<float, 2> min(Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = std::min(a.x, b.x);
        float y = std::min(a.y, b.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> max(Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = std::max(a.x, b.x);
        float y = std::max(a.y, b.y);
        return Vector<float, 2>(x, y);
    }

    static inline float dot(Vector<float, 2> a, Vector<float, 2> b)
    {
        return a.x * b.x + a.y * b.y;
    }

    static inline Vector<float, 2> clamp(const Vector<float, 2>& a, const Vector<float, 2>& low, const Vector<float, 2>& high)
    {
        const float x = std::max(low.x, std::min(high.x, a.x));
        const float y = std::max(low.y, std::min(high.y, a.y));
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> lerp(const Vector<float, 2>& a, const Vector<float, 2>& b, float factor)
    {
        const float x = a.x + (b.x - a.x) * factor;
        const float y = a.y + (b.y - a.y) * factor;
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> lerp(const Vector<float, 2>& a, const Vector<float, 2>& b, const Vector<float, 2>& factor)
    {
        const float x = a.x + (b.x - a.x) * factor.x;
        const float y = a.y + (b.y - a.y) * factor.y;
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> hmin(const Vector<float, 2>& v)
    {
        const float s = std::min(v.x, v.y);
        return Vector<float, 2>(s);
    }

    static inline Vector<float, 2> hmax(const Vector<float, 2>& v)
    {
        const float s = std::max(v.x, v.y);
        return Vector<float, 2>(s);
    }

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    static inline Vector<float, 2> sin(Vector<float, 2> v)
    {
        float x = std::sin(v.x);
        float y = std::sin(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> cos(Vector<float, 2> v)
    {
        float x = std::cos(v.x);
        float y = std::cos(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> tan(Vector<float, 2> v)
    {
        float x = std::tan(v.x);
        float y = std::tan(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> exp(Vector<float, 2> v)
    {
        float x = std::exp(v.x);
        float y = std::exp(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> exp2(Vector<float, 2> v)
    {
        float x = std::exp2(v.x);
        float y = std::exp2(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> log(Vector<float, 2> v)
    {
        float x = std::log(v.x);
        float y = std::log(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> log2(Vector<float, 2> v)
    {
        float x = std::log2(v.x);
        float y = std::log2(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> asin(Vector<float, 2> v)
    {
        float x = std::asin(v.x);
        float y = std::asin(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> acos(Vector<float, 2> v)
    {
        float x = std::acos(v.x);
        float y = std::acos(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> atan(Vector<float, 2> v)
    {
        float x = std::atan(v.x);
        float y = std::atan(v.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> atan2(Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = std::atan2(a.x, b.x);
        float y = std::atan2(a.y, b.y);
        return Vector<float, 2>(x, y);
    }

    static inline Vector<float, 2> pow(Vector<float, 2> a, Vector<float, 2> b)
    {
        float x = std::pow(a.x, b.x);
        float y = std::pow(a.y, b.y);
        return Vector<float, 2>(x, y);
    }

} // namespace mango::math
