/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<u16, 32>
    {
        using VectorType = simd::u16x32;
        using ScalarType = u16;
        enum { VectorSize = 32 };

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

        Vector(u16 s)
            : m(simd::u16x32_set(s))
        {
        }

        Vector(
            u16 s00, u16 s01, u16 s02, u16 s03, u16 s04, u16 s05, u16 s06, u16 s07, 
            u16 s08, u16 s09, u16 s10, u16 s11, u16 s12, u16 s13, u16 s14, u16 s15,
            u16 s16, u16 s17, u16 s18, u16 s19, u16 s20, u16 s21, u16 s22, u16 s23, 
            u16 s24, u16 s25, u16 s26, u16 s27, u16 s28, u16 s29, u16 s30, u16 s31)
            : m(simd::u16x32_set(
                s00, s01, s02, s03, s04, s05, s06, s07, s08, s09, s10, s11, s12, s13, s14, s15, 
                s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30, s31))
        {
        }

        Vector(simd::u16x32 v)
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

        Vector& operator = (simd::u16x32 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (u16 s)
        {
            m = simd::u16x32_set(s);
            return *this;
        }

        operator simd::u16x32 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::u16x32::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
        }
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(u16, 32);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(u16, 32, mask16x32);
    MATH_SIMD_SATURATING_INTEGER_FUNCTIONS(u16, 32, mask16x32);

    MATH_SIMD_BITWISE_FUNCTIONS(u16, 32);
    MATH_SIMD_COMPARE_FUNCTIONS(u16, 32, mask16x32);

    // ------------------------------------------------------------------
    // shift
    // ------------------------------------------------------------------

    static inline Vector<u16, 32> operator << (Vector<u16, 32> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u16, 32> operator >> (Vector<u16, 32> a, int b)
    {
        return simd::srl(a, b);
    }

} // namespace mango::math
