#pragma once

#include "erhe_graphics/vertex_format.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include <cstdint>
#include <vector>

namespace erhe::primitive
{

class Triangle_soup
{
public:
    erhe::graphics::Vertex_format vertex_format;
    gl::Primitive_type            type;
    std::vector<uint8_t>          vertex_data;
    std::vector<uint32_t>         index_data;
};

} // namespace erhe::primitive
