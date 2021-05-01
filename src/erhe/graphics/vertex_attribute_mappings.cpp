#include "erhe/graphics/vertex_attribute_mappings.hpp"
#include "erhe/graphics/log.hpp"
#include "erhe/graphics/vertex_format.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"

namespace erhe::graphics
{

using erhe::log::Log;
using std::string;

void Vertex_attribute_mappings::add(gl::Attribute_type      shader_type,
                                    string                  name,
                                    Vertex_attribute::Usage usage,
                                    size_t                  layout_location)
{
    mappings.emplace_back(shader_type, std::move(name), usage, layout_location);
}

void Vertex_attribute_mappings::add(gl::Attribute_type           shader_type,
                                    string                       name,
                                    Vertex_attribute::Usage      src_usage,
                                    Vertex_attribute::Usage_type dst_usage_type,
                                    size_t                       layout_location)
{
    mappings.emplace_back(shader_type, std::move(name), src_usage, dst_usage_type, layout_location);
}

void Vertex_attribute_mappings::apply_to_vertex_input_state(Vertex_input_state&    vertex_input_state,
                                                            gsl::not_null<Buffer*> vertex_buffer,
                                                            Vertex_format&         vertex_format) const
{
    Expects(vertex_input_state.bindings().empty());

    log_vertex_attribute_mappings.trace("Vertex_attribute_mappings::apply_to_vertex_input_state()\n");
    Log::Indenter log_indent;

    for (const auto& mapping : mappings)
    {
        if (vertex_format.has_attribute(mapping.src_usage.type,
                                        static_cast<unsigned int>(mapping.src_usage.index)))
        {
            auto attribute = vertex_format.find_attribute(mapping.src_usage.type,
                                                          static_cast<unsigned int>(mapping.src_usage.index));
            log_vertex_attribute_mappings.trace("vertex attribute: shader type = {}, name = {}, usage = {}, data_type = {}, dimension = {}, index = {}\n",
                                                gl::c_str(mapping.shader_type),
                                                mapping.name,
                                                Vertex_attribute::desc(attribute->usage.type),
                                                gl::c_str(attribute->data_type.type),
                                                static_cast<unsigned int>(attribute->data_type.dimension),
                                                static_cast<unsigned int>(attribute->usage.index));

            vertex_input_state.emplace_back(vertex_buffer, mapping, attribute, vertex_format.stride());
        }
    }
}

} // namespace erhe::graphics
