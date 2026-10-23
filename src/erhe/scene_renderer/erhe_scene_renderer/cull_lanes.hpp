#pragma once

#include "erhe_scene_renderer/draw_list.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

// The culling classification lanes of Draw_list_scene::classify_cull_views()
// (doc/erhe/draw_list_renderer.md Q6), in their own translation unit so the
// deviceless tests can call them; reaches_view() runs them for one view over
// a set of boxes (Shadow_map_cache's per-pass test). Public API of
// erhe::scene_renderer. Each lane is one branch-free loop over raw arrays
// (the entries' SoA bounds and the per-entry flags), the shape the compiler
// vectorizes without intrinsics.
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

// The lanes' per-box arrays of reaches_view(), owned by the caller (capacity
// kept across calls).
class View_reach_scratch
{
public:
    std::vector<uint32_t> unbounded;
    std::vector<uint32_t> inside;
    std::vector<uint32_t> sphere_inside;
    std::vector<float>    distance2;
    std::vector<float>    gap_x;
    std::vector<float>    gap_y;
    std::vector<float>    gap_z;
};

// Whether any box of bounds is kept by view: the tests and lanes
// Draw_list_scene::classify_cull_views() classifies an entry with, so a box
// this rejects is culled from the view. A box without bounds is kept by
// every view, and a planes view without planes keeps every box.
[[nodiscard]] auto reaches_view(const Cull_view_tests& view, const Draw_list_bounds& bounds, View_reach_scratch& scratch) -> bool;

} // namespace erhe::scene_renderer::cull_lanes
