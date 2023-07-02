#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <cstddef>
#include <limits>
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
        const erhe::graphics::Vertex_format&         vertex_format,
        erhe::graphics::Vertex_attribute::Usage_type semantic,
        unsigned int                                 semantic_index
    );

    [[nodiscard]] auto is_valid() -> bool;

    const erhe::graphics::Vertex_attribute* attribute{nullptr};
    gl::Vertex_attrib_type                  data_type{gl::Vertex_attrib_type::float_};
    std::size_t                             offset   {std::numeric_limits<std::size_t>::max()};
    std::size_t                             size     {0};
};

} // namespace erhe::primitive
