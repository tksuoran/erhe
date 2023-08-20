#include "erhe_primitive/vertex_attribute_info.hpp"
#include "erhe_gl/gl_helpers.hpp"
#include "erhe_graphics/vertex_format.hpp"

#include <cstddef>

namespace erhe::primitive
{

Vertex_attribute_info::Vertex_attribute_info() = default;

Vertex_attribute_info::Vertex_attribute_info(
    const erhe::graphics::Vertex_format&               vertex_format,
    const erhe::graphics::Vertex_attribute::Usage_type semantic,
    const unsigned int                                 semantic_index
)
{
    attribute = vertex_format.find_attribute_maybe(semantic, semantic_index);
    if (attribute != nullptr) {
        data_type = attribute->data_type.type;
        offset    = attribute->offset;
        size      = attribute->size();
    }
}

auto Vertex_attribute_info::is_valid() -> bool
{
    return (attribute != nullptr) && (offset != std::numeric_limits<std::size_t>::max()) && (size > 0);
}

} // namespace erhe::primitive
