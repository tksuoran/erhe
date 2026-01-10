#include "erhe_primitive/vertex_attribute_info.hpp"

namespace erhe::primitive {

Vertex_attribute_info::Vertex_attribute_info() = default;

Vertex_attribute_info::Vertex_attribute_info(
    const erhe::dataformat::Vertex_format&         vertex_format,
    const erhe::dataformat::Vertex_attribute_usage usage,
    const unsigned int                             usage_index
)
{
    erhe::dataformat::Attribute_stream info = vertex_format.find_attribute(usage, usage_index);
    if (info.attribute != nullptr) {
        attribute = info.attribute;
        format    = info.attribute->format;
        binding   = info.stream->binding;
        offset    = info.attribute->offset;
        size      = erhe::dataformat::get_format_size_bytes(info.attribute->format);
    }
}

auto Vertex_attribute_info::is_valid() -> bool
{
    return (attribute != nullptr) && (offset != std::numeric_limits<std::size_t>::max()) && (size > 0);
}

} // namespace erhe::primitive
