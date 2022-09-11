/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s16, 8>
    {
        using VectorType = simd::s16x8;
        using ScalarType = s16;
        enum { VectorSize = 8 };

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

        Vector(s16 s)
            : m(simd::s16x8_set(s))
        {
        }

        Vector(s16 s0, s16 s1, s16 s2, s16 s3, s16 s4, s16 s5, s16 s6, s16 s7)
            : m(simd::s16x8_set(s0, s1, s2, s3, s4, s5, s6, s7))
        {
        }

        Vector(simd::s16x8 v)
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

        Vector& operator = (simd::s16x8 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s16 s)
        {
            m = simd::s16x8_set(s);
            return *this;
        }

        operator simd::s16x8 () const
        {
            return m;
        }

#ifdef simd_int128_is_hardware_vector
        operator simd::s16x8::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7);
        }
    };

    template <>
    inline Vector<s16, 8> load_low<s16, 8>(const s16 *source)
    {
        return simd::s16x8_load_low(source);
    }

    static inline void store_low(s16 *dest, Vector<s16, 8> v)
    {
        simd::s16x8_store_low(dest, v);
    }

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(s16, 8);
    MATH_SIMD_SIGNED_INTEGER_OPERATORS(s16, 8);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(s16, 8, mask16x8);
    MATH_SIMD_SATURATING_INTEGER_FUNCTIONS(s16, 8, mask16x8);
    MATH_SIMD_ABS_INTEGER_FUNCTIONS(s16, 8, mask16x8);

    static inline Vector<s16, 8> hadd(Vector<s16, 8> a, Vector<s16, 8> b)
    {
        return simd::hadd(a, b);
    }

    static inline Vector<s16, 8> hsub(Vector<s16, 8> a, Vector<s16, 8> b)
    {
        return simd::hsub(a, b);
    }

    static inline Vector<s16, 8> hadds(Vector<s16, 8> a, Vector<s16, 8> b)
    {
        return simd::hadds(a, b);
    }

    static inline Vector<s16, 8> hsubs(Vector<s16, 8> a, Vector<s16, 8> b)
    {
        return simd::hsubs(a, b);
    }

    MATH_SIMD_BITWISE_FUNCTIONS(s16, 8);
    MATH_SIMD_COMPARE_FUNCTIONS(s16, 8, mask16x8);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<s16, 8> operator << (Vector<s16, 8> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<s16, 8> operator >> (Vector<s16, 8> a, int b)
    {
        return simd::sra(a, b);
    }

} // namespace mango::math
