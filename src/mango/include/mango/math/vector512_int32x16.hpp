/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s32, 16>
    {
        using VectorType = simd::s32x16;
        using ScalarType = s32;
        enum { VectorSize = 16 };

        VectorType m;

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
            : m(simd::s32x16_set(s))
        {
        }

        Vector(s32 v0, s32 v1, s32 v2, s32 v3, s32 v4, s32 v5, s32 v6, s32 v7,
            s32 v8, s32 v9, s32 v10, s32 v11, s32 v12, s32 v13, s32 v14, s32 v15)
            : m(simd::s32x16_set(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15))
        {
        }

        Vector(simd::s32x16 v)
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

        Vector& operator = (simd::s32x16 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s32 s)
        {
            m = simd::s32x16_set(s);
            return *this;
        }

        operator simd::s32x16 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::s32x16::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(s32, 16);
    MATH_SIMD_SIGNED_INTEGER_OPERATORS(s32, 16);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(s32, 16, mask32x16);
    MATH_SIMD_SATURATING_INTEGER_FUNCTIONS(s32, 16, mask32x16);
    MATH_SIMD_ABS_INTEGER_FUNCTIONS(s32, 16, mask32x16);

    MATH_SIMD_BITWISE_FUNCTIONS(s32, 16);
    MATH_SIMD_COMPARE_FUNCTIONS(s32, 16, mask32x16);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<s32, 16> operator << (Vector<s32, 16> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<s32, 16> operator >> (Vector<s32, 16> a, int b)
    {
        return simd::sra(a, b);
    }

    static inline Vector<s32, 16> operator << (Vector<s32, 16> a, Vector<u32, 16> b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<s32, 16> operator >> (Vector<s32, 16> a, Vector<u32, 16> b)
    {
        return simd::sra(a, b);
    }

} // namespace mango::math
