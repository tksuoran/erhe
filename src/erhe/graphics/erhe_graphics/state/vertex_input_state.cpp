#include "erhe_graphics/state/vertex_input_state.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_vertex_input_state.hpp"
#endif

#include "erhe_dataformat/vertex_format.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include <memory>

namespace erhe::graphics {

auto get_vertex_divisor(erhe::dataformat::Vertex_step step) -> unsigned int
{
    switch (step) {
        case erhe::dataformat::Vertex_step::Step_per_vertex:   return 0;
        case erhe::dataformat::Vertex_step::Step_per_instance: return 1;
        default:
            ERHE_FATAL("Invalid Vertex_step value");
            return 0;
    }
}

auto get_attribute_name(const erhe::dataformat::Vertex_attribute& attribute) -> std::string
{
    using namespace erhe::dataformat;
    switch (attribute.usage_type) {
        case Vertex_attribute_usage::position: return "a_position";
        case Vertex_attribute_usage::normal: {
            return attribute.usage_index == 0 ? "a_normal" : fmt::format("a_normal_{}",  attribute.usage_index);
        }
        case Vertex_attribute_usage::tangent:       return "a_tangent";
        case Vertex_attribute_usage::bitangent:     return "a_bitangent";
        case Vertex_attribute_usage::color:         return fmt::format("a_color_{}",         attribute.usage_index);
        case Vertex_attribute_usage::tex_coord:     return fmt::format("a_texcoord_{}",      attribute.usage_index);
        case Vertex_attribute_usage::joint_indices: return fmt::format("a_joint_indices_{}", attribute.usage_index);
        case Vertex_attribute_usage::joint_weights: return fmt::format("a_joint_weights_{}", attribute.usage_index);
        case Vertex_attribute_usage::custom:        return fmt::format("a_custom_{}",        attribute.usage_index);
        default: {
            return {};
        }
    }
}

auto Vertex_input_state_data::make(const erhe::dataformat::Vertex_format& vertex_format) -> Vertex_input_state_data
{
    Vertex_input_state_data result;
    unsigned int layout_location = 0;
    for (const erhe::dataformat::Vertex_stream& stream : vertex_format.streams) {
        result.bindings.push_back(
            Vertex_input_binding{
                .binding = stream.binding,
                .stride  = stream.stride,
                .divisor = get_vertex_divisor(stream.step)
            }
        );
        for (const erhe::dataformat::Vertex_attribute& attribute : stream.attributes) {
            Vertex_input_attribute input_attribute;
            input_attribute.layout_location = layout_location++;
            input_attribute.binding         = stream.binding;
            input_attribute.stride          = stream.stride;
            input_attribute.format          = attribute.format;
            input_attribute.offset          = attribute.offset;
            input_attribute.name            = get_attribute_name(attribute);
            result.attributes.push_back(input_attribute);
        }
    }
    return result;
}

Vertex_input_state::Vertex_input_state(Device& device)
    : m_impl{std::make_unique<Vertex_input_state_impl>(device)}
{
}
Vertex_input_state::Vertex_input_state(Device& device, Vertex_input_state_data&& create_info)
    : m_impl{std::make_unique<Vertex_input_state_impl>(device, std::move(create_info))}
{
}
Vertex_input_state::~Vertex_input_state() noexcept = default;
void Vertex_input_state::set(const Vertex_input_state_data& data)
{
    m_impl->set(data);
}
auto Vertex_input_state::get_data() const -> const Vertex_input_state_data&
{
    return m_impl->get_data();
}

auto Vertex_input_state::get_impl() -> Vertex_input_state_impl&
{
    return *m_impl.get();
}
auto Vertex_input_state::get_impl() const -> const Vertex_input_state_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
