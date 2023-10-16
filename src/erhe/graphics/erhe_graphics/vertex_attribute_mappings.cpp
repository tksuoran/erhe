#include "erhe_graphics/vertex_attribute_mappings.hpp"
#include "erhe_graphics/graphics_log.hpp"
#include "erhe_graphics/vertex_format.hpp"
#include "erhe_graphics/shader_resource.hpp"

#include <igl/Common.h>
#include <igl/Device.h>

namespace erhe::graphics
{

using std::string;
using std::string_view;

Vertex_attribute_mappings::Vertex_attribute_mappings(igl::IDevice& device)
    : m_device{device}
{
}

Vertex_attribute_mappings::Vertex_attribute_mappings(
    igl::IDevice&                                   device,
    std::initializer_list<Vertex_attribute_mapping> mappings_in
)
    : mappings{mappings_in}
    , m_device{device}
{
}

void Vertex_attribute_mappings::collect_attributes(
    std::vector<Vertex_input_attribute>& attributes,
    const igl::IBuffer*                  vertex_buffer,
    const Vertex_format&                 vertex_format
) const
{
    const unsigned int max_attribute_count = igl::IGL_VERTEX_ATTRIBUTES_MAX;

    if (vertex_buffer == nullptr) {
        log_vertex_attribute_mappings->error("error: vertex buffer == nullptr");
        return;
    }

    for (const auto& mapping : mappings) {
        if (
            vertex_format.has_attribute(
                mapping.src_usage.type,
                static_cast<unsigned int>(mapping.src_usage.index)
            )
        ) {
            const auto& attribute = vertex_format.find_attribute(
                mapping.src_usage.type,
                static_cast<unsigned int>(mapping.src_usage.index)
            );
            log_vertex_attribute_mappings->trace(
                "vertex attribute: shader type = {}, name = {}, usage = {}, data_type = {}, index = {}",
                c_str(mapping.shader_type),
                mapping.name,
                Vertex_attribute::desc(attribute->usage.type),
                c_str(attribute->data_type),
                attribute->usage.index
            );

            if (attribute == nullptr) {
                log_vertex_attribute_mappings->error("bad vertex input state: attribute == nullptr");
                continue;
            }
            if (mapping.layout_location >= max_attribute_count) {
                log_vertex_attribute_mappings->error("bad vertex input state: layout location >= max attribute count");
                continue;
            }

            attributes.push_back(
                {
                    .layout_location = mapping.layout_location,
                    .vertex_buffer   = vertex_buffer,
                    .stride          = static_cast<uint32_t>(vertex_format.stride()),
                    .shader_type     = attribute->shader_type,
                    .data_type       = attribute->data_type,
                    .offset          = attribute->offset,
                    .divisor         = attribute->divisor
                }
            );
        }
    }
}

} // namespace erhe::graphics
