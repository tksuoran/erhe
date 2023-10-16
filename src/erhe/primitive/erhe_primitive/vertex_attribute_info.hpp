#pragma once

#include "erhe_graphics/vertex_attribute.hpp"

#include <cstddef>
#include <limits>

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
    igl::VertexAttributeFormat                  data_type{igl::VertexAttributeFormat::float_};
    std::size_t                             offset   {std::numeric_limits<std::size_t>::max()};
    std::size_t                             size     {0};
};

} // namespace erhe::primitive
