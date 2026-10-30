#pragma once

#include "erhe_graphics/shader_resource.hpp"

#include <cstddef>
#include <cstdint>

namespace erhe::graphics { class Device; }

namespace erhe::scene_renderer {

// The depth layers of the potentially visible set bake's layered buffer
// (doc/plans/pvs.md "Bake" 3): layer k of layer_count holds the linear view
// depths in [z_near * r^k, z_near * r^(k+1)), r = (z_far / z_near)^(1 /
// layer_count).
//
// pvs_layer_of_depth() is the C++ twin of erhe_pvs_layer_of_depth() in
// res/shaders/erhe_pvs_layer.glsl: the same float operations in the same
// order (z / z_near, its log, divided by the log of z_far / z_near, times
// layer_count, floor, clamp to [0, layer_count - 1]). The GPU's log is not
// correctly rounded, so a depth within a few ulps of a layer boundary may
// land in the neighbouring layer on the GPU; no consumer depends on the
// boundary bits (every depth of a layer is compared only with depths of the
// same layer of the same buffer).
//
// Preconditions (both functions): 0 < z_near < z_far, finite;
// layer_count >= 1.
[[nodiscard]] auto pvs_layer_of_depth(float z, float z_near, float z_far, uint32_t layer_count) -> uint32_t;

// The front depth of layer k (0 <= k <= layer_count): the smallest float
// whose pvs_layer_of_depth() is k, for k < layer_count; z_near for k = 0;
// z_far for k = layer_count (the back of the last layer). Strictly
// increasing in k.
[[nodiscard]] auto pvs_layer_front_depth(uint32_t k, float z_near, float z_far, uint32_t layer_count) -> float;

// The pvs_layered storage block of the standard shader's PVS variants
// (VARIANT_PVS_DEPTH, VARIANT_PVS_ID; res/shaders/standard.frag
// erhe_pvs_write()), one std430 storage block at
// pvs_layered_buffer_binding_point:
//
//     uvec4 info;        // face resolution, layer count, words per layer
//                        // (resolution^2), words per region (words per
//                        // layer * layer count)
//     vec4  depth_range; // z_near, z_far, 0, 0
//     uint  words[];     // three regions of words per region each: depth,
//                        // id_min, id_max; word (layer * words per layer +
//                        // y * resolution + x) of a region
//
// The three arrays are dense (doc/plans/pvs.md D7): face_resolution^2 *
// layer_count words each.
class Pvs_layered_interface
{
public:
    explicit Pvs_layered_interface(erhe::graphics::Device& graphics_device);

    // std430 byte offsets of the members (16, 16 and 4 byte aligned).
    [[nodiscard]] auto get_info_offset       () const -> std::size_t { return m_info_offset; }
    [[nodiscard]] auto get_depth_range_offset() const -> std::size_t { return m_depth_range_offset; }
    [[nodiscard]] auto get_words_offset      () const -> std::size_t { return m_words_offset; }

    erhe::graphics::Shader_resource pvs_layered_block;

private:
    std::size_t m_info_offset       {0};
    std::size_t m_depth_range_offset{0};
    std::size_t m_words_offset      {0};
};

} // namespace erhe::scene_renderer
