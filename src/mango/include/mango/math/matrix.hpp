/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <cassert>
#include <mango/math/vector.hpp>

namespace mango::math
{

    // ------------------------------------------------------------------
    // MatrixBase
    // ------------------------------------------------------------------

    template <typename ScalarType, int Width, int Height>
    struct MatrixBase
    {
        using VectorType = Vector<ScalarType, Width>;

        operator const float* () const
        {
            return reinterpret_cast<const float*>(this);
        }

        operator float* ()
        {
            return reinterpret_cast<float*>(this);
        }

        const VectorType& operator [] (int y) const
        {
            assert(y >= 0 && y < Height);
            return reinterpret_cast<const VectorType *>(this)[y];
        }

        VectorType& operator [] (int y)
        {
            assert(y >= 0 && y < Height);
            return reinterpret_cast<VectorType *>(this)[y];
        }

        float operator () (int y, int x) const
        {
            assert(x >= 0 && x < Width);
            assert(y >= 0 && y < Height);
            return reinterpret_cast<const float *>(this)[y * Width + x];
        }

        float& operator () (int y, int x)
        {
            assert(x >= 0 && x < Width);
            assert(y >= 0 && y < Height);
            return reinterpret_cast<float *>(this)[y * Width + x];
        }
    };

    // ------------------------------------------------------------------
    // Matrix
    // ------------------------------------------------------------------

    template <typename ScalarType, int Width, int Height>
    struct Matrix : MatrixBase<ScalarType, Width, Height>
    {
        using VectorType = Vector<ScalarType, Width>;

        VectorType m[Height];

        explicit Matrix()
        {
        }

        ~Matrix()
        {
        }
    };

    // ------------------------------------------------------------------
    // 2x2
    // ------------------------------------------------------------------

    template <typename ScalarType>
    struct Matrix<ScalarType, 2, 2> : MatrixBase<ScalarType, 2, 2>
    {
        Vector<ScalarType, 2> m[2];

        explicit Matrix()
        {
        }

        explicit Matrix(Vector<ScalarType, 2> v0, Vector<ScalarType, 2> v1)
        {
            m[0] = v0;
            m[1] = v1;
        }

        explicit Matrix(ScalarType s0, ScalarType s1, ScalarType s2, ScalarType s3)
        {
            m[0] = Vector<ScalarType, 2>(s0, s1);
            m[1] = Vector<ScalarType, 2>(s2, s3);
        }

        ~Matrix()
        {
        }

        operator Vector<ScalarType, 2>* ()
        {
            return m;
        }

        operator const Vector<ScalarType, 2>* () const
        {
            return m;
        }
    };

    template <typename ScalarType>
    static inline Matrix<ScalarType, 2, 2> operator * (const Matrix<ScalarType, 2, 2>& m, ScalarType s)
    {
        return Matrix<ScalarType, 2, 2>(
            m[0] * s,
            m[1] * s);
    }

    template <typename ScalarType>
    static inline Vector<ScalarType, 2> operator * (const Vector<ScalarType, 2>& v, const Matrix<ScalarType, 2, 2>& m)
    {
        return Vector<ScalarType, 2>(
            v[0] * m(0, 0) + v[1] * m(1, 0),
            v[0] * m(0, 1) + v[1] * m(1, 1));
    }

    // ------------------------------------------------------------------
    // 3x3
    // ------------------------------------------------------------------

    template <typename ScalarType>
    struct Matrix<ScalarType, 3, 3> : MatrixBase<ScalarType, 3, 3>
    {
        Vector<ScalarType, 3> m[3];

        explicit Matrix()
        {
        }

        explicit Matrix(Vector<ScalarType, 3> v0, Vector<ScalarType, 3> v1, Vector<ScalarType, 3> v2)
        {
            m[0] = v0;
            m[1] = v1;
            m[2] = v2;
        }

        explicit Matrix(ScalarType s0, ScalarType s1, ScalarType s2, ScalarType s3, ScalarType s4, ScalarType s5, ScalarType s6, ScalarType s7, ScalarType s8)
        {
            m[0] = Vector<ScalarType, 3>(s0, s1, s2);
            m[1] = Vector<ScalarType, 3>(s3, s4, s5);
            m[2] = Vector<ScalarType, 3>(s6, s7, s8);
        }

        ~Matrix()
        {
        }

        operator Vector<ScalarType, 3>* ()
        {
            return m;
        }

        operator const Vector<ScalarType, 3>* () const
        {
            return m;
        }
    };

    template <typename ScalarType>
    static inline Matrix<ScalarType, 3, 3> operator * (const Matrix<ScalarType, 3, 3>& m, ScalarType s)
    {
        return Matrix<ScalarType, 3, 3>(
            m[0] * s,
            m[1] * s,
            m[2] * s);
    }

    template <typename ScalarType>
    static inline Vector<ScalarType, 3> operator * (const Vector<ScalarType, 3>& v, const Matrix<ScalarType, 3, 3>& m)
    {
        return Vector<ScalarType, 3>(
            v[0] * m(0, 0) + v[1] * m(1, 0) + v[2] * m(2, 0),
            v[0] * m(0, 1) + v[1] * m(1, 1) + v[2] * m(2, 1),
            v[0] * m(0, 2) + v[1] * m(1, 2) + v[2] * m(2, 2));
    }

    // ------------------------------------------------------------------
    // types
    // ------------------------------------------------------------------

    using Matrix2x2 = Matrix<float, 2, 2>;
    using Matrix3x3 = Matrix<float, 3, 3>;
    using Matrix4x4 = Matrix<float, 4, 4>;

} // namespace mango::math

#include <mango/math/matrix4x4.hpp>
