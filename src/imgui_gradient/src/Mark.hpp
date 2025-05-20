#pragma once

#include "ColorRGBA.hpp"
#include "RelativePosition.hpp"

namespace ImGG {

struct Mark {
    RelativePosition position;
    ColorRGBA        color;

    Mark( // We need to explicitly define the constructor in order to compile with MacOS Clang in C++ 11
        RelativePosition position = RelativePosition{0.f},
        ColorRGBA        color    = {0.f, 0.f, 0.f, 1.f}
    )
        : position{position}
        , color{color}
    {}

    friend auto operator==(const Mark& a, const Mark& b) -> bool
    {
        return a.position == b.position
               && a.color == b.color;
    };
};

} // namespace ImGG