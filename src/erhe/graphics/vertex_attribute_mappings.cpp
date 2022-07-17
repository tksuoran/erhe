#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/configuration.hpp"
#include "erhe/graphics/graphics_log.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/gl/enum_string_functions.hpp"

namespace erhe::graphics
{

using std::string;
using std::string_view;

void Vertex_attribute_mappings::add(
    const Vertex_attribute_mapping& attribute
)
{
    mappings.push_back(attribute);
}

void Vertex_attribute_mappings::collect_attributes(
    std::vector<Vertex_input_attribute>& attributes,
    const Buffer*                        vertex_buffer,
    const Vertex_format&                 vertex_format
) const
{
    const unsigned int max_attribute_count = std::min(
        MAX_ATTRIBUTE_COUNT,
        erhe::graphics::Instance::limits.max_vertex_attribs
    );

    if (vertex_buffer == nullptr)
    {
        log_vertex_attribute_mappings->error("error: vertex buffer == nullptr");
        return;
    }

    for (const auto& mapping : mappings)
    {
        if (
            vertex_format.has_attribute(
                mapping.src_usage.type,
                static_cast<unsigned int>(mapping.src_usage.index)
            )
        )
        {
            const auto& attribute = vertex_format.find_attribute(
                mapping.src_usage.type,
                static_cast<unsigned int>(mapping.src_usage.index)
            );
            log_vertex_attribute_mappings->trace(
                "vertex attribute: shader type = {}, name = {}, usage = {}, data_type = {}, dimension = {}, index = {}",
                gl::c_str(mapping.shader_type),
                mapping.name,
                Vertex_attribute::desc(attribute->usage.type),
                gl::c_str(attribute->data_type.type),
                attribute->data_type.dimension,
                attribute->usage.index
            );

                if (attribute == nullptr)
            {
                log_vertex_attribute_mappings->error("bad vertex input state: attribute == nullptr");
                continue;
            }
            if (mapping.layout_location >= max_attribute_count)
            {
                log_vertex_attribute_mappings->error("bad vertex input state: layout location >= max attribute count");
                continue;
            }

            attributes.push_back(
                {
                    .layout_location = static_cast<GLuint>(mapping.layout_location),       // layout_location
                    .vertex_buffer   = vertex_buffer,                                      // vertex buffer
                    .stride          = static_cast<GLsizei>(vertex_format.stride()),       // stride
                    .dimension       = static_cast<GLint>(attribute->data_type.dimension), // dimension
                    .shader_type     = attribute->shader_type,                             // shader type
                    .data_type       = attribute->data_type.type,                          // data type
                    .normalized      = attribute->data_type.normalized,                    // normalized
                    .offset          = static_cast<GLuint>(attribute->offset),             // offset
                    .divisor         = attribute->divisor                                  // divisor
                }
            );
        }
    }
}

} // namespace erhe::graphics
