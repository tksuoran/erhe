/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s8, 16>
    {
        using VectorType = simd::s8x16;
        using ScalarType = s8;
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

        Vector(s8 s)
            : m(simd::s8x16_set(s))
        {
        }

        Vector(s8 v0, s8 v1, s8 v2, s8 v3, s8 v4, s8 v5, s8 v6, s8 v7, s8 v8, s8 v9, s8 v10, s8 v11, s8 v12, s8 v13, s8 v14, s8 v15)
            : m(simd::s8x16_set(v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14, v15))
        {
        }

        Vector(simd::s8x16 v)
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

        Vector& operator = (simd::s8x16 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s8 s)
        {
            m = simd::s8x16_set(s);
            return *this;
        }

        operator simd::s8x16 () const
        {
            return m;
        }

#ifdef simd_int128_is_hardware_vector
        operator simd::s8x16::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
        }
    };

    template <>
    inline Vector<s8, 16> load_low<s8, 16>(const s8 *source)
    {
        return simd::s8x16_load_low(source);
    }

    static inline void store_low(s8 *dest, Vector<s8, 16> v)
    {
        simd::s8x16_store_low(dest, v);
    }

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    MATH_SIMD_UNSIGNED_INTEGER_OPERATORS(s8, 16);
    MATH_SIMD_SIGNED_INTEGER_OPERATORS(s8, 16);

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    MATH_SIMD_INTEGER_FUNCTIONS(s8, 16, mask8x16);
    MATH_SIMD_SATURATING_INTEGER_FUNCTIONS(s8, 16, mask8x16);
    MATH_SIMD_ABS_INTEGER_FUNCTIONS(s8, 16, mask8x16);

    MATH_SIMD_BITWISE_FUNCTIONS(s8, 16);
    MATH_SIMD_COMPARE_FUNCTIONS(s8, 16, mask8x16);

} // namespace mango::math
