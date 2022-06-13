/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>
#include <mango/core/bits.hpp>

namespace mango::math
{

    constexpr double pi = 3.14159265358979323846264338327;

    // ------------------------------------------------------------------
    // easing functions
    // ------------------------------------------------------------------

    // Illustration of the ease functions:
    // https://easings.net/

    template <typename ScalarType>
    inline ScalarType easeLinear(ScalarType time)
    {
        return time;
    }

    template <typename ScalarType>
    inline ScalarType easeSmoothStep(ScalarType time)
    {
        time = clamp(time, ScalarType(0.0), ScalarType(1.0));
        return (ScalarType(3.0) - ScalarType(2.0) * time) * time * time;
    }

    // quadratic

    template <typename ScalarType>
    inline ScalarType easeInQuadratic(ScalarType time)
    {
        return time * time;
    }

    template <typename ScalarType>
    inline ScalarType easeOutQuadratic(ScalarType time)
    {
        return time * (ScalarType(2.0) - time);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutQuadratic(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(2.0) * time * time;
        }
        else
        {
            time = (ScalarType(4.0) - (ScalarType(2.0) * time)) * time - ScalarType(1.0);
        }
        return time;
    }

    // cubic

    template <typename ScalarType>
    inline ScalarType easeInCubic(ScalarType time)
    {
        return time * time * time;
    }

    template <typename ScalarType>
    inline ScalarType easeOutCubic(ScalarType time)
    {
        time = time - ScalarType(1.0);
        return time * time * time + ScalarType(1.0);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutCubic(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(4.0) * time * time * time;
        }
        else
        {
            time = (time - ScalarType(1.0)) * ScalarType(2.0);
            return ScalarType(0.5) * time * time * time + ScalarType(1.0);
        }
        return time;
    }

    // quartic

    template <typename ScalarType>
    inline ScalarType easeInQuartic(ScalarType time)
    {
        return (time * time) * (time * time);
    }

    template <typename ScalarType>
    inline ScalarType easeOutQuartic(ScalarType time)
    {
        time = (ScalarType(1.0) - time) * (ScalarType(1.0) - time);
        return ScalarType(1.0) - time * time;
    }

    template <typename ScalarType>
    inline ScalarType easeInOutQuartic(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(8.0) * (time * time) * (time * time);
        }
        else
        {
            time = time - ScalarType(1.0);
            time = ScalarType(1.0) - ScalarType(8.0) * (time * time) * (time * time);
        }
        return time;
    }

    // quintic

    template <typename ScalarType>
    inline ScalarType easeInQuintic(ScalarType time)
    {
        return time * (time * time) * (time * time);
    }

    template <typename ScalarType>
    inline ScalarType easeOutQuintic(ScalarType time)
    {
        return easeInQuintic(time - ScalarType(1.0)) + ScalarType(1.0);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutQuintic(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(16.0) * time * (time * time) * (time * time);
        }
        else
        {
            time = ScalarType(1.0) - time;
            time = ScalarType(1.0) - ScalarType(16.0) * time * (time * time) * (time * time);
        }
        return time;
    }

    // sine

    template <typename ScalarType>
    inline ScalarType easeInSine(ScalarType time)
    {
        return std::sin(ScalarType(pi * 0.5) * (time - ScalarType(1.0))) + ScalarType(1.0);
    }

    template <typename ScalarType>
    inline ScalarType easeOutSine(ScalarType time)
    {
        return std::sin(ScalarType(pi * 0.5) * time);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutSine(ScalarType time)
    {
        return (ScalarType(1.0) - std::cos(time * ScalarType(pi))) * ScalarType(0.5);
    }

    // circular

    template <typename ScalarType>
    inline ScalarType easeInCircular(ScalarType time)
    {
        return ScalarType(1.0) - std::sqrt(ScalarType(1.0) - time * time);
    }

    template <typename ScalarType>
    inline ScalarType easeOutCircular(ScalarType time)
    {
        return std::sqrt((ScalarType(2.0) - time) * time);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutCircular(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(1.0) - std::sqrt(ScalarType(1.0) - ScalarType(4.0) * time * time);
        }
        else
        {
            time = ScalarType(1.0) + std::sqrt((ScalarType(3.0) - (ScalarType(2.0) * time)) * ((ScalarType(2.0) * time) - ScalarType(1.0)));
        }
        return time * ScalarType(0.5);
    }

    // exponential

    template <typename ScalarType>
    inline ScalarType easeInExponential(ScalarType time)
    {
        return time <= ScalarType(0.0) ?
            ScalarType(0.0) :
            std::pow(ScalarType(2.0), ScalarType(10.0) * (time - ScalarType(1.0)));
    }

    template <typename ScalarType>
    inline ScalarType easeOutExponential(ScalarType time)
    {
        return time >= ScalarType(1.0) ?
            ScalarType(1.0) :
            ScalarType(1.0) - std::pow(ScalarType(2.0), ScalarType(-10.0) * time);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutExponential(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(0.5) * std::pow(ScalarType(2.0), ScalarType(20.0) * time - ScalarType(10.0));
        }
        else
        {
            time = ScalarType(1.0) - ScalarType(0.5) * std::pow(ScalarType(2.0), ScalarType(10.0) - ScalarType(20.0) * time);
        }
        return time;
    }

    // elastic

    template <typename ScalarType>
    inline ScalarType easeInElastic(ScalarType time)
    {
        return std::pow(ScalarType(2.0), ScalarType(10.0) * (time - ScalarType(1.0))) * std::sin(ScalarType(13.0 * pi * 0.5) * time);
    }

    template <typename ScalarType>
    inline ScalarType easeOutElastic(ScalarType time)
    {
	    return std::sin(ScalarType(-13.0 * pi * 0.5) * (time + ScalarType(1.0))) * pow(ScalarType(2.0), ScalarType(-10.0) * time) + ScalarType(1.0);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutElastic(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
		    time = ScalarType(0.5) * std::sin(ScalarType(13.0 * pi * 0.5) * (ScalarType(2.0) * time)) * std::pow(ScalarType(2.0), ScalarType(10.0) * ((ScalarType(2.0) * time) - ScalarType(1.0)));
        }
        else
        {
		    time = ScalarType(0.5) * (std::sin(ScalarType(-13.0 * pi * 0.5) * ((ScalarType(2.0) * time - ScalarType(1.0)) + ScalarType(1.0))) * std::pow(ScalarType(2.0), ScalarType(-10.0) * (ScalarType(2.0) * time - ScalarType(1.0))) + ScalarType(2.0));
        }
        return time;
    }

    // back

    template <typename ScalarType>
    inline ScalarType easeInBack(ScalarType time)
    {
        return time * (time * time - std::sin(ScalarType(pi) * time));
    }

    template <typename ScalarType>
    inline ScalarType easeOutBack(ScalarType time)
    {
        time = ScalarType(1.0) - time;
        return ScalarType(1.0) - time * (time * time - std::sin(ScalarType(pi) * time));
    }

    template <typename ScalarType>
    inline ScalarType easeInOutBack(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(2.0) * time;
            time = ScalarType(0.5) * time * (time * time - std::sin(time * ScalarType(pi)));
        }
        else
        {
            time = ScalarType(1.0) - (ScalarType(2.0) * time - ScalarType(1.0));
            time = ScalarType(0.5) * (ScalarType(1.0) - time * (time * time - std::sin(time * ScalarType(pi)))) + ScalarType(0.5);
        }
        return time;
    }

    // bounce

    template <typename ScalarType>
    inline ScalarType easeOutBounce(ScalarType time)
    {
        if (time < ScalarType(4.0 / 11.0))
        {
            time = (ScalarType(121.0) * time * time) / ScalarType(16.0);
        }
        else if (time < ScalarType(8.0 / 11.0))
        {
            time = ScalarType(363.0 / 40.0) * time * time - ScalarType(99.0 / 10.0) * time + ScalarType(17.0 / 5.0);
        }
        else if (time < ScalarType(9.0 / 10.0))
        {
            time = ScalarType(4356.0 / 361.0) * time * time - ScalarType(35442.0 / 1805.0) * time + ScalarType(16061.0 / 1805.0);
        }
        else
        {
            time = ScalarType(54.0 / 5.0) * time * time - ScalarType(513.0 / 25.0) * time + ScalarType(268.0 / 25.0);
        }
        return time;
    }

    template <typename ScalarType>
    inline ScalarType easeInBounce(ScalarType time)
    {
        return ScalarType(1.0) - easeOutBounce(ScalarType(1.0) - time);
    }

    template <typename ScalarType>
    inline ScalarType easeInOutBounce(ScalarType time)
    {
        if (time < ScalarType(0.5))
        {
            time = ScalarType(0.5) * easeInBounce(ScalarType(2.0) * time);
        }
        else
        {
            time = ScalarType(0.5) * easeOutBounce(ScalarType(2.0) * time - ScalarType(1.0)) + ScalarType(0.5);
        }
        return time;
    }

    // ------------------------------------------------------------------
    // spline interpolation
    // ------------------------------------------------------------------

    template <typename VectorType, typename ScalarType>
    inline VectorType bezier(VectorType a, VectorType b, VectorType c, VectorType d, ScalarType time)
    {
        ScalarType s = ScalarType(1.0) - time;
        return a * (s * s) * s +
               b * ScalarType(3.0) * (s * s) * time +
               c * ScalarType(3.0) * s * (time * time) +
               d * time * (time * time);
    }

    template <typename VectorType, typename ScalarType>
    inline VectorType catmull(VectorType a, VectorType b, VectorType c, VectorType d, ScalarType time)
    {
        VectorType a5 = a * ScalarType(-0.5);
        VectorType d5 = d * ScalarType(0.5);
        return VectorType(time * (time * time) * (a5 + (b - c) * ScalarType(1.5) + d5) +
                          (time * time) * (a - b * ScalarType(2.5) + c * ScalarType(2.0) - d5) +
                          time * (a5 + c * ScalarType(0.5)) + b);
    }

    template <typename VectorType, typename ScalarType>
    inline VectorType bicubic(VectorType a, VectorType b, VectorType c, VectorType d, ScalarType time)
    {
        VectorType s1 = c - a;
        VectorType s2 = d - b;
        return (time * time) * time * ((b - c) * ScalarType(2.0) + s1 + s2) +
               (time * time) * ((c - b) * ScalarType(3.0) - s1 * ScalarType(2.0) - s2) +
               time * s1 + b;
    }

    template <typename VectorType, typename ScalarType>
    inline VectorType bspline(VectorType a, VectorType b, VectorType c, VectorType d, ScalarType time)
    {
        VectorType c3 = c * ScalarType(3.0);
        VectorType a3 = a * ScalarType(3.0);
        return ((time * time) * time * (b * ScalarType(3.0) - c3 + d - a) +
                (time * time) * (a3 - b * ScalarType(6.0) + c3) +
                time * (c3 - a3) + (a + b * ScalarType(4.0) + c)) / ScalarType(6.0);
    }

    template <typename VectorType, typename ScalarType>
    inline VectorType hermite(VectorType a, VectorType b, VectorType atangent, VectorType btangent, ScalarType time)
    {
        ScalarType time2 = time * time;
        ScalarType time3 = time * time2;
        return a * (ScalarType(2.0) * time3 - ScalarType(3.0) * time2 + ScalarType(1.0)) -
               b * (ScalarType(2.0) * time3 - ScalarType(3.0) * time2) +
               atangent * (time3 - ScalarType(2.0) * time2 + time) +
               btangent * (time3 - time2);
    }

} // namespace mango::math
