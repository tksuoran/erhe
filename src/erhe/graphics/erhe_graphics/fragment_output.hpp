#pragma once

#include "erhe_graphics/shader_resource.hpp"

#include <igl/IGL.h>

#include <string>

namespace erhe::graphics
{

class Fragment_output
{
public:
    std::string  name    {};
    Glsl_type    type    {Glsl_type::float_vec4};
    unsigned int location{0};
};

} // namespace erhe::graphics
