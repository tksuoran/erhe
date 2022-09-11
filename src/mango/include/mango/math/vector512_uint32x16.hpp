/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<u32, 16>
    {
        using VectorType = simd::u32x16;
        using ScalarType = u32;
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

        Vector(u32 s)
            : m(simd::u32x16_set(s))
        {
        }

        Vector(u32 s0, u32 s1, u32 s2, u32 s3, u32 s4, u32 s5, u32 s6, u32 s7,
            u32 s8, u32 s9, u32 s10, u32 s11, u32 s12, u32 s13, u32 s14, u32 s15)
            : m(simd::u32x16_set(s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11, s12, s13, s14, s15))
        {
        }

        Vector(simd::u32x16 v)
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

        Vector& operator = (simd::u32x16 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (u32 s)
        {
            m = simd::u32x16_set(s);
            return *this;
        }

        operator simd::u32x16 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::u32x16::vector () const
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

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(u32, 16);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(u32, 16, mask32x16);
    MATH_SIMD_SATURATING_INTEGER_FUNCTIONS(u32, 16, mask32x16);

    MATH_SIMD_BITWISE_FUNCTIONS(u32, 16);
    MATH_SIMD_COMPARE_FUNCTIONS(u32, 16, mask32x16);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<u32, 16> operator << (Vector<u32, 16> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u32, 16> operator >> (Vector<u32, 16> a, int b)
    {
        return simd::srl(a, b);
    }

    static inline Vector<u32, 16> operator << (Vector<u32, 16> a, Vector<u32, 16> b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u32, 16> operator >> (Vector<u32, 16> a, Vector<u32, 16> b)
    {
        return simd::srl(a, b);
    }

} // namespace mango::math
