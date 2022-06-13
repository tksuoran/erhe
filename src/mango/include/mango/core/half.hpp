/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2020 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
/*
    Based on Half/Float conversion code by Fabian "ryg" Giesen.
*/
#pragma once

#include <algorithm>
#include <mango/core/configure.hpp>

// NOTES:
// https://en.wikipedia.org/wiki/Half-precision_floating-point_format

namespace mango
{

    // -----------------------------------------------------------------------
    // Double
    // -----------------------------------------------------------------------

    union Double
    {
        enum
        {
            SIGN = 1,
            EXPONENT = 11,
            MANTISSA = 52,
            BIAS = 1023
        };

        u64 u;
        double f;
        struct
        {
            u64 mantissa : MANTISSA;
            u64 exponent : EXPONENT;
            u64 sign : SIGN;
        };

        Double()
            : mantissa(0)
            , exponent(0)
            , sign(0)
        {
        }

        explicit Double(u64 sign, u64 exponent, u64 mantissa)
            : mantissa(mantissa)
            , exponent(exponent)
            , sign(sign)
        {
        }

        explicit Double(u64 bits)
            : u(bits)
        {
        }

        Double(double s)
            : f(s)
        {
        }

        ~Double()
        {
        }

        Double& operator = (double s)
        {
            f = s;
            return *this;
        }

        operator double () const
        {
            return f;
        }
    };

    // -----------------------------------------------------------------------
    // Float
    // -----------------------------------------------------------------------

    union Float
    {
        enum
        {
            SIGN = 1,
            EXPONENT = 8,
            MANTISSA = 23,
            BIAS = 127
        };

        u32 u;
        float f;
        struct
        {
            u32 mantissa : MANTISSA;
            u32 exponent : EXPONENT;
            u32 sign : SIGN;
        };

        Float()
            : mantissa(0)
            , exponent(0)
            , sign(0)
        {
        }

        explicit Float(u32 sign, u32 exponent, u32 mantissa)
            : mantissa(mantissa)
            , exponent(exponent)
            , sign(sign)
        {
        }

        explicit Float(u32 bits)
            : u(bits)
        {
        }

        Float(float s)
            : f(s)
        {
        }

        ~Float()
        {
        }

        Float& operator = (float s)
        {
            f = s;
            return *this;
        }

        operator float () const
        {
            return f;
        }

        // pack

        template <int Sign, int Exponent, int Mantissa>
        static u32 pack(float value)
        {
            u32 result = 0;

            Float temp = value;
            u32 vsign = temp.sign;
            temp.sign = 0;

            if (temp.exponent == (1 << Float::EXPONENT) - 1)
            {
                // Inf / NaN
                result = ((1 << Exponent) - 1) << Mantissa;
                result |= temp.mantissa ? temp.mantissa >> (Float::MANTISSA - Mantissa) : 0; // Nan -> qNaN, Inf -> Inf
            }
            else
            {
                // (De)Normalized number or zero
                const Float magic(0, (1 << (Exponent - 1)) - 1, 0);
                temp.u &= ~((1 << (Float::MANTISSA - Mantissa - 1)) - 1);
                temp.f *= magic.f;
                temp.u += (1 << (Float::MANTISSA - Mantissa - 1)); // Rounding bias

                const Float infty(0, (1 << Exponent) - 1, 0);
                if (temp.u > infty.u)
                {
                    // Clamp to signed Infinity
                    temp.u = infty.u;
                }

                result = temp.u >> (Float::MANTISSA - Mantissa);
            }

            result |= ((vsign & Sign) << (Exponent + Mantissa));

            return result;
        }

        // unpack

        template <u32 Sign, u32 Exponent, u32 Mantissa>
        static float unpack(u32 sign, u32 exponent, u32 mantissa)
        {
            Float result;

            if (!exponent)
            {
                // Zero / Denormal
                const Float magic(0, BIAS - 1, 0);
                result.u = magic.u + mantissa;
                result.f -= magic.f;
            }
            else
            {
                result.mantissa = mantissa << (MANTISSA - Mantissa);

                if (exponent == (1 << Exponent) - 1)
                {
                    // Inf / NaN
                    result.exponent = (1 << EXPONENT) - 1;
                }
                else
                {
                    const int bias = (1 << (Exponent - 1)) - 1;
                    result.exponent = BIAS - bias + exponent;
                }
            }

            result.sign = sign & SIGN;

            return result;
        }
    };

    // -----------------------------------------------------------------------
    // Half
    // -----------------------------------------------------------------------

    union Half
    {
        enum
        {
            SIGN = 1,
            EXPONENT = 5,
            MANTISSA = 10,
            BIAS = 15
        };

        u16 u;
        struct
        {
            u16 mantissa : MANTISSA;
            u16 exponent : EXPONENT;
            u16 sign : SIGN;
        };

        Half()
            : mantissa(0)
            , exponent(0)
            , sign(0)
        {
        }

        Half(u16 sign, u16 exponent, u16 mantissa)
            : mantissa(mantissa)
            , exponent(exponent)
            , sign(sign)
        {
        }

        Half(u16 bits)
            : u(bits)
        {
        }

        Half(float s)
        {
            *this = s;
        }

        Half(double s)
        {
            *this = float(s);
        }

        ~Half()
        {
        }

        Half& operator = (float s)
        {
            u = u16(Float::pack<SIGN, EXPONENT, MANTISSA>(s));
            return *this;
        }

        Half& operator = (double s)
        {
            u = u16(Float::pack<SIGN, EXPONENT, MANTISSA>(float(s)));
            return *this;
        }

        operator float () const
        {
            return Float::unpack<SIGN, EXPONENT, MANTISSA>(sign, exponent, mantissa);
        }

        operator double () const
        {
            return double(Float::unpack<SIGN, EXPONENT, MANTISSA>(sign, exponent, mantissa));
        }
    };

    // ----------------------------------------------------------------------------
    // UnsignedInt24
    // ----------------------------------------------------------------------------

    struct UnsignedInt24
    {
        u8 data[3];

        UnsignedInt24()
            : data { 0 }
        {
        }

        UnsignedInt24(u32 v)
        {
            *this = v;
        }

        operator u32 () const
        {
            u32 v = (data[2] << 16) | (data[1] << 8) | data[0];
            return v;
        }

        UnsignedInt24& operator = (u32 v)
        {
            data[0] = u8(v);
            data[1] = u8(v >> 8);
            data[2] = u8(v >> 16);
            return *this;
        }
    };

    // -----------------------------------------------------------------------
    // conversions
    // -----------------------------------------------------------------------

    static inline double unsignedIntToDouble(u32 i)
    {
        const double bias = (1ll << 52) * 1.5;
        Double x(u64(0x4338000000000000UL | i));
        return x.f - bias;
    }

    static inline u32 doubleToUnsignedInt(double d)
    {
        const double bias = (1ll << 52) * 1.5;
        Double x = d + bias;
        return u32(x.u);
    }

    // -----------------------------------------------------------------------
    // types
    // -----------------------------------------------------------------------

    using u24 = UnsignedInt24;

    using float16 = Half;
    using float32 = float;
    using float64 = double;

} // namespace mango
