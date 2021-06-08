#include "erhe/graphics/vertex_attribute_mapping.hpp"

namespace erhe::graphics
{

Vertex_attribute_mapping::Vertex_attribute_mapping(gl::Attribute_type      shader_type,
                                                   std::string_view        name,
                                                   Vertex_attribute::Usage usage,
                                                   size_t                  layout_location)
    : shader_type    {shader_type}
    , name           {name}
    , src_usage      {usage}
    , dst_usage_type {usage.type}
    , layout_location{layout_location}
{
}

Vertex_attribute_mapping::Vertex_attribute_mapping(gl::Attribute_type           shader_type,
                                                   std::string_view             name,
                                                   Vertex_attribute::Usage      src_usage,
                                                   Vertex_attribute::Usage_type dst_usage_type,
                                                   size_t                       layout_location)
    : shader_type    {shader_type}
    , name           {name}
    , src_usage      {src_usage}
    , dst_usage_type {dst_usage_type}
    , layout_location{layout_location}
{
}

}
