/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s32, 4>
    {
        using VectorType = simd::s32x4;
        using ScalarType = s32;
        enum { VectorSize = 4 };

        union
        {
            simd::s32x4 m {};

            ScalarAccessor<s32, simd::s32x4, 0> x;
            ScalarAccessor<s32, simd::s32x4, 1> y;
            ScalarAccessor<s32, simd::s32x4, 2> z;
            ScalarAccessor<s32, simd::s32x4, 3> w;

            // generate 2 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor4x2<s32, simd::s32x4, A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR2

            // generate 3 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR3(A, B, C, NAME) \
            ShuffleAccessor4x3<s32, simd::s32x4, A, B, C> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR3

            // generate 4 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR4(A, B, C, D, NAME) \
            ShuffleAccessor4<s32, simd::s32x4, A, B, C, D> NAME
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

        Vector(s32 s)
            : m(simd::s32x4_set(s))
        {
        }

        explicit Vector(s32 x, s32 y, s32 z, s32 w)
            : m(simd::s32x4_set(x, y, z, w))
        {
        }

        Vector(simd::s32x4 v)
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

        Vector& operator = (simd::s32x4 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s32 s)
        {
            m = simd::s32x4_set(s);
            return *this;
        }

        operator simd::s32x4 () const
        {
            return m;
        }

#ifdef simd_int128_is_hardware_vector
        operator simd::s32x4::vector () const
        {
            return m.data;
        }
#endif

        u32 pack() const
        {
            return simd::pack(m);
        }

        void unpack(u32 a)
        {
            m = simd::unpack(a);
        }

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3);
        }
    };

    template <int x, int y, int z, int w>
    static inline Vector<s32, 4> shuffle(Vector<s32, 4> v)
    {
        return simd::shuffle<x, y, z, w>(v);
    }

    template <>
    inline Vector<s32, 4> load_low<s32, 4>(const s32 *source)
    {
        return simd::s32x4_load_low(source);
    }

    static inline void store_low(s32 *dest, Vector<s32, 4> v)
    {
        simd::s32x4_store_low(dest, v);
    }

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(s32, 4);
    MATH_SIMD_SIGNED_INTEGER_OPERATORS(s32, 4);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(s32, 4, mask32x4);
    MATH_SIMD_SATURATING_INTEGER_FUNCTIONS(s32, 4, mask32x4);
    MATH_SIMD_ABS_INTEGER_FUNCTIONS(s32, 4, mask32x4);

    static inline Vector<s32, 4> hadd(Vector<s32, 4> a, Vector<s32, 4> b)
    {
        return simd::hadd(a, b);
    }

    static inline Vector<s32, 4> hsub(Vector<s32, 4> a, Vector<s32, 4> b)
    {
        return simd::hsub(a, b);
    }

    MATH_SIMD_BITWISE_FUNCTIONS(s32, 4);
    MATH_SIMD_COMPARE_FUNCTIONS(s32, 4, mask32x4);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<s32, 4> operator << (Vector<s32, 4> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<s32, 4> operator >> (Vector<s32, 4> a, int b)
    {
        return simd::sra(a, b);
    }

    static inline Vector<s32, 4> operator << (Vector<s32, 4> a, Vector<u32, 4> b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<s32, 4> operator >> (Vector<s32, 4> a, Vector<u32, 4> b)
    {
        return simd::sra(a, b);
    }

} // namespace mango::math
