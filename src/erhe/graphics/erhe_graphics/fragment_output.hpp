#pragma once

#include "erhe_graphics/enums.hpp"

#include <string>

namespace erhe::graphics {

class Fragment_output
{
public:
    std::string  name  {};
    Glsl_type    type  {Glsl_type::float_vec4};
    unsigned int location{0};
};

} // namespace erhe::graphics
