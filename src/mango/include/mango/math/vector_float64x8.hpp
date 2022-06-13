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

    static inline Vector<double, 8> operator + (Vector<double, 8> v)
    {
        return v;
    }

    static inline Vector<double, 8> operator - (Vector<double, 8> v)
    {
        return simd::neg(v);
    }

    static inline Vector<double, 8>& operator += (Vector<double, 8>& a, Vector<double, 8> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<double, 8>& operator -= (Vector<double, 8>& a, Vector<double, 8> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<double, 8>& operator *= (Vector<double, 8>& a, Vector<double, 8> b)
    {
        a = simd::mul(a, b);
        return a;
    }

    template <typename VT, int I>
    static inline Vector<double, 8>& operator /= (Vector<double, 8>& a, ScalarAccessor<double, VT, I> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 8>& operator /= (Vector<double, 8>& a, Vector<double, 8> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 8>& operator /= (Vector<double, 8>& a, double b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<double, 8> operator + (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<double, 8> operator - (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<double, 8> operator * (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::mul(a, b);
    }

    template <typename VT, int I>
    static inline Vector<double, 8> operator / (Vector<double, 8> a, ScalarAccessor<double, VT, I> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<double, 8> operator / (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<double, 8> operator / (Vector<double, 8> a, double b)
    {
        return simd::div(a, b);
    }

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    static inline Vector<double, 8> add(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<double, 8> add(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask, Vector<double, 8> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<double, 8> sub(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<double, 8> sub(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask, Vector<double, 8> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<double, 8> mul(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask)
    {
        return simd::mul(a, b, mask);
    }

    static inline Vector<double, 8> mul(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask, Vector<double, 8> value)
    {
        return simd::mul(a, b, mask, value);
    }

    static inline Vector<double, 8> div(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask)
    {
        return simd::div(a, b, mask);
    }

    static inline Vector<double, 8> div(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask, Vector<double, 8> value)
    {
        return simd::div(a, b, mask, value);
    }

    static inline Vector<double, 8> abs(Vector<double, 8> a)
    {
        return simd::abs(a);
    }

    static inline Vector<double, 8> round(Vector<double, 8> a)
    {
        return simd::round(a);
    }

    static inline Vector<double, 8> floor(Vector<double, 8> a)
    {
        return simd::floor(a);
    }

    static inline Vector<double, 8> ceil(Vector<double, 8> a)
    {
        return simd::ceil(a);
    }

    static inline Vector<double, 8> trunc(Vector<double, 8> a)
    {
        return simd::trunc(a);
    }

    static inline Vector<double, 8> fract(Vector<double, 8> a)
    {
        return simd::fract(a);
    }

    static inline Vector<double, 8> sqrt(Vector<double, 8> a)
    {
        return simd::sqrt(a);
    }

    static inline Vector<double, 8> rsqrt(Vector<double, 8> a)
    {
        return simd::rsqrt(a);
    }

    static inline Vector<double, 8> rcp(Vector<double, 8> a)
    {
        return simd::rcp(a);
    }

    static inline Vector<double, 8> unpacklo(Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<double, 8> unpackhi(Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<double, 8> min(Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<double, 8> min(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<double, 8> min(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask, Vector<double, 8> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<double, 8> max(Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<double, 8> max(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<double, 8> max(Vector<double, 8> a, Vector<double, 8> b, mask64x8 mask, Vector<double, 8> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<double, 8> clamp(Vector<double, 8> a, Vector<double, 8> low, Vector<double, 8> high)
    {
        return simd::max(low, simd::min(high, a));
    }

    static inline Vector<double, 8> madd(Vector<double, 8> a, Vector<double, 8> b, Vector<double, 8> c)
    {
        return simd::madd(a, b, c);
    }

    static inline Vector<double, 8> msub(Vector<double, 8> a, Vector<double, 8> b, Vector<double, 8> c)
    {
        return simd::msub(a, b, c);
    }

    static inline Vector<double, 8> nmadd(Vector<double, 8> a, Vector<double, 8> b, Vector<double, 8> c)
    {
        return simd::nmadd(a, b, c);
    }

    static inline Vector<double, 8> nmsub(Vector<double, 8> a, Vector<double, 8> b, Vector<double, 8> c)
    {
        return simd::nmsub(a, b, c);
    }

    static inline Vector<double, 8> lerp(Vector<double, 8> a, Vector<double, 8> b, double factor)
    {
        Vector<double, 8> s(factor);
        return simd::lerp(a, b, s);
    }

    static inline Vector<double, 8> lerp(Vector<double, 8> a, Vector<double, 8> b, Vector<double, 8> factor)
    {
        return simd::lerp(a, b, factor);
    }

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    /* These come from default implementation
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

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<double, 8> nand(Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<double, 8> operator & (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<double, 8> operator | (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<double, 8> operator ^ (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<double, 8> operator ~ (Vector<double, 8> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask64x8 operator > (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask64x8 operator >= (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask64x8 operator < (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask64x8 operator <= (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask64x8 operator == (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask64x8 operator != (Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<double, 8> select(mask64x8 mask, Vector<double, 8> a, Vector<double, 8> b)
    {
        return simd::select(mask, a, b);
    }

} // namespace mango::math
