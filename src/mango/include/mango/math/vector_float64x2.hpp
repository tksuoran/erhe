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
            simd::f64x2 m;

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

    static inline const Vector<double, 2> operator + (Vector<double, 2> v)
    {
        return v;
    }

    static inline Vector<double, 2> operator - (Vector<double, 2> v)
    {
        return simd::neg(v);
    }

    static inline Vector<double, 2>& operator += (Vector<double, 2>& a, Vector<double, 2> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<double, 2>& operator -= (Vector<double, 2>& a, Vector<double, 2> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<double, 2>& operator *= (Vector<double, 2>& a, Vector<double, 2> b)
    {
        a = simd::mul(a, b);
        return a;
    }

    template <typename VT, int I>
    static inline Vector<double, 2>& operator /= (Vector<double, 2>& a, ScalarAccessor<double, VT, I> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 2>& operator /= (Vector<double, 2>& a, Vector<double, 2> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 2>& operator /= (Vector<double, 2>& a, double b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 2> operator + (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<double, 2> operator - (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<double, 2> operator * (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::mul(a, b);
    }

    template <typename VT, int I>
    static inline Vector<double, 2> operator / (Vector<double, 2> a, ScalarAccessor<double, VT, I> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<double, 2> operator / (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<double, 2> operator / (Vector<double, 2> a, double b)
    {
        return simd::div(a, b);
    }

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    static inline Vector<double, 2> add(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<double, 2> add(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask, Vector<double, 2> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<double, 2> sub(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<double, 2> sub(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask, Vector<double, 2> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<double, 2> mul(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask)
    {
        return simd::mul(a, b, mask);
    }

    static inline Vector<double, 2> mul(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask, Vector<double, 2> value)
    {
        return simd::mul(a, b, mask, value);
    }

    static inline Vector<double, 2> div(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask)
    {
        return simd::div(a, b, mask);
    }

    static inline Vector<double, 2> div(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask, Vector<double, 2> value)
    {
        return simd::div(a, b, mask, value);
    }

    static inline Vector<double, 2> abs(Vector<double, 2> a)
    {
        return simd::abs(a);
    }

    static inline double square(Vector<double, 2> a)
    {
        return simd::square(a);
    }

    static inline double length(Vector<double, 2> a)
    {
        return simd::length(a);
    }

    static inline Vector<double, 2> normalize(Vector<double, 2> a)
    {
        return simd::normalize(a);
    }

    static inline Vector<double, 2> sqrt(Vector<double, 2> a)
    {
        return simd::sqrt(a);
    }

    static inline Vector<double, 2> rsqrt(Vector<double, 2> a)
    {
        return simd::rsqrt(a);
    }

    static inline Vector<double, 2> rcp(Vector<double, 2> a)
    {
        return simd::rcp(a);
    }

    static inline Vector<double, 2> round(Vector<double, 2> a)
    {
        return simd::round(a);
    }

    static inline Vector<double, 2> trunc(Vector<double, 2> a)
    {
        return simd::trunc(a);
    }

    static inline Vector<double, 2> floor(Vector<double, 2> a)
    {
        return simd::floor(a);
    }

    static inline Vector<double, 2> ceil(Vector<double, 2> a)
    {
        return simd::ceil(a);
    }

    static inline Vector<double, 2> fract(Vector<double, 2> a)
    {
        return simd::fract(a);
    }

    static inline double dot(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::dot2(a, b);
    }

    static inline Vector<double, 2> unpacklo(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<double, 2> unpackhi(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<double, 2> min(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<double, 2> min(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<double, 2> min(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask, Vector<double, 2> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<double, 2> max(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<double, 2> max(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<double, 2> max(Vector<double, 2> a, Vector<double, 2> b, mask64x2 mask, Vector<double, 2> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<double, 2> clamp(Vector<double, 2> a, Vector<double, 2> low, Vector<double, 2> high)
    {
        return simd::clamp(a, low, high);
    }

    static inline Vector<double, 2> madd(Vector<double, 2> a, Vector<double, 2> b, Vector<double, 2> c)
    {
        return simd::madd(a, b, c);
    }

    static inline Vector<double, 2> msub(Vector<double, 2> a, Vector<double, 2> b, Vector<double, 2> c)
    {
        return simd::msub(a, b, c);
    }

    static inline Vector<double, 2> nmadd(Vector<double, 2> a, Vector<double, 2> b, Vector<double, 2> c)
    {
        return simd::nmadd(a, b, c);
    }

    static inline Vector<double, 2> nmsub(Vector<double, 2> a, Vector<double, 2> b, Vector<double, 2> c)
    {
        return simd::nmsub(a, b, c);
    }

    static inline Vector<double, 2> lerp(Vector<double, 2> a, Vector<double, 2> b, double factor)
    {
        Vector<double, 2> s(factor);
        return simd::lerp(a, b, s);
    }

    static inline Vector<double, 2> lerp(Vector<double, 2> a, Vector<double, 2> b, Vector<double, 2> factor)
    {
        return simd::lerp(a, b, factor);
    }

    template <int x, int y>
    static inline Vector<double, 2> shuffle(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::shuffle<x, y>(a, b);
    }

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    /* These come from default implementation
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

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<double, 2> nand(Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<double, 2> operator & (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<double, 2> operator | (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<double, 2> operator ^ (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<double, 2> operator ~ (Vector<double, 2> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
    // compare / select
    // ------------------------------------------------------------------

    static inline mask64x2 operator > (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask64x2 operator >= (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask64x2 operator < (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask64x2 operator <= (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask64x2 operator == (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask64x2 operator != (Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<double, 2> select(mask64x2 mask, Vector<double, 2> a, Vector<double, 2> b)
    {
        return simd::select(mask, a, b);
    }

} // namespace mango::math
