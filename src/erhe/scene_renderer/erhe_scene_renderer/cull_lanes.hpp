#pragma once

#include "erhe_scene_renderer/draw_list.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>

// Library-internal: the culling classification lanes of
// Draw_list_scene::classify_cull_views() (doc/erhe/draw_list_renderer.md Q6),
// in their own translation unit so the deviceless tests can call them. Each
// is one branch-free loop over raw arrays (the entries' SoA bounds and the
// per-entry flags), the shape the compiler vectorizes without intrinsics.
namespace erhe::scene_renderer::cull_lanes {

// The per-axis gaps from the range sphere's center to the boxes, which the
// cube face lanes read after the sphere test.
class Sphere_gaps
{
public:
    float* x{nullptr};
    float* y{nullptr};
    float* z{nullptr};
};

void mark_unbounded_lanes(const Draw_list_bounds& bounds, std::size_t count, uint32_t* unbounded);
void sphere_axis_lanes   (const float* center, const float* extent, float sphere_center, std::size_t count, float* gap, float* distance2);
void sphere_lanes        (const Cull_sphere& sphere, const Draw_list_bounds& bounds, std::size_t count, const Sphere_gaps& gaps, float* distance2, uint32_t* inside);
void plane_lanes         (const glm::vec4& plane, const Draw_list_bounds& bounds, std::size_t count, uint32_t* inside);
void cube_face_lanes(
    const float*    center,
    const float*    extent,
    float           light,
    float           direction,
    const float*    gap_a,
    const float*    gap_b,
    const uint32_t* sphere_inside,
    std::size_t     count,
    uint32_t*       inside
);
[[nodiscard]] auto write_view_bit_lanes(const uint32_t* inside, const uint32_t* unbounded, std::size_t count, uint64_t* masks, uint32_t bit_index) -> uint32_t;
void set_view_bit_lanes(uint64_t* masks, std::size_t count, uint64_t bit);

} // namespace erhe::scene_renderer::cull_lanes
