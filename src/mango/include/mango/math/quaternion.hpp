/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/math/matrix.hpp>

namespace mango::math
{

    struct AngleAxis;

    // ------------------------------------------------------------------
    // Quaternion
    // ------------------------------------------------------------------

    struct Quaternion
    {
        union
        {
            struct { float x, y, z, w; };
            float data[4];
        };

        explicit Quaternion()
        {
        }

        explicit Quaternion(float x, float y, float z, float w)
            : x(x)
            , y(y)
            , z(z)
            , w(w)
        {
        }

        Quaternion(const Quaternion& q)
            : x(q.x)
            , y(q.y)
            , z(q.z)
            , w(q.w)
        {
        }

        Quaternion(const Vector<float, 3>& v, float w)
            : x(v.x)
            , y(v.y)
            , z(v.z)
            , w(w)
        {
        }

        Quaternion(const Vector<float, 4>& v)
        {
            x = v.x;
            y = v.y;
            z = v.z;
            w = v.w;
        }

        Quaternion(const Matrix<float, 4, 4>& m)
        {
            *this = m;
        }

        Quaternion(const AngleAxis& a)
        {
            *this = a;
        }

        ~Quaternion()
        {
        }

        const Quaternion& operator = (const Quaternion& q)
        {
            x = q.x;
            y = q.y;
            z = q.z;
            w = q.w;
            return *this;
        }

        const Quaternion& operator = (const Matrix<float, 4, 4>& m);
        const Quaternion& operator = (const AngleAxis& a);

        const Quaternion& operator += (const Quaternion& q)
        {
            x += q.x;
            y += q.y;
            z += q.z;
            w += q.w;
            return *this;
        }

        const Quaternion& operator -= (const Quaternion& q)
        {
            x -= q.x;
            y -= q.y;
            z -= q.z;
            w -= q.w;
            return *this;
        }

        const Quaternion& operator *= (const Quaternion& q)
        {
            float s = w * q.w - (x * q.x + y * q.y + z * q.z);
            *this = Quaternion(q.x * w + x * q.w + y * q.z - z * q.y,
                               q.y * w + y * q.w + z * q.x - x * q.z,
                               q.z * w + z * q.w + x * q.y - y * q.x, s);
            return *this;
        }

        const Quaternion& operator *= (float s)
        {
            x *= s;
            y *= s;
            z *= s;
            w *= s;
            return *this;
        }

        Quaternion operator + () const
        {
            return *this;
        }

        Quaternion operator - () const
        {
    		return Quaternion(-x, -y, -z, -w);
        }

        operator Vector<float, 4> () const
        {
            return Vector<float, 4>(x, y, z, w);
        }

        float operator [] (int index) const
        {
            return data[index];
        }

        float& operator [] (int index)
        {
            return data[index];
        }

        static Quaternion identity();
        static Quaternion rotateX(float angle);
        static Quaternion rotateY(float angle);
        static Quaternion rotateZ(float angle);
        static Quaternion rotateXYZ(float xangle, float yangle, float zangle);
        static Quaternion rotate(const Vector<float, 3>& from, const Vector<float, 3>& to);
    };

    // ------------------------------------------------------------------
    // AngleAxis
    // ------------------------------------------------------------------

    struct AngleAxis
    {
        float angle;
        Vector<float, 3> axis;

        AngleAxis() = default;

        AngleAxis(float Angle, const Vector<float, 3>& Axis)
            : angle(Angle)
            , axis(Axis)
        {
        }

        AngleAxis(const Matrix4x4& m);
        AngleAxis(const Quaternion& q);
        ~AngleAxis() = default;
    };

    // ------------------------------------------------------------------
    // Quaternion operators
    // ------------------------------------------------------------------

    static inline Quaternion operator + (const Quaternion& a, const Quaternion& b)
    {
		return Quaternion(a.x + b.x,
                          a.y + b.y,
                          a.z + b.z,
                          a.w + b.w);
    }

    static inline Quaternion operator - (const Quaternion& a, const Quaternion& b)
    {
		return Quaternion(a.x - b.x,
                          a.y - b.y,
                          a.z - b.z,
                          a.w - b.w);
    }

    static inline Quaternion operator * (const Quaternion& q, float s)
    {
		return Quaternion(q.x * s,
                          q.y * s,
                          q.z * s,
                          q.w * s);
    }

    static inline Quaternion operator * (float s, const Quaternion& q)
    {
		return Quaternion(q.x * s,
                          q.y * s,
                          q.z * s,
                          q.w * s);
    }

    static inline Quaternion operator * (const Quaternion& a, const Quaternion& b)
    {
        float x = a.y * b.z - a.z * b.y + a.x * b.w + b.x * a.w;
        float y = a.z * b.x - a.x * b.z + a.y * b.w + b.y * a.w;
        float z = a.x * b.y - a.y * b.x + a.z * b.w + b.z * a.w;
        float w = a.w * b.w - (a.x * b.x + a.y * b.y + a.z * b.z);
        return Quaternion(x, y, z, w);
    }

    static inline Vector<float, 3> operator * (const Vector<float, 3>& v, const Quaternion& q)
    {
        float nx = q.y * v.z - q.z * v.y;
        float ny = q.z * v.x - q.x * v.z;
        float nz = q.x * v.y - q.y * v.x;
        float x = v.x + (q.y * nz - q.z * ny + q.w * nx) * 2.0f;
        float y = v.y + (q.z * nx - q.x * nz + q.w * ny) * 2.0f;
        float z = v.z + (q.x * ny - q.y * nx + q.w * nz) * 2.0f;
        return Vector<float, 3>(x, y, z);
    }

    // ------------------------------------------------------------------
    // Quaternion functions
    // ------------------------------------------------------------------

    static inline float dot(const Quaternion& a, const Quaternion& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    static inline float norm(const Quaternion& q)
    {
        return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    }

    static inline float square(const Quaternion& q)
    {
        return q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    }

    static inline float mod(const Quaternion& q)
    {
        return float(std::sqrt(norm(q)));
    }

    static inline Quaternion negate(const Quaternion& q)
    {
        const float s = -1.0f / mod(q);
        return Quaternion(q.x * s,
                          q.y * s,
                          q.z * s,
                          q.w * -s);
    }

    static inline Quaternion inverse(const Quaternion& q)
    {
        const float s = -1.0f / norm(q);
        return Quaternion(q.x * s,
                          q.y * s,
                          q.z * s,
                          q.w * -s);
    }

    static inline Quaternion conjugate(const Quaternion& q)
    {
        return Quaternion(-q.x, -q.y, -q.z, q.w);
    }

    Quaternion log(const Quaternion& q);
    Quaternion exp(const Quaternion& q);
    Quaternion pow(const Quaternion& q, float p);
    Quaternion normalize(const Quaternion& q);
    Quaternion lndif(const Quaternion& a, const Quaternion& b);
    Quaternion lerp(const Quaternion& a, const Quaternion& b, float time);
    Quaternion slerp(const Quaternion& a, const Quaternion& b, float time);
    Quaternion slerp(const Quaternion& a, const Quaternion& b, int spin, float time);
    Quaternion squad(const Quaternion& p, const Quaternion& a, const Quaternion& b, const Quaternion& q, float time);

} // namespace mango::math
