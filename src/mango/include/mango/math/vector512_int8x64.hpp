/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    template <>
    struct Vector<s8, 64>
    {
        using VectorType = simd::s8x64;
        using ScalarType = s8;
        enum { VectorSize = 64 };

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
            : m(simd::s8x64_set(s))
        {
        }

        Vector(
            s8 v00, s8 v01, s8 v02, s8 v03, s8 v04, s8 v05, s8 v06, s8 v07,
            s8 v08, s8 v09, s8 v10, s8 v11, s8 v12, s8 v13, s8 v14, s8 v15,
            s8 v16, s8 v17, s8 v18, s8 v19, s8 v20, s8 v21, s8 v22, s8 v23,
            s8 v24, s8 v25, s8 v26, s8 v27, s8 v28, s8 v29, s8 v30, s8 v31,
            s8 v32, s8 v33, s8 v34, s8 v35, s8 v36, s8 v37, s8 v38, s8 v39,
            s8 v40, s8 v41, s8 v42, s8 v43, s8 v44, s8 v45, s8 v46, s8 v47,
            s8 v48, s8 v49, s8 v50, s8 v51, s8 v52, s8 v53, s8 v54, s8 v55,
            s8 v56, s8 v57, s8 v58, s8 v59, s8 v60, s8 v61, s8 v62, s8 v63)
            : m(simd::s8x64_set(
                v00, v01, v02, v03, v04, v05, v06, v07, v08, v09, v10, v11, v12, v13, v14, v15,
                v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28, v29, v30, v31,
                v32, v33, v34, v35, v36, v37, v38, v39, v40, v41, v42, v43, v44, v45, v46, v47,
                v48, v49, v50, v51, v52, v53, v54, v55, v56, v57, v58, v59, v60, v61, v62, v63))
        {
        }

        Vector(simd::s8x64 v)
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

        Vector& operator = (simd::s8x64 v)
        {
            m = v;
            return *this;
        }

        Vector& operator = (s8 s)
        {
            m = simd::s8x64_set(s);
            return *this;
        }

        operator simd::s8x64 () const
        {
            return m;
        }

#ifdef simd_int512_is_hardware_vector
        operator simd::s8x64::vector () const
        {
            return m.data;
        }
#endif

        static Vector ascend()
        {
            return Vector(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
                48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63);
        }
    };

    static inline const Vector<s8, 64> operator + (Vector<s8, 64> v)
    {
        return v;
    }

    static inline Vector<s8, 64> operator - (Vector<s8, 64> v)
    {
        return simd::neg(v);
    }

    static inline Vector<s8, 64>& operator += (Vector<s8, 64>& a, Vector<s8, 64> b)
    {
        a = simd::add(a, b);
        return a;
    }

    static inline Vector<s8, 64>& operator -= (Vector<s8, 64>& a, Vector<s8, 64> b)
    {
        a = simd::sub(a, b);
        return a;
    }

    static inline Vector<s8, 64> operator + (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::add(a, b);
    }

    static inline Vector<s8, 64> operator - (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::sub(a, b);
    }

    static inline Vector<s8, 64> unpacklo(Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::unpacklo(a, b);
    }

    static inline Vector<s8, 64> unpackhi(Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::unpackhi(a, b);
    }

    static inline Vector<s8, 64> abs(Vector<s8, 64> a)
    {
        return simd::abs(a);
    }

    static inline Vector<s8, 64> abs(Vector<s8, 64> a, mask8x64 mask)
    {
        return simd::abs(a, mask);
    }

    static inline Vector<s8, 64> abs(Vector<s8, 64> a, mask8x64 mask, Vector<s8, 64> value)
    {
        return simd::abs(a, mask, value);
    }

    static inline Vector<s8, 64> add(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask)
    {
        return simd::add(a, b, mask);
    }

    static inline Vector<s8, 64> add(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask, Vector<s8, 64> value)
    {
        return simd::add(a, b, mask, value);
    }

    static inline Vector<s8, 64> sub(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask)
    {
        return simd::sub(a, b, mask);
    }

    static inline Vector<s8, 64> sub(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask, Vector<s8, 64> value)
    {
        return simd::sub(a, b, mask, value);
    }

    static inline Vector<s8, 64> adds(Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::adds(a, b);
    }

    static inline Vector<s8, 64> adds(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask)
    {
        return simd::adds(a, b, mask);
    }

    static inline Vector<s8, 64> adds(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask, Vector<s8, 64> value)
    {
        return simd::adds(a, b, mask, value);
    }

    static inline Vector<s8, 64> subs(Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::subs(a, b);
    }

    static inline Vector<s8, 64> subs(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask)
    {
        return simd::subs(a, b, mask);
    }

    static inline Vector<s8, 64> subs(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask, Vector<s8, 64> value)
    {
        return simd::subs(a, b, mask, value);
    }

    static inline Vector<s8, 64> min(Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::min(a, b);
    }

    static inline Vector<s8, 64> min(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask)
    {
        return simd::min(a, b, mask);
    }

    static inline Vector<s8, 64> min(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask, Vector<s8, 64> value)
    {
        return simd::min(a, b, mask, value);
    }

    static inline Vector<s8, 64> max(Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::max(a, b);
    }

    static inline Vector<s8, 64> max(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask)
    {
        return simd::max(a, b, mask);
    }

    static inline Vector<s8, 64> max(Vector<s8, 64> a, Vector<s8, 64> b, mask8x64 mask, Vector<s8, 64> value)
    {
        return simd::max(a, b, mask, value);
    }

    static inline Vector<s8, 64> clamp(Vector<s8, 64> a, Vector<s8, 64> low, Vector<s8, 64> high)
    {
        return simd::clamp(a, low, high);
    }

    // ------------------------------------------------------------------
	// bitwise operators
    // ------------------------------------------------------------------

    static inline Vector<s8, 64> nand(Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::bitwise_nand(a, b);
    }

    static inline Vector<s8, 64> operator & (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::bitwise_and(a, b);
    }

    static inline Vector<s8, 64> operator | (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::bitwise_or(a, b);
    }

    static inline Vector<s8, 64> operator ^ (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::bitwise_xor(a, b);
    }

    static inline Vector<s8, 64> operator ~ (Vector<s8, 64> a)
    {
        return simd::bitwise_not(a);
    }

    // ------------------------------------------------------------------
	// compare / select
    // ------------------------------------------------------------------

    static inline mask8x64 operator > (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::compare_gt(a, b);
    }

    static inline mask8x64 operator < (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::compare_gt(b, a);
    }

    static inline mask8x64 operator == (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::compare_eq(a, b);
    }

    static inline mask8x64 operator >= (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::compare_ge(a, b);
    }

    static inline mask8x64 operator <= (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::compare_le(b, a);
    }

    static inline mask8x64 operator != (Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::compare_neq(a, b);
    }

    static inline Vector<s8, 64> select(mask8x64 mask, Vector<s8, 64> a, Vector<s8, 64> b)
    {
        return simd::select(mask, a, b);
    }

} // namespace mango::math
