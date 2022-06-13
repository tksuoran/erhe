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

    static inline const Vector<s32, 16> operator + (Vector<s32, 16> v)
    {
        return v;
    }

    static inline Vector<s32, 16> operator - (Vector<s32, 16> v)
    {
        return simd::neg(v);
    }

    static inline Vector<s32, 16>& operator += (Vector<s32, 16>& a, Vector<s32, 16> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s32, 16>& operator -= (Vector<s32, 16>& a, Vector<s32, 16> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s32, 16> operator + (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s32, 16> operator - (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s32, 16> unpacklo(Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s32, 16> unpackhi(Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s32, 16> abs(Vector<s32, 16> a)
    {
        return simd::abs(a);
    }

    static inline Vector<s32, 16> add(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s32, 16> add(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask, Vector<s32, 16> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s32, 16> sub(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s32, 16> sub(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask, Vector<s32, 16> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s32, 16> min(Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s32, 16> min(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s32, 16> min(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask, Vector<s32, 16> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s32, 16> max(Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s32, 16> max(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s32, 16> max(Vector<s32, 16> a, Vector<s32, 16> b, mask32x16 mask, Vector<s32, 16> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<s32, 16> clamp(Vector<s32, 16> a, Vector<s32, 16> low, Vector<s32, 16> high)
    {
        return simd::clamp(a, low, high);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s32, 16> nand(Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s32, 16> operator & (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s32, 16> operator | (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s32, 16> operator ^ (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s32, 16> operator ~ (Vector<s32, 16> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask32x16 operator > (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask32x16 operator < (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::compare_gt(b, a);
    }

    static inline mask32x16 operator == (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask32x16 operator >= (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask32x16 operator <= (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::compare_le(b, a);
    }

    static inline mask32x16 operator != (Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s32, 16> select(mask32x16 mask, Vector<s32, 16> a, Vector<s32, 16> b)
    {
        return simd::select(mask, a, b);
    }

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
