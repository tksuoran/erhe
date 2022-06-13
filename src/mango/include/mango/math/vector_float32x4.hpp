/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/math/vector_float32x2.hpp>
#include <mango/math/vector_float32x3.hpp>

namespace mango::math
{

    // ------------------------------------------------------------------
    // Vector<float, 4>
    // ------------------------------------------------------------------

    template <>
    struct Vector<float, 4>
    {
        using VectorType = simd::f32x4;
        using ScalarType = float;
        enum { VectorSize = 4 };

        union
        {
            simd::f32x4 m;

            LowAccessor<Vector<float, 2>, simd::f32x4> low;
            HighAccessor<Vector<float, 2>, simd::f32x4> high;

            ScalarAccessor<float, simd::f32x4, 0> x;
            ScalarAccessor<float, simd::f32x4, 1> y;
            ScalarAccessor<float, simd::f32x4, 2> z;
            ScalarAccessor<float, simd::f32x4, 3> w;

            // generate 2 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor4x2<float, simd::f32x4, A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR2

            // generate 3 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR3(A, B, C, NAME) \
            ShuffleAccessor4x3<float, simd::f32x4, A, B, C> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR3

            // generate 4 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR4(A, B, C, D, NAME) \
            ShuffleAccessor4<float, simd::f32x4, A, B, C, D> NAME
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

        Vector(float s)
            : m(simd::f32x4_set(s))
        {
        }

        explicit Vector(float x, float y, float z, float w)
            : m(simd::f32x4_set(x, y, z, w))
        {
        }

        explicit Vector(const Vector<float, 2>& v, float z, float w)
            : m(simd::f32x4_set(v.x, v.y, z, w))
        {
        }

        explicit Vector(float x, float y, const Vector<float, 2>& v)
            : m(simd::f32x4_set(x, y, v.x, v.y))
        {
        }

        explicit Vector(float x, const Vector<float, 2>& v, float w)
            :m(simd::f32x4_set(x, v.x, v.y, w))
        {
        }

        explicit Vector(const Vector<float, 2>& xy, const Vector<float, 2>& zw)
            : m(simd::f32x4_set(xy.x, xy.y, zw.x, zw.y))
        {
        }

        explicit Vector(const Vector<float, 3>& v, float w)
            : m(simd::f32x4_set(v.x, v.y, v.z, w))
        {
        }

        explicit Vector(float x, const Vector<float, 3>& v)
            : m(simd::f32x4_set(x, v.x, v.y, v.z))
        {
        }

        Vector(simd::f32x4 v)
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

        Vector& operator = (simd::f32x4 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (float s)
        {
            m = simd::f32x4_set(s);
            return *this;
        }

        operator simd::f32x4 () const
        {
            return m;
        }

#ifdef simd_float128_is_hardware_vector
        operator simd::f32x4::vector () const
        {
            return m.data;
        }
#endif

        u32 pack() const
        {
            const simd::s32x4 temp = simd::convert<simd::s32x4>(m);
            return simd::pack(temp);
        }

        void unpack(u32 a)
        {
            const simd::s32x4 temp = simd::unpack(a);
            m = simd::convert<simd::f32x4>(temp);
        }

        static Vector ascend()
        {
            return Vector(0.0f, 1.0f, 2.0f, 3.0f);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    static inline Vector<float, 4> operator + (Vector<float, 4> v)
    {
        return v;
    }

    static inline Vector<float, 4> operator - (Vector<float, 4> v)
    {
        return simd::neg(v);
    }

    static inline Vector<float, 4>& operator += (Vector<float, 4>& a, Vector<float, 4> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<float, 4>& operator -= (Vector<float, 4>& a, Vector<float, 4> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<float, 4>& operator *= (Vector<float, 4>& a, Vector<float, 4> b)
    {
        a = simd::mul(a, b);
        return a;
    }

    template <typename VT, int I>
    static inline Vector<float, 4>& operator /= (Vector<float, 4>& a, ScalarAccessor<float, VT, I> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<float, 4>& operator /= (Vector<float, 4>& a, Vector<float, 4> b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<float, 4>& operator /= (Vector<float, 4>& a, float b)
    {
        a = simd::div(a, b);
        return a;
    }

    static inline Vector<float, 4> operator + (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<float, 4> operator - (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<float, 4> operator * (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::mul(a, b);
    }

    template <typename VT, int I>
    static inline Vector<float, 4> operator / (Vector<float, 4> a, ScalarAccessor<float, VT, I> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<float, 4> operator / (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::div(a, b);
    }

    static inline Vector<float, 4> operator / (Vector<float, 4> a, float b)
    {
        return simd::div(a, b);
    }

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    static inline Vector<float, 4> add(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<float, 4> add(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask, Vector<float, 4> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<float, 4> sub(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<float, 4> sub(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask, Vector<float, 4> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<float, 4> mul(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask)
    {
        return simd::mul(a, b, mask);
    }

    static inline Vector<float, 4> mul(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask, Vector<float, 4> value)
    {
        return simd::mul(a, b, mask, value);
    }

    static inline Vector<float, 4> div(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask)
    {
        return simd::div(a, b, mask);
    }

    static inline Vector<float, 4> div(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask, Vector<float, 4> value)
    {
        return simd::div(a, b, mask, value);
    }

    static inline Vector<float, 4> abs(Vector<float, 4> a)
    {
        return simd::abs(a);
    }

    static inline float square(Vector<float, 4> a)
    {
        return simd::square(a);
    }

    static inline float length(Vector<float, 4> a)
    {
        return simd::length(a);
    }

    static inline Vector<float, 4> normalize(Vector<float, 4> a)
    {
        return simd::normalize(a);
    }

    static inline Vector<float, 4> round(Vector<float, 4> a)
    {
        return simd::round(a);
    }

    static inline Vector<float, 4> floor(Vector<float, 4> a)
    {
        return simd::floor(a);
    }

    static inline Vector<float, 4> ceil(Vector<float, 4> a)
    {
        return simd::ceil(a);
    }

    static inline Vector<float, 4> trunc(Vector<float, 4> a)
    {
        return simd::trunc(a);
    }

    static inline Vector<float, 4> fract(Vector<float, 4> a)
    {
        return simd::fract(a);
    }

    static inline Vector<float, 4> sign(Vector<float, 4> a)
    {
        return simd::sign(a);
    }

    static inline Vector<float, 4> radians(Vector<float, 4> a)
    {
        return simd::radians(a);
    }

    static inline Vector<float, 4> degrees(Vector<float, 4> a)
    {
        return simd::degrees(a);
    }

    static inline Vector<float, 4> sqrt(Vector<float, 4> a)
    {
        return simd::sqrt(a);
    }

    static inline Vector<float, 4> rsqrt(Vector<float, 4> a)
    {
        return simd::rsqrt(a);
    }

    static inline Vector<float, 4> rcp(Vector<float, 4> a)
    {
        return simd::rcp(a);
    }

    static inline Vector<float, 4> unpacklo(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<float, 4> unpackhi(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<float, 4> min(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<float, 4> min(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<float, 4> min(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask, Vector<float, 4> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<float, 4> max(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<float, 4> max(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<float, 4> max(Vector<float, 4> a, Vector<float, 4> b, mask32x4 mask, Vector<float, 4> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline float dot(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::dot4(a, b);
    }

    static inline Vector<float, 4> cross(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::cross3(a, b);
    }

    static inline Vector<float, 4> mod(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::mod(a, b);
    }

    static inline Vector<float, 4> clamp(Vector<float, 4> a, Vector<float, 4> low, Vector<float, 4> high)
    {
        return simd::clamp(a, low, high);
    }

    static inline Vector<float, 4> madd(Vector<float, 4> a, Vector<float, 4> b, Vector<float, 4> c)
    {
        return simd::madd(a, b, c);
    }

    static inline Vector<float, 4> msub(Vector<float, 4> a, Vector<float, 4> b, Vector<float, 4> c)
    {
        return simd::msub(a, b, c);
    }

    static inline Vector<float, 4> nmadd(Vector<float, 4> a, Vector<float, 4> b, Vector<float, 4> c)
    {
        return simd::nmadd(a, b, c);
    }

    static inline Vector<float, 4> nmsub(Vector<float, 4> a, Vector<float, 4> b, Vector<float, 4> c)
    {
        return simd::nmsub(a, b, c);
    }

    static inline Vector<float, 4> lerp(Vector<float, 4> a, Vector<float, 4> b, float factor)
    {
        Vector<float, 4> s(factor);
        return simd::lerp(a, b, s);
    }

    static inline Vector<float, 4> lerp(Vector<float, 4> a, Vector<float, 4> b, Vector<float, 4> factor)
    {
        return simd::lerp(a, b, factor);
    }

    static inline Vector<float, 4> hmin(Vector<float, 4> v)
    {
        return simd::hmin(v);
    }

    static inline Vector<float, 4> hmax(Vector<float, 4> v)
    {
        return simd::hmax(v);
    }

    template <int x, int y, int z, int w>
    static inline Vector<float, 4> shuffle(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::shuffle<x, y, z, w>(a, b);
    }

    static inline Vector<float, 4> movelh(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::movelh(a, b);
    }
    
    static inline Vector<float, 4> movehl(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::movehl(a, b);
    }

    // ------------------------------------------------------------------
    // trigonometric functions
    // ------------------------------------------------------------------

    Vector<float, 4> sin(Vector<float, 4> a);
    Vector<float, 4> cos(Vector<float, 4> a);
    Vector<float, 4> tan(Vector<float, 4> a);
    Vector<float, 4> exp(Vector<float, 4> a);
    Vector<float, 4> exp2(Vector<float, 4> a);
    Vector<float, 4> log(Vector<float, 4> a);
    Vector<float, 4> log2(Vector<float, 4> a);
    Vector<float, 4> asin(Vector<float, 4> a);
    Vector<float, 4> acos(Vector<float, 4> a);
    Vector<float, 4> atan(Vector<float, 4> a);
    Vector<float, 4> atan2(Vector<float, 4> a, Vector<float, 4> b);
    Vector<float, 4> pow(Vector<float, 4> a, Vector<float, 4> b);

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<float, 4> nand(Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<float, 4> operator & (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<float, 4> operator | (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<float, 4> operator ^ (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<float, 4> operator ~ (Vector<float, 4> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
    // compare / select
    // ------------------------------------------------------------------

    static inline mask32x4 operator > (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask32x4 operator >= (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask32x4 operator < (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask32x4 operator <= (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask32x4 operator == (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask32x4 operator != (Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<float, 4> select(mask32x4 mask, Vector<float, 4> a, Vector<float, 4> b)
    {
        return simd::select(mask, a, b);
    }

} // namespace mango::math
