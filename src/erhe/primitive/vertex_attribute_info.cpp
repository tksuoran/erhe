#include "erhe/primitive/vertex_attribute_info.hpp"
#include "erhe/graphics/vertex_format.hpp"

namespace erhe::primitive
{

Vertex_attribute_info::Vertex_attribute_info() = default;

Vertex_attribute_info::Vertex_attribute_info(
    erhe::graphics::Vertex_format* const               vertex_format,
    const gl::Vertex_attrib_type                       default_data_type,
    const std::size_t                                  dimension,
    const erhe::graphics::Vertex_attribute::Usage_type semantic,
    const unsigned int                                 semantic_index
)
    : attribute{vertex_format->find_attribute_maybe(semantic, semantic_index)}
    , data_type{(attribute != nullptr) ? attribute->data_type.type : default_data_type}
    , offset   {(attribute != nullptr) ? attribute->offset         : std::numeric_limits<std::size_t>::max()}
    , size     {size_of_type(data_type) * dimension}
{
}

auto Vertex_attribute_info::is_valid() -> bool
{
    return (attribute != nullptr) && (offset != std::numeric_limits<size_t>::max()) && (size > 0);
}

} // namespace erhe::primitive
