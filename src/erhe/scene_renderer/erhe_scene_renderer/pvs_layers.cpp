#include "erhe_scene_renderer/pvs_layers.hpp"
#include "erhe_scene_renderer/buffer_binding_points.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>

namespace erhe::scene_renderer {

auto pvs_layer_of_depth(const float z, const float z_near, const float z_far, const uint32_t layer_count) -> uint32_t
{
    // erhe_pvs_layer_of_depth() (res/shaders/erhe_pvs_layer.glsl), operation
    // for operation, in float.
    const float t     = std::log(z / z_near) / std::log(z_far / z_near);
    const float layer = std::floor(static_cast<float>(layer_count) * t);
    return static_cast<uint32_t>(std::clamp(layer, 0.0f, static_cast<float>(layer_count - 1u)));
}

auto pvs_layer_front_depth(const uint32_t k, const float z_near, const float z_far, const uint32_t layer_count) -> float
{
    ERHE_VERIFY((z_near > 0.0f) && (z_near < z_far) && std::isfinite(z_far));
    ERHE_VERIFY((layer_count >= 1) && (k <= layer_count));
    if (k == 0) {
        return z_near;
    }
    if (k == layer_count) {
        return z_far;
    }
    // z_near * (z_far / z_near)^(k / layer_count), then moved by ulps to the
    // smallest float pvs_layer_of_depth() puts in layer k: the formula's
    // rounding may land a few ulps on either side of the boundary.
    const double exact = static_cast<double>(z_near) * std::pow(static_cast<double>(z_far) / static_cast<double>(z_near), static_cast<double>(k) / static_cast<double>(layer_count));
    float depth = static_cast<float>(exact);
    while ((depth > z_near) && (pvs_layer_of_depth(depth, z_near, z_far, layer_count) >= k)) {
        depth = std::nextafter(depth, 0.0f);
    }
    while (pvs_layer_of_depth(depth, z_near, z_far, layer_count) < k) {
        depth = std::nextafter(depth, z_far);
    }
    return depth;
}

Pvs_layered_interface::Pvs_layered_interface(erhe::graphics::Device& graphics_device)
    : pvs_layered_block{
        graphics_device,
        erhe::graphics::Shader_resource::Block_create_info{
            .name          = "pvs_layered",
            .binding_point = static_cast<int>(pvs_layered_buffer_binding_point),
            .type          = erhe::graphics::Shader_resource::Type::shader_storage_block
        }
    }
{
    // std430 (storage blocks): uvec4 at 0, uvec4 at 16, the uint array at 32.
    m_info_offset        = pvs_layered_block.add_uvec4("info"       )->get_offset_in_parent();
    m_trace_offset       = pvs_layered_block.add_uvec4("trace"      )->get_offset_in_parent();
    m_words_offset       = pvs_layered_block.add_uint ("words", erhe::graphics::Shader_resource::unsized_array)->get_offset_in_parent();
    ERHE_VERIFY((m_info_offset == 0) && (m_trace_offset == 16) && (m_words_offset == 32));
}

} // namespace erhe::scene_renderer
