#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <string_view>
#include <utility>

namespace erhe::graphics
{

class Vertex_attribute_mapping
{
public:
    Vertex_attribute_mapping(gl::Attribute_type      shader_type,
                             std::string_view        name,
                             Vertex_attribute::Usage usage,
                             size_t                  layout_location);;
    Vertex_attribute_mapping(gl::Attribute_type           shader_type,
                             std::string_view             name,
                             Vertex_attribute::Usage      src_usage,
                             Vertex_attribute::Usage_type dst_usage_type,
                             size_t                       layout_location);
    Vertex_attribute_mapping(const Vertex_attribute_mapping&) = delete;
    void operator=          (const Vertex_attribute_mapping&) = delete;
    Vertex_attribute_mapping(Vertex_attribute_mapping&&)      = delete;
    void operator=          (Vertex_attribute_mapping&&)      = delete;

    gl::Attribute_type           shader_type;
    std::string                  name;
    Vertex_attribute::Usage      src_usage;
    Vertex_attribute::Usage_type dst_usage_type;
    size_t                       layout_location;
};

} // namespace erhe::graphics
