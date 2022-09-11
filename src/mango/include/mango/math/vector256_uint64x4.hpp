/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<u64, 4>
    {
        using VectorType = simd::u64x4;
        using ScalarType = u64;
        enum { VectorSize = 4 };

        union
        {
            simd::u64x4 m;

            ScalarAccessor<u64, simd::u64x4, 0> x;
            ScalarAccessor<u64, simd::u64x4, 1> y;
            ScalarAccessor<u64, simd::u64x4, 2> z;
            ScalarAccessor<u64, simd::u64x4, 3> w;

            // generate 2 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR2(A, B, NAME) \
            ShuffleAccessor4x2<u64, simd::u64x4, A, B> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR2

            // generate 3 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR3(A, B, C, NAME) \
            ShuffleAccessor4x3<u64, simd::u64x4, A, B, C> NAME
#include <mango/math/accessor.hpp>
#undef VECTOR4_SHUFFLE_ACCESSOR3

            // generate 4 component accessors
#define VECTOR4_SHUFFLE_ACCESSOR4(A, B, C, D, NAME) \
            ShuffleAccessor4<u64, simd::u64x4, A, B, C, D> NAME
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

        Vector(u64 s)
            : m(simd::u64x4_set(s))
        {
        }

        explicit Vector(u64 x, u64 y, u64 z, u64 w)
            : m(simd::u64x4_set(x, y, z, w))
        {
        }

        Vector(simd::u64x4 v)
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

        Vector& operator = (simd::u64x4 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (u64 s)
        {
            m = simd::u64x4_set(s);
            return *this;
        }

        operator simd::u64x4 () const
        {
            return m;
        }

#ifdef simd_int256_is_hardware_vector
        operator simd::u64x4::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(u64, 4);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(u64, 4, mask64x4);

    MATH_SIMD_BITWISE_FUNCTIONS(u64, 4);
    MATH_SIMD_COMPARE_FUNCTIONS(u64, 4, mask64x4);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<u64, 4> operator << (Vector<u64, 4> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u64, 4> operator >> (Vector<u64, 4> a, int b)
    {
        return simd::srl(a, b);
    }

} // namespace mango::math
