#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <string_view>
#include <utility>

namespace erhe::graphics
{

class Vertex_attribute_mapping
{
public:
    size_t                       layout_location;
    gl::Attribute_type           shader_type;
    std::string_view             name;
    Vertex_attribute::Usage      src_usage;
    Vertex_attribute::Usage_type dst_usage_type{Vertex_attribute::Usage_type::automatic};
};

} // namespace erhe::graphics
