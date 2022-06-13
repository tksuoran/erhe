/*
    MANGO Multimedia Development Platform
    Copyright (C) 2012-2021 Twilight Finland 3D Oy Ltd. All rights reserved.
*/
#pragma once

#include <mango/math/vector.hpp>

namespace mango::math
{

    // ------------------------------------------------------------------
    // sRGB <-> linear conversion functions
    // ------------------------------------------------------------------

    float srgbEncode(float linear);
    float srgbDecode(float srgb);

    float32x4 srgbEncode(float32x4 linear);
    float32x4 srgbDecode(float32x4 srgb);

    // ------------------------------------------------------------------
    // sRGB
    // ------------------------------------------------------------------

    // sRGB implements non-linear color container where each color component
    // is stored as 8 bit UNORM sRGB. 
    // The alpha component is always stored as a linear value.

    struct sRGB
    {
        u32 color;

        sRGB() = default;

        sRGB(u32 srgb)
            : color(srgb)
        {
        }

        sRGB(float32x4 linear)
        {
            *this = linear;
        }

        sRGB& operator = (float32x4 linear)
        {
            float32x4 srgb = srgbEncode(linear) * 255.0f;
            srgb.w = linear.w * 255.0f; // pass-through linear alpha
            color = srgb.pack();
            return *this;
        }

        sRGB& operator = (u32 srgb)
        {
            color = srgb;
            return *this;
        }

        operator u32 () const
        {
            return color;
        }

        operator float32x4 () const
        {
            float32x4 srgb;
            srgb.unpack(color);
            float32x4 linear = srgbDecode(srgb / 255.0f);
            linear.w = float(color >> 24) / 255.0f; // pass-through linear alpha
            return linear;
        }
    };

} // namespace mango::math
