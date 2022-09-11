/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<u32, 4>
    {
        using VectorType = simd::u32x4;
        using ScalarType = u32;
        enum { VectorSize = 4 };

        union
        {
            simd::u32x4 m {};

            ScalarAccessor<u32, simd::u32x4, 0> x;
            ScalarAccessor<u32, simd::u32x4, 1> y;
            ScalarAccessor<u32, simd::u32x4, 2> z;
            ScalarAccessor<u32, simd::u32x4, 3> w;

            // generate 2 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor4x2<u32, simd::u32x4, A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR2

            // generate 3 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR3(A, B, C, NAME) \
            ShuffleAccessor4x3<u32, simd::u32x4, A, B, C> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR3

            // generate 4 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR4(A, B, C, D, NAME) \
            ShuffleAccessor4<u32, simd::u32x4, A, B, C, D> NAME
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

        Vector(u32 s)
            : m(simd::u32x4_set(s))
        {
        }

        Vector(u32 x, u32 y, u32 z, u32 w)
            : m(simd::u32x4_set(x, y, z, w))
        {
        }

        Vector(simd::u32x4 v)
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

        Vector& operator = (simd::u32x4 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (u32 s)
        {
            m = simd::u32x4_set(s);
            return *this;
        }

        operator simd::u32x4 () const
        {
            return m;
        }

#ifdef simd_int128_is_hardware_vector
        operator simd::u32x4::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3);
        }
    };

    template <int x, int y, int z, int w>
    static inline Vector<u32, 4> shuffle(Vector<u32, 4> v)
    {
        return simd::shuffle<x, y, z, w>(v);
    }

    template <>
    inline Vector<u32, 4> load_low<u32, 4>(const u32 *source)
    {
        return simd::u32x4_load_low(source);
    }

    static inline void store_low(u32 *dest, Vector<u32, 4> v)
    {
        simd::u32x4_store_low(dest, v);
    }

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(u32, 4);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(u32, 4, mask32x4);
    MATH_SIMD_SATURATING_INTEGER_FUNCTIONS(u32, 4, mask32x4);

    MATH_SIMD_BITWISE_FUNCTIONS(u32, 4);
    MATH_SIMD_COMPARE_FUNCTIONS(u32, 4, mask32x4);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<u32, 4> operator << (Vector<u32, 4> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u32, 4> operator >> (Vector<u32, 4> a, int b)
    {
        return simd::srl(a, b);
    }

    static inline Vector<u32, 4> operator << (Vector<u32, 4> a, Vector<u32, 4> b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u32, 4> operator >> (Vector<u32, 4> a, Vector<u32, 4> b)
    {
        return simd::srl(a, b);
    }

} // namespace mango::math
