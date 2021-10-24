#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/graphics/vertex_attribute.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace erhe::graphics
{
    class Vertex_format;
}

namespace erhe::primitive
{

class Vertex_attribute_info
{
public:
    Vertex_attribute_info();
    Vertex_attribute_info(
        erhe::graphics::Vertex_format*               vertex_format,
        gl::Vertex_attrib_type                       default_data_type,
        size_t                                       dimension,
        erhe::graphics::Vertex_attribute::Usage_type semantic,
        unsigned int                                 semantic_index
    );

    auto is_valid() -> bool;

    const erhe::graphics::Vertex_attribute* attribute{nullptr};
    gl::Vertex_attrib_type                  data_type{gl::Vertex_attrib_type::float_};
    size_t                                  offset   {std::numeric_limits<size_t>::max()};
    size_t                                  size     {0};
};

} // namespace erhe::primitive
