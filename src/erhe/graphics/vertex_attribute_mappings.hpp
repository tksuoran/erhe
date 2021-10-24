#pragma once

#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_attribute_mapping.hpp"
#include <gsl/pointers>

#include <memory>
#include <string_view>
#include <vector>

namespace erhe::graphics
{

class Buffer;
class Vertex_format;
class Vertex_input_state;

class Vertex_attribute_mappings final
{
public:
    using Mapping_collection = std::vector<std::shared_ptr<Vertex_attribute_mapping>>;

    Vertex_attribute_mappings();
    ~Vertex_attribute_mappings();

    void add(
        const gl::Attribute_type      shader_type,
        const std::string_view        name,
        const Vertex_attribute::Usage usage,
        const size_t                  layout_location
    );

    void add(
        const gl::Attribute_type           shader_type,
        const std::string_view             name,
        const Vertex_attribute::Usage      src_usage,
        const Vertex_attribute::Usage_type dst_usage_type,
        const size_t                       layout_location
    );

    void apply_to_vertex_input_state(
        Vertex_input_state&          vertex_input_state,
        gsl::not_null<const Buffer*> vertex_buffer,
        const Vertex_format&         vertex_format
    ) const;

    Mapping_collection mappings;
};

} // namespace erhe::graphics
