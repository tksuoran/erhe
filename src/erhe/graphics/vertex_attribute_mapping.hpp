#pragma once

#include "erhe/graphics/vertex_attribute.hpp"

#include <string>
#include <utility>

namespace erhe::graphics
{

class Vertex_attribute_mapping
{
public:
    Vertex_attribute_mapping(gl::Attribute_type      shader_type,
                             std::string             name,
                             Vertex_attribute::Usage usage,
                             size_t                  layout_location)
        : shader_type    {shader_type}
        , name           {std::move(name)}
        , src_usage      {usage}
        , dst_usage_type {usage.type}
        , layout_location{layout_location}
    {
    }

    Vertex_attribute_mapping(gl::Attribute_type           shader_type,
                             std::string                  name,
                             Vertex_attribute::Usage      src_usage,
                             Vertex_attribute::Usage_type dst_usage_type,
                             size_t                       layout_location)
        : shader_type    {shader_type}
        , name           {std::move(name)}
        , src_usage      {src_usage}
        , dst_usage_type {dst_usage_type}
        , layout_location{layout_location}
    {
    }
    Vertex_attribute_mapping(const Vertex_attribute_mapping&) = delete;
    Vertex_attribute_mapping& operator=(const Vertex_attribute_mapping&) = delete;
    Vertex_attribute_mapping(Vertex_attribute_mapping&&) = delete;
    Vertex_attribute_mapping& operator=(Vertex_attribute_mapping&&) = delete;

    gl::Attribute_type           shader_type;
    std::string                  name;
    Vertex_attribute::Usage      src_usage;
    Vertex_attribute::Usage_type dst_usage_type;
    size_t                       layout_location;
};

} // namespace erhe::graphics
