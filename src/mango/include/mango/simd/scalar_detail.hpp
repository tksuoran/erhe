/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/simd/simd.hpp>

namespace mango::simd::detail
{

    template <typename ScalarType, int Size>
    static inline scalar_vector<ScalarType, Size>
    scalar_set(ScalarType value)
    {
        scalar_vector<ScalarType, Size> v;
        for (int i = 0; i < Size; ++i)
        {
            v[i] = value;
        }
        return v;
    }

    // unary

    template <typename ScalarType, int Size>
    static inline scalar_vector<ScalarType, Size>
    scalar_unroll(ScalarType (*func)(ScalarType), scalar_vector<ScalarType, Size> a)
    {
        scalar_vector<ScalarType, Size> v;
        for (int i = 0; i < Size; ++i)
        {
            v[i] = func(a[i]);
        }
        return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_abs(ScalarType a)
    {
        return std::abs(a);
    }

    template <typename ScalarType>
    static inline ScalarType scalar_neg(ScalarType a)
    {
        return -a;
    }

    // binary

    template <typename ScalarType, int Size>
    static inline scalar_vector<ScalarType, Size>
    scalar_unroll(ScalarType (*func)(ScalarType, ScalarType), scalar_vector<ScalarType, Size> a, scalar_vector<ScalarType, Size> b)
    {
        scalar_vector<ScalarType, Size> v;
        for (int i = 0; i < Size; ++i)
        {
            v[i] = func(a[i], b[i]);
        }
        return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_add(ScalarType a, ScalarType b)
    {
        return a + b;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_sub(ScalarType a, ScalarType b)
    {
        return a - b;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_unsigned_adds(ScalarType a, ScalarType b)
    {
	    ScalarType v = a + b;
	    v |= -(v < a);
	    return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_unsigned_subs(ScalarType a, ScalarType b)
    {
    	ScalarType v = a - b;
	    v &= -(v <= a);
	    return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_signed_adds(ScalarType a, ScalarType b)
    {
        using UnsignedScalarType = typename std::make_unsigned<ScalarType>::type;
        UnsignedScalarType x = a;
        UnsignedScalarType y = b;
        UnsignedScalarType v = x + y;

  	    // overflow
	    x = (x >> (sizeof(ScalarType) * 8 - 1)) + std::numeric_limits<ScalarType>::max();
	    if (ScalarType((x ^ y) | ~(y ^ v)) >= 0)
	    {
		    v = x;
	    }

	    return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_signed_subs(ScalarType a, ScalarType b)
    {
        using UnsignedScalarType = typename std::make_unsigned<ScalarType>::type;
        UnsignedScalarType x = a;
        UnsignedScalarType y = b;
        UnsignedScalarType v = x - y;

  	    // overflow
	    x = (x >> (sizeof(ScalarType) * 8 - 1)) + std::numeric_limits<ScalarType>::max();
	    if (ScalarType((x ^ y) & (x ^ v)) < 0)
	    {
		    v = x;
	    }

	    return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_unsigned_average(ScalarType a, ScalarType b)
    {
        ScalarType v = (a & b) + ((a ^ b) >> 1);
        return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_unsigned_rounded_average(ScalarType a, ScalarType b)
    {
        ScalarType v = (a & b) + ((a ^ b) >> 1) + ((a ^ b) & 1);
        return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_signed_average(ScalarType a, ScalarType b)
    {
        using UnsignedType = typename std::make_unsigned<ScalarType>::type;

        constexpr UnsignedType sign = UnsignedType(1) << (sizeof(UnsignedType) * 8 - 1);
        return scalar_unsigned_average<UnsignedType>(a ^ sign, b ^ sign) ^ sign;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_signed_rounded_average(ScalarType a, ScalarType b)
    {
        using UnsignedType = typename std::make_unsigned<ScalarType>::type;

        constexpr UnsignedType sign = UnsignedType(1) << (sizeof(UnsignedType) * 8 - 1);
        return scalar_unsigned_rounded_average<UnsignedType>(a ^ sign, b ^ sign) ^ sign;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_mullo(ScalarType a, ScalarType b)
    {
        return ScalarType(a * b);
    }

    template <typename ScalarType>
    static inline ScalarType scalar_nand(ScalarType a, ScalarType b)
    {
        return ~a & b;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_and(ScalarType a, ScalarType b)
    {
        return a & b;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_or(ScalarType a, ScalarType b)
    {
        return a | b;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_xor(ScalarType a, ScalarType b)
    {
        return a ^ b;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_not(ScalarType a)
    {
        return ~a;
    }

    // shift by constant

    template <typename VectorType, typename ScalarType, int Count>
    static inline VectorType scalar_shift_left(VectorType a)
    {
        VectorType v;
        for (int i = 0; i < VectorType::size; ++i)
        {
            v[i] = Count < int(sizeof(ScalarType) * 8) ? ScalarType(a[i]) << Count : 0;
        }
        return v;
    }

    template <typename VectorType, typename ScalarType, int Count>
    static inline VectorType scalar_shift_right(VectorType a)
    {
        VectorType v;
        for (int i = 0; i < VectorType::size; ++i)
        {
            v[i] = Count < int(sizeof(ScalarType) * 8) ? ScalarType(a[i]) >> Count : 0;
        }
        return v;
    }

    // shift by scalar

    template <typename VectorType, typename ScalarType>
    static inline VectorType scalar_shift_left(VectorType a, int count)
    {
        VectorType v;
        for (int i = 0; i < VectorType::size; ++i)
        {
            v[i] = count < int(sizeof(ScalarType) * 8) ? ScalarType(a[i]) << count : 0;
        }
        return v;
    }

    template <typename VectorType, typename ScalarType>
    static inline VectorType scalar_shift_right(VectorType a, int count)
    {
        VectorType v;
        for (int i = 0; i < VectorType::size; ++i)
        {
            v[i] = count < int(sizeof(ScalarType) * 8) ? ScalarType(a[i]) >> count : 0;
        }
        return v;
    }

    // shift by vector

    template <typename VectorType, typename ScalarType, typename CountVectorType>
    static inline VectorType scalar_shift_left(VectorType a, CountVectorType count)
    {
        VectorType v;
        for (int i = 0; i < VectorType::size; ++i)
        {
            v[i] = count[i] < sizeof(ScalarType) * 8 ? ScalarType(a[i]) << count[i] : 0;
        }
        return v;
    }

    template <typename VectorType, typename ScalarType, typename CountVectorType>
    static inline VectorType scalar_shift_right(VectorType a, CountVectorType count)
    {
        VectorType v;
        for (int i = 0; i < VectorType::size; ++i)
        {
            v[i] = count[i] < sizeof(ScalarType) * 8 ? ScalarType(a[i]) >> count[i] : 0;
        }
        return v;
    }

    template <typename ScalarType>
    static inline ScalarType scalar_min(ScalarType a, ScalarType b)
    {
        return std::min(a, b);
    }

    template <typename ScalarType>
    static inline ScalarType scalar_max(ScalarType a, ScalarType b)
    {
        return std::max(a, b);
    }

    // cmp

    template <typename VectorType>
    static inline u32 scalar_compare_eq(VectorType a, VectorType b)
    {
        u32 mask = 0;
        for (int i = 0; i < VectorType::size; ++i)
        {
            mask |= u32(a[i] == b[i]) << i;
        }
        return mask;
    }

    template <typename VectorType>
    static inline u32 scalar_compare_gt(VectorType a, VectorType b)
    {
        u32 mask = 0;
        for (int i = 0; i < VectorType::size; ++i)
        {
            mask |= u32(a[i] > b[i]) << i;
        }
        return mask;
    }

    template <typename VectorType>
    static inline u32 scalar_compare_neq(VectorType a, VectorType b)
    {
        u32 mask = 0;
        for (int i = 0; i < VectorType::size; ++i)
        {
            mask |= u32(a[i] != b[i]) << i;
        }
        return mask;
    }

    template <typename VectorType>
    static inline u32 scalar_compare_lt(VectorType a, VectorType b)
    {
        u32 mask = 0;
        for (int i = 0; i < VectorType::size; ++i)
        {
            mask |= u32(a[i] < b[i]) << i;
        }
        return mask;
    }

    template <typename VectorType>
    static inline u32 scalar_compare_le(VectorType a, VectorType b)
    {
        u32 mask = 0;
        for (int i = 0; i < VectorType::size; ++i)
        {
            mask |= u32(a[i] <= b[i]) << i;
        }
        return mask;
    }

    template <typename VectorType>
    static inline u32 scalar_compare_ge(VectorType a, VectorType b)
    {
        u32 mask = 0;
        for (int i = 0; i < VectorType::size; ++i)
        {
            mask |= u32(a[i] >= b[i]) << i;
        }
        return mask;
    }

    // misc

    template <typename ScalarType, int Size>
    static inline scalar_vector<ScalarType, Size>
    scalar_select(u32 mask, scalar_vector<ScalarType, Size> a, scalar_vector<ScalarType, Size> b)
    {
        scalar_vector<ScalarType, Size> v;
        for (int i = 0; i < Size; ++i)
        {
            v[i] = mask & (1 << i) ? a[i] : b[i];
        }
        return v;
    }

    template <typename VectorType>
    static inline VectorType scalar_unpacklo(VectorType a, VectorType b)
    {
        VectorType v;
        for (int i = 0; i < VectorType::size / 2; ++i)
        {
            v[i * 2 + 0] = a[i];
            v[i * 2 + 1] = b[i];
        }
        return v;
    }

    template <typename VectorType>
    static inline VectorType scalar_unpackhi(VectorType a, VectorType b)
    {
        VectorType v;
        constexpr int half = VectorType::size / 2;
        for (int i = 0; i < half; ++i)
        {
            v[i * 2 + 0] = a[i + half];
            v[i * 2 + 1] = b[i + half];
        }
        return v;
    }

} // namespace mango::simd::detail
