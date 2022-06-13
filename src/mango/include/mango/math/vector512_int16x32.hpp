/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s16, 32>
    {
        using VectorType = simd::s16x32;
        using ScalarType = s16;
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

        Vector(s16 s)
            : m(simd::s16x32_set(s))
        {
        }

        Vector(
            s16 v00, s16 v01, s16 v02, s16 v03, s16 v04, s16 v05, s16 v06, s16 v07, 
            s16 v08, s16 v09, s16 v10, s16 v11, s16 v12, s16 v13, s16 v14, s16 v15,
            s16 v16, s16 v17, s16 v18, s16 v19, s16 v20, s16 v21, s16 v22, s16 v23, 
            s16 v24, s16 v25, s16 v26, s16 v27, s16 v28, s16 v29, s16 v30, s16 v31)
            : m(simd::s16x32_set(
                v00, v01, v02, v03, v04, v05, v06, v07, v08, v09, v10, v11, v12, v13, v14, v15,
                v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31))
        {
        }

        Vector(simd::s16x32 v)
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

        Vector& operator = (simd::s16x32 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s16 s)
        {
            m = simd::s16x32_set(s);
            return *this;
        }

        operator simd::s16x32 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::s16x32::vector () const
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

    static inline const Vector<s16, 32> operator + (Vector<s16, 32> v)
    {
        return v;
    }

    static inline Vector<s16, 32> operator - (Vector<s16, 32> v)
    {
        return simd::neg(v);
    }

    static inline Vector<s16, 32>& operator += (Vector<s16, 32>& a, Vector<s16, 32> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s16, 32>& operator -= (Vector<s16, 32>& a, Vector<s16, 32> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s16, 32> operator + (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s16, 32> operator - (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s16, 32> unpacklo(Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s16, 32> unpackhi(Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s16, 32> abs(Vector<s16, 32> a)
    {
        return simd::abs(a);
    }

    static inline Vector<s16, 32> abs(Vector<s16, 32> a, mask16x32 mask)
    {
        return simd::abs(a, mask);
    }

    static inline Vector<s16, 32> abs(Vector<s16, 32> a, mask16x32 mask, Vector<s16, 32> value)
    {
        return simd::abs(a, mask, value);
    }

    static inline Vector<s16, 32> add(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s16, 32> add(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask, Vector<s16, 32> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s16, 32> sub(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s16, 32> sub(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask, Vector<s16, 32> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s16, 32> adds(Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::adds(a, b);
    }

    static inline Vector<s16, 32> adds(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask)
    {
        return simd::adds(a, b, mask);
    }

    static inline Vector<s16, 32> adds(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask, Vector<s16, 32> value)
    {
        return simd::adds(a, b, mask, value);
    }

    static inline Vector<s16, 32> subs(Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::subs(a, b);
    }

    static inline Vector<s16, 32> subs(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask)
    {
        return simd::subs(a, b, mask);
    }

    static inline Vector<s16, 32> subs(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask, Vector<s16, 32> value)
    {
        return simd::subs(a, b, mask, value);
    }

    static inline Vector<s16, 32> min(Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s16, 32> min(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s16, 32> min(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask, Vector<s16, 32> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s16, 32> max(Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s16, 32> max(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s16, 32> max(Vector<s16, 32> a, Vector<s16, 32> b, mask16x32 mask, Vector<s16, 32> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<s16, 32> clamp(Vector<s16, 32> a, Vector<s16, 32> low, Vector<s16, 32> high)
    {
        return simd::clamp(a, low, high);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s16, 32> nand(Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s16, 32> operator & (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s16, 32> operator | (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s16, 32> operator ^ (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s16, 32> operator ~ (Vector<s16, 32> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask16x32 operator > (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask16x32 operator < (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::compare_gt(b, a);
    }

    static inline mask16x32 operator == (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask16x32 operator >= (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask16x32 operator <= (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::compare_le(b, a);
    }

    static inline mask16x32 operator != (Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s16, 32> select(mask16x32 mask, Vector<s16, 32> a, Vector<s16, 32> b)
    {
        return simd::select(mask, a, b);
    }

    // ------------------------------------------------------------------
	// shift
    // ------------------------------------------------------------------

    static inline Vector<s16, 32> operator << (Vector<s16, 32> a, int b)
    {
        return simd::sll(a, b);
    }

    static inline Vector<s16, 32> operator >> (Vector<s16, 32> a, int b)
    {
        return simd::sra(a, b);
    }

} // namespace mango::math
