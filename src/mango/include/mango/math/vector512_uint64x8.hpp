/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<u64, 8>
    {
        using VectorType = simd::u64x8;
        using ScalarType = u64;
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

        Vector(u64 s)
            : m(simd::u64x8_set(s))
        {
        }

        explicit Vector(u64 s0, u64 s1, u64 s2, u64 s3, u64 s4, u64 s5, u64 s6, u64 s7)
            : m(simd::u64x8_set(s0, s1, s2, s3, s4, s5, s6, s7))
        {
        }

        Vector(simd::u64x8 v)
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

        Vector& operator = (simd::u64x8 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (u64 s)
        {
            m = simd::u64x8_set(s);
            return *this;
        }

        operator simd::u64x8 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::u64x8::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7);
        }
    };

    static inline const Vector<u64, 8> operator + (Vector<u64, 8> v)
    {
        return v;
    }

    static inline Vector<u64, 8> operator - (Vector<u64, 8> v)
    {
        return simd::sub(simd::u64x8_zero(), v);
    }

    static inline Vector<u64, 8>& operator += (Vector<u64, 8>& a, Vector<u64, 8> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<u64, 8>& operator -= (Vector<u64, 8>& a, Vector<u64, 8> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<u64, 8> operator + (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<u64, 8> operator - (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<u64, 8> unpacklo(Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<u64, 8> unpackhi(Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<u64, 8> add(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<u64, 8> add(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask, Vector<u64, 8> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<u64, 8> sub(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<u64, 8> sub(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask, Vector<u64, 8> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<u64, 8> min(Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<u64, 8> min(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<u64, 8> min(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask, Vector<u64, 8> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<u64, 8> max(Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<u64, 8> max(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<u64, 8> max(Vector<u64, 8> a, Vector<u64, 8> b, mask64x8 mask, Vector<u64, 8> value)
    {
        return simd::max(a, b, mask, value);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<u64, 8> nand(Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<u64, 8> operator & (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<u64, 8> operator | (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<u64, 8> operator ^ (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<u64, 8> operator ~ (Vector<u64, 8> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask64x8 operator > (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask64x8 operator >= (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask64x8 operator < (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::compare_lt(a, b);
    }

    static inline mask64x8 operator <= (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::compare_le(a, b);
    }

    static inline mask64x8 operator == (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask64x8 operator != (Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<u64, 8> select(mask64x8 mask, Vector<u64, 8> a, Vector<u64, 8> b)
    {
        return simd::select(mask, a, b);
    }

    // ------------------------------------------------------------------
	// shift
    // ------------------------------------------------------------------

    static inline Vector<u64, 8> operator << (Vector<u64, 8> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<u64, 8> operator >> (Vector<u64, 8> a, int b)
    {
        return simd::srl(a, b);
    }

} // namespace mango::math
