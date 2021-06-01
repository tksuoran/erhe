#pragma once

#include "erhe/graphics/vertex_attribute.hpp"
#include "erhe/graphics/vertex_attribute_mapping.hpp"
#include <gsl/pointers>

#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics
{

class Buffer;
class Vertex_format;
class Vertex_input_state;

class Vertex_attribute_mappings
{
public:
    using Mapping_collection = std::vector<std::shared_ptr<Vertex_attribute_mapping>>;

    Vertex_attribute_mappings();
    //Vertex_attribute_mappings(const Vertex_attribute_mappings&) = delete;
    //Vertex_attribute_mappings& operator=(const Vertex_attribute_mappings&) = delete;
    //Vertex_attribute_mappings(Vertex_attribute_mappings&&) = delete;
    //Vertex_attribute_mappings& operator=(Vertex_attribute_mappings&&) = delete;
    ~Vertex_attribute_mappings();

    void add(gl::Attribute_type      shader_type,
             std::string             name,
             Vertex_attribute::Usage usage,
             size_t                  layout_location);

    void add(gl::Attribute_type           shader_type,
             std::string                  name,
             Vertex_attribute::Usage      src_usage,
             Vertex_attribute::Usage_type dst_usage_type,
             size_t                       layout_location);

    void apply_to_vertex_input_state(Vertex_input_state&    vertex_input_state,
                                     gsl::not_null<Buffer*> vertex_buffer,
                                     Vertex_format&         vertex_format) const;

    Mapping_collection mappings;
};

} // namespace erhe::graphics
