#pragma once

#include "erhe_renderer/generated/visualization_mode.hpp"

namespace erhe::renderer
{

//enum class Fill_mode : int
//{
//    fill    = 0,
//    outline = 1
//};

enum class Blend_mode : int
{
    opaque      = 0,
    translucent = 1
};

//enum class Selection_mode : int
//{
//    not_selected = 0,
//    selected     = 1,
//    any          = 2
//};

using ::Visualization_mode;

} // namespace erhe::renderer
