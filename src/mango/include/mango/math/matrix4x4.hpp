/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/matrix.hpp>

namespace mango::math
{

    struct Quaternion;
    struct AngleAxis;

    // ------------------------------------------------------------------
    // Matrix<float, 4, 4>
    // ------------------------------------------------------------------

    /*
    offsets: [memory layout]

    [ 0  1  2  3]  <- simd float32x4 #0
    [ 4  5  6  7]  <- simd float32x4 #1
    [ 8  9 10 11]  <- simd float32x4 #2
    [12 13 14 15]  <- simd float32x4 #3

    indices: [operator (y,x)]

    [(0,0) (0,1) (0,2) (0,3)]
    [(1,0) (1,1) (1,2) (1,3)]
    [(2,0) (2,1) (2,2) (2,3)]
    [(3,0) (3,1) (3,2) (3,3)]

    scaling: [sx, sy, sz]

    [sx -- -- --]
    [-- sy -- --]
    [-- -- sz --]
    [-- -- -- --]

    translation: [tx, ty, tz]

    [-- -- -- --]
    [-- -- -- --]
    [-- -- -- --]
    [tx ty tz --]

    rotation: [axis vectors]

    [xx xy xz --]  <- x-axis
    [yx yy yz --]  <- y-axis
    [zx zy zz --]  <- z-axis
    [-- -- -- --]
    */

    template <>
    struct Matrix<float, 4, 4> : MatrixBase<float, 4, 4>
    {
        float32x4 m[4];

        explicit Matrix()
        {
        }

        explicit Matrix(float scale)
        {
            *this = scale;
        }

        explicit Matrix(const float* v)
        {
            *this = v;
        }

        Matrix(const Matrix& v)
        {
            m[0] = v.m[0];
            m[1] = v.m[1];
            m[2] = v.m[2];
            m[3] = v.m[3];
        }
        
        explicit Matrix(
            const Vector<float, 4>& v0,
            const Vector<float, 4>& v1,
            const Vector<float, 4>& v2,
            const Vector<float, 4>& v3)
        {
            m[0] = v0;
            m[1] = v1;
            m[2] = v2;
            m[3] = v3;
        }

        Matrix(const Quaternion& rotation)
        {
            *this = rotation;
        }

        Matrix(const AngleAxis& rotation)
        {
            *this = rotation;
        }

        ~Matrix()
        {
        }

        const Matrix4x4& operator = (const Matrix4x4& v)
        {
            m[0] = v.m[0];
            m[1] = v.m[1];
            m[2] = v.m[2];
            m[3] = v.m[3];
            return *this;
        }

        const Matrix4x4& operator = (float scale);
        const Matrix4x4& operator = (const float* ptr);
        const Matrix4x4& operator = (const Quaternion& rotation);
        const Matrix4x4& operator = (const AngleAxis& rotation);

        operator float32x4* ()
        {
            return m;
        }

        operator const float32x4* () const
        {
            return m;
        }

        template <u32 index>
        float32x4 column() const
        {
            static_assert(index <= 3, "Index out of range.");
            float32x4 temp0 = index & 2 ? unpackhi(m[0], m[1]) : unpacklo(m[0], m[1]);
            float32x4 temp1 = index & 2 ? unpackhi(m[2], m[3]) : unpacklo(m[2], m[3]);
            return index & 1 ? movehl(temp1, temp0) : movelh(temp0, temp1);
        }

        bool isAffine() const;
        float determinant2x2() const;
        float determinant3x3() const;
        float determinant4x4() const;

        static Matrix4x4 identity();
        static Matrix4x4 translate(float x, float y, float z);
        static Matrix4x4 scale(float s);
        static Matrix4x4 scale(float x, float y, float z);
        static Matrix4x4 rotate(float angle, const float32x3& axis);
        static Matrix4x4 rotateX(float angle);
        static Matrix4x4 rotateY(float angle);
        static Matrix4x4 rotateZ(float angle);
        static Matrix4x4 rotateXYZ(float x, float y, float z);
        static Matrix4x4 lookat(const float32x3& target, const float32x3& viewer, const float32x3& up);

        static Matrix4x4 orthoGL(float left, float right, float bottom, float top, float znear, float zfar);
        static Matrix4x4 frustumGL(float left, float right, float bottom, float top, float znear, float zfar);
        static Matrix4x4 perspectiveGL(float xfov, float yfov, float znear, float zfar);

        static Matrix4x4 orthoVK(float left, float right, float bottom, float top, float znear, float zfar);
        static Matrix4x4 frustumVK(float left, float right, float bottom, float top, float znear, float zfar);
        static Matrix4x4 perspectiveVK(float xfov, float yfov, float znear, float zfar);

        static Matrix4x4 orthoD3D(float left, float right, float bottom, float top, float znear, float zfar);
        static Matrix4x4 frustumD3D(float left, float right, float bottom, float top, float znear, float zfar);
        static Matrix4x4 perspectiveD3D(float xfov, float yfov, float znear, float zfar);
    };

    // ------------------------------------------------------------------
    // operators
    // ------------------------------------------------------------------

    static inline Vector<float, 3> operator * (const Vector<float, 3>& v, const Matrix4x4& m)
    {
        float x = v[0] * m(0, 0) + v[1] * m(1, 0) + v[2] * m(2, 0) + m(3, 0);
        float y = v[0] * m(0, 1) + v[1] * m(1, 1) + v[2] * m(2, 1) + m(3, 1);
        float z = v[0] * m(0, 2) + v[1] * m(1, 2) + v[2] * m(2, 2) + m(3, 2);
        return Vector<float, 3>(x, y, z);
    }

    static inline Vector<float, 4> operator * (const Vector<float, 4>& v, const Matrix4x4& m)
    {
        float32x4 temp = m[0] * v.xxxx;
        temp = madd(temp, m[1], v.yyyy);
        temp = madd(temp, m[2], v.zzzz);
        temp = madd(temp, m[3], v.wwww);
        return temp;
    }

    static inline Matrix4x4 operator * (const Matrix4x4& a, const Matrix4x4& b)
    {
        Matrix4x4 result;
        result[0] = a[0] * b;
        result[1] = a[1] * b;
        result[2] = a[2] * b;
        result[3] = a[3] * b;
        return result;
    }

    // ------------------------------------------------------------------
    // functions
    // ------------------------------------------------------------------

    Matrix4x4 translate(const Matrix4x4& matrix, float x, float y, float z);
    Matrix4x4 scale(const Matrix4x4& matrix, float s);
    Matrix4x4 scale(const Matrix4x4& matrix, float x, float y, float z);
    Matrix4x4 rotate(const Matrix4x4& matrix, float angle, const float32x3& axis);
    Matrix4x4 rotateX(const Matrix4x4& matrix, float angle);
    Matrix4x4 rotateY(const Matrix4x4& matrix, float angle);
    Matrix4x4 rotateZ(const Matrix4x4& matrix, float angle);
    Matrix4x4 rotateXYZ(const Matrix4x4& matrix, float x, float y, float z);
    Matrix4x4 normalize(const Matrix4x4& matrix);
    Matrix4x4 mirror(const Matrix4x4& matrix, const float32x4& plane);
    Matrix4x4 affineInverse(const Matrix4x4& matrix);
    Matrix4x4 adjoint(const Matrix4x4& matrix);

    Matrix4x4 obliqueGL(const Matrix4x4& matrix, const float32x4& nearclip);
    Matrix4x4 obliqueVK(const Matrix4x4& matrix, const float32x4& nearclip);
    Matrix4x4 obliqueD3D(const Matrix4x4& matrix, const float32x4& nearclip);

    static inline Matrix4x4 transpose(float32x4 m0, float32x4 m1, float32x4 m2, float32x4 m3)
    {
        float32x4 temp0 = unpacklo(m0, m1);
        float32x4 temp1 = unpacklo(m2, m3);
        float32x4 temp2 = unpackhi(m0, m1);
        float32x4 temp3 = unpackhi(m2, m3);
        Matrix4x4 result;
        result[0] = movelh(temp0, temp1);
        result[1] = movehl(temp1, temp0);
        result[2] = movelh(temp2, temp3);
        result[3] = movehl(temp3, temp2);
        return result;
    }

    static inline Matrix4x4 inverse(float32x4 m0, float32x4 m1, float32x4 m2, float32x4 m3)
    {
        // Original Intel SSE code (C) Rune Stubbe. All rights reserved.
        // Modified to use our SIMD abstraction so that compiles for more architectures.
        // ~15% faster than our inverseTranspose() + transpose() method we used here earlier.
        float32x4 r0y_r1y_r0x_r1x = movelh(m1, m0);
        float32x4 r0z_r1z_r0w_r1w = movelh(m2, m3);
        float32x4 r2y_r3y_r2x_r3x = movehl(m0, m1);
        float32x4 r2z_r3z_r2w_r3w = movehl(m3, m2);

        float32x4 r1y_r2y_r1x_r2x = shuffle<1, 2, 1, 2>(m1, m0);
        float32x4 r1z_r2z_r1w_r2w = shuffle<1, 2, 1, 2>(m2, m3);
        float32x4 r3y_r0y_r3x_r0x = shuffle<3, 0, 3, 0>(m1, m0);
        float32x4 r3z_r0z_r3w_r0w = shuffle<3, 0, 3, 0>(m2, m3);

        float32x4 r0_wzyx = shuffle<2, 0, 0, 2>(r0z_r1z_r0w_r1w, r0y_r1y_r0x_r1x);
        float32x4 r1_wzyx = shuffle<3, 1, 1, 3>(r0z_r1z_r0w_r1w, r0y_r1y_r0x_r1x);
        float32x4 r2_wzyx = shuffle<2, 0, 0, 2>(r2z_r3z_r2w_r3w, r2y_r3y_r2x_r3x);
        float32x4 r3_wzyx = shuffle<3, 1, 1, 3>(r2z_r3z_r2w_r3w, r2y_r3y_r2x_r3x);
        float32x4 r0_xyzw = shuffle<2, 0, 0, 2>(r0y_r1y_r0x_r1x, r0z_r1z_r0w_r1w);

        float32x4 inner12_23 = r1y_r2y_r1x_r2x * r2z_r3z_r2w_r3w - r1z_r2z_r1w_r2w * r2y_r3y_r2x_r3x;
        float32x4 inner02_13 = r0y_r1y_r0x_r1x * r2z_r3z_r2w_r3w - r0z_r1z_r0w_r1w * r2y_r3y_r2x_r3x;
        float32x4 inner30_01 = r3z_r0z_r3w_r0w * r0y_r1y_r0x_r1x - r3y_r0y_r3x_r0x * r0z_r1z_r0w_r1w;

        float32x4 inner12 = shuffle<0, 2, 2, 0>(inner12_23, inner12_23);
        float32x4 inner23 = shuffle<1, 3, 3, 1>(inner12_23, inner12_23);

        float32x4 inner02 = shuffle<0, 2, 2, 0>(inner02_13, inner02_13);
        float32x4 inner13 = shuffle<1, 3, 3, 1>(inner02_13, inner02_13);

        float32x4 minors0 = r3_wzyx * inner12 - r2_wzyx * inner13 + r1_wzyx * inner23;

        float32x4 denom = r0_xyzw * minors0;

        denom += shuffle<1, 0, 3, 2>(denom, denom); // x+y        x+y            z+w            z+w
        denom -= shuffle<2, 2, 0, 0>(denom, denom); // x+y-z-w    x+y-z-w        z+w-x-y        z+w-x-y

        float32x4 rcp_denom_ppnn = 1.0f / denom;

        float32x4 inner30 = shuffle<0, 2, 2, 0>(inner30_01, inner30_01);
        float32x4 inner01 = shuffle<1, 3, 3, 1>(inner30_01, inner30_01);

        float32x4 minors1 = r2_wzyx * inner30 - r0_wzyx * inner23 - r3_wzyx * inner02;
        float32x4 minors2 = r0_wzyx * inner13 - r1_wzyx * inner30 - r3_wzyx * inner01;
        float32x4 minors3 = r1_wzyx * inner02 - r0_wzyx * inner12 + r2_wzyx * inner01;

        Matrix4x4 result;
        result[0] = minors0 * rcp_denom_ppnn;
        result[1] = minors1 * rcp_denom_ppnn;
        result[2] = minors2 * rcp_denom_ppnn;
        result[3] = minors3 * rcp_denom_ppnn;
        return result;
    }

    static inline Matrix4x4 inverseTranspose(float32x4 m0, float32x4 m1, float32x4 m2, float32x4 m3)
    {
        const float32x4 m0zwyz = m0.zwyz;
        const float32x4 m0wzwy = m0.wzwy;
        const float32x4 m1zwyz = m1.zwyz;
        const float32x4 m1wzwy = m1.wzwy;
        const float32x4 m2zwyz = m2.zwyz;
        const float32x4 m2wzwy = m2.wzwy;
        const float32x4 m3zwyz = m3.zwyz;
        const float32x4 m3wzwy = m3.wzwy;
        const float32x4 m0yxxx = m0.yxxx;
        const float32x4 m1yxxx = m1.yxxx;
        const float32x4 m2yxxx = m2.yxxx;
        const float32x4 m3yxxx = m3.yxxx;

        const float32x4 v0 = nmsub(m0wzwy * m1zwyz, m0zwyz, m1wzwy);
        const float32x4 v1 = nmsub(m0zwyz * m2wzwy, m0wzwy, m2zwyz);
        const float32x4 v2 = nmsub(m0wzwy * m3zwyz, m0zwyz, m3wzwy);
        const float32x4 v3 = nmsub(m1wzwy * m2zwyz, m1zwyz, m2wzwy);
        const float32x4 v4 = nmsub(m1zwyz * m3wzwy, m1wzwy, m3zwyz);
        const float32x4 v5 = nmsub(m2zwyz * m3wzwy, m2wzwy, m3zwyz);
        const float32x4 v6 = nmsub(m2wzwy * m3zwyz, m2zwyz, m3wzwy);

        float32x4 a = m1yxxx * v5;
        float32x4 b = m0yxxx * v6;
        float32x4 c = m0yxxx * v4;
        float32x4 d = m0yxxx * v3;
        a = nmsub(a, m2yxxx, v4);
        a = nmsub(a, m3yxxx, v3);
        b = nmsub(b, m2yxxx, v2);
        b = nmsub(b, m3yxxx, v1);
        c =  madd(c, m1yxxx, v2);
        c = nmsub(c, m3yxxx, v0);
        d =  madd(d, m1yxxx, v1);
        d =  madd(d, m2yxxx, v0);

        float32x4 det = 1.0f / dot(m0, a);
        return Matrix4x4(a * det, b * det, c * det, d * det);
    }

    /*
        inveseTRS and inverseTR are based on work by Eric Zhang:
        https://lxjk.github.io/2017/09/03/Fast-4x4-Matrix-Inverse-with-SSE-SIMD-Explained.html
    */

    // Optimized transformation matrix inversion:
    //     - last column is [0 0 0 1]
    //     - matrix may contain: translation, rotation and scaling

    static inline Matrix4x4 inverseTRS(float32x4 m0, float32x4 m1, float32x4 m2, float32x4 m3)
    {
        Matrix4x4 r;

        // 3x3 transpose
        float32x4 t0 = shuffle<0, 1, 0, 1>(m0, m1);
        float32x4 t1 = shuffle<2, 3, 2, 3>(m0, m1);
        r[0] = shuffle<0, 2, 0, 3>(t0, m2);
        r[1] = shuffle<1, 3, 1, 3>(t0, m2);
        r[2] = shuffle<0, 2, 2, 3>(t1, m2);

        // 3x3 de-scale
        float32x4 det = 1.0f / (r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        r[0] = r[0] * det;
        r[1] = r[1] * det;
        r[2] = r[2] * det;

        // last row
        float32x4 t2 = m0 * m3.xxxx + m1 * m3.yyyy + m2 * m3.zzzz;
        r[3] = float32x4(0.0f, 0.0f, 0.0f, 1.0f) - t2;

        return r;
    }

    // Optimized transformation matrix inversion:
    //     - last column is [0 0 0 1]
    //     - matrix may contain: translation and rotation
    //     - no scaling (scale must be 1.0)

    static inline Matrix4x4 inverseTR(float32x4 m0, float32x4 m1, float32x4 m2, float32x4 m3)
    {
        Matrix4x4 r;

        // 3x3 transpose
        float32x4 t0 = shuffle<0, 1, 0, 1>(m0, m1);
        float32x4 t1 = shuffle<2, 3, 2, 3>(m0, m1);
        r[0] = shuffle<0, 2, 0, 3>(t0, m2);
        r[1] = shuffle<1, 3, 1, 3>(t0, m2);
        r[2] = shuffle<0, 2, 2, 3>(t1, m2);

        // last row
        float32x4 t2 = m0 * m3.xxxx + m1 * m3.yyyy + m2 * m3.zzzz;
        r[3] = float32x4(0.0f, 0.0f, 0.0f, 1.0f) - t2;

        return r;
    }

    // Matrix inversion performance
    // CPU: 2.9 GHz Intel Core i9 (compiled for AVX)
    //
    // transpose:        500,000,000 / sec  ( 6 clocks)
    // inverseTR:        200,000,000 / sec  (15 clocks)
    // inverseTRS:        91,000,000 / sec  (32 clocks)
    // inverseTranspose:  67,000,000 / sec  (44 clocks)
    // inverse:           59,000,000 / sec  (50 clocks)

    static inline Matrix4x4 transpose(const Matrix4x4& m)
    {
        return transpose(m[0], m[1], m[2], m[3]);
    }

    static inline Matrix4x4 inverse(const Matrix4x4& m)
    {
        return inverse(m[0], m[1], m[2], m[3]);
    }

    static inline Matrix4x4 inverseTranspose(const Matrix4x4& m)
    {
        return inverseTranspose(m[0], m[1], m[2], m[3]);
    }

    static inline Matrix4x4 inverseTRS(const Matrix4x4& m)
    {
        return inverseTRS(m[0], m[1], m[2], m[3]);
    }

    static inline Matrix4x4 inverseTR(const Matrix4x4& m)
    {
        return inverseTR(m[0], m[1], m[2], m[3]);
    }

} // namespace mango::math
