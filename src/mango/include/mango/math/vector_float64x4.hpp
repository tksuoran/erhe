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
            simd::f64x4 m;

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

    static inline Vector<double, 4> operator + (Vector<double, 4> v)
    {
        return v;
    }

    static inline Vector<double, 4> operator - (Vector<double, 4> v)
    {
        return simd::neg(v);
    }

    static inline Vector<double, 4>& operator += (Vector<double, 4>& a, Vector<double, 4> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<double, 4>& operator -= (Vector<double, 4>& a, Vector<double, 4> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<double, 4>& operator *= (Vector<double, 4>& a, Vector<double, 4> b)
    {
        a = simd::mul(a, b);
        return a;
    }

    template <typename VT, int I>
    static inline Vector<double, 4>& operator /= (Vector<double, 4>& a, ScalarAccessor<double, VT, I> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 4>& operator /= (Vector<double, 4>& a, Vector<double, 4> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 4>& operator /= (Vector<double, 4>& a, double b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 4> operator + (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<double, 4> operator - (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<double, 4> operator * (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::mul(a, b);
    }

    template <typename VT, int I>
    static inline Vector<double, 4> operator / (Vector<double, 4> a, ScalarAccessor<double, VT, I> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<double, 4> operator / (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<double, 4> operator / (Vector<double, 4> a, double b)
    {
        return simd::div(a, b);
    }

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    static inline Vector<double, 4> add(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<double, 4> add(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask, Vector<double, 4> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<double, 4> sub(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<double, 4> sub(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask, Vector<double, 4> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<double, 4> mul(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask)
    {
        return simd::mul(a, b, mask);
    }

    static inline Vector<double, 4> mul(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask, Vector<double, 4> value)
    {
        return simd::mul(a, b, mask, value);
    }

    static inline Vector<double, 4> div(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask)
    {
        return simd::div(a, b, mask);
    }

    static inline Vector<double, 4> div(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask, Vector<double, 4> value)
    {
        return simd::div(a, b, mask, value);
    }

    static inline Vector<double, 4> abs(Vector<double, 4> a)
    {
        return simd::abs(a);
    }

    static inline double square(Vector<double, 4> a)
    {
        return simd::square(a);
    }

    static inline double length(Vector<double, 4> a)
    {
        return simd::length(a);
    }

    static inline Vector<double, 4> normalize(Vector<double, 4> a)
    {
        return simd::normalize(a);
    }

    static inline Vector<double, 4> round(Vector<double, 4> a)
    {
        return simd::round(a);
    }

    static inline Vector<double, 4> floor(Vector<double, 4> a)
    {
        return simd::floor(a);
    }

    static inline Vector<double, 4> ceil(Vector<double, 4> a)
    {
        return simd::ceil(a);
    }

    static inline Vector<double, 4> trunc(Vector<double, 4> a)
    {
        return simd::trunc(a);
    }

    static inline Vector<double, 4> fract(Vector<double, 4> a)
    {
        return simd::fract(a);
    }

    static inline Vector<double, 4> sign(Vector<double, 4> a)
    {
        return simd::sign(a);
    }

    static inline Vector<double, 4> radians(Vector<double, 4> a)
    {
        return simd::radians(a);
    }

    static inline Vector<double, 4> degrees(Vector<double, 4> a)
    {
        return simd::degrees(a);
    }

    static inline Vector<double, 4> sqrt(Vector<double, 4> a)
    {
        return simd::sqrt(a);
    }

    static inline Vector<double, 4> rsqrt(Vector<double, 4> a)
    {
        return simd::rsqrt(a);
    }

    static inline Vector<double, 4> rcp(Vector<double, 4> a)
    {
        return simd::rcp(a);
    }

    static inline Vector<double, 4> unpacklo(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<double, 4> unpackhi(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<double, 4> min(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<double, 4> min(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<double, 4> min(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask, Vector<double, 4> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<double, 4> max(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<double, 4> max(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<double, 4> max(Vector<double, 4> a, Vector<double, 4> b, mask64x4 mask, Vector<double, 4> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline double dot(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::dot4(a, b);
    }

    static inline Vector<double, 4> mod(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::mod(a, b);
    }

    static inline Vector<double, 4> clamp(Vector<double, 4> a, Vector<double, 4> low, Vector<double, 4> high)
    {
        return simd::clamp(a, low, high);
    }

    static inline Vector<double, 4> madd(Vector<double, 4> a, Vector<double, 4> b, Vector<double, 4> c)
    {
        return simd::madd(a, b, c);
    }

    static inline Vector<double, 4> msub(Vector<double, 4> a, Vector<double, 4> b, Vector<double, 4> c)
    {
        return simd::msub(a, b, c);
    }

    static inline Vector<double, 4> nmadd(Vector<double, 4> a, Vector<double, 4> b, Vector<double, 4> c)
    {
        return simd::nmadd(a, b, c);
    }

    static inline Vector<double, 4> nmsub(Vector<double, 4> a, Vector<double, 4> b, Vector<double, 4> c)
    {
        return simd::nmsub(a, b, c);
    }

    static inline Vector<double, 4> lerp(Vector<double, 4> a, Vector<double, 4> b, double factor)
    {
        Vector<double, 4> s(factor);
        return simd::lerp(a, b, s);
    }

    static inline Vector<double, 4> lerp(Vector<double, 4> a, Vector<double, 4> b, Vector<double, 4> factor)
    {
        return simd::lerp(a, b, factor);
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

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    /* These come from default implementation
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

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<double, 4> nand(Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<double, 4> operator & (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<double, 4> operator | (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<double, 4> operator ^ (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<double, 4> operator ~ (Vector<double, 4> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask64x4 operator > (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask64x4 operator >= (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask64x4 operator < (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask64x4 operator <= (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask64x4 operator == (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask64x4 operator != (Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<double, 4> select(mask64x4 mask, Vector<double, 4> a, Vector<double, 4> b)
    {
        return simd::select(mask, a, b);
    }

} // namespace mango::math
