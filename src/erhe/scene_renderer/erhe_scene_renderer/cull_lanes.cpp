#include "erhe_scene_renderer/cull_lanes.hpp"

#include <algorithm>
#include <cmath>

namespace erhe::scene_renderer::cull_lanes {

// The flags are written with a conditional expression of two constants:
// MSVC vectorizes that, but not a cast of a negated comparison.

// unbounded[i] = 1 for an entry without bounds (negative extent sentinel,
// Draw_list_bounds::get_unbounded_flag()).
void mark_unbounded_lanes(const Draw_list_bounds& bounds, const std::size_t count, uint32_t* const unbounded)
{
    const float* const ex = bounds.extent_x.data();
    for (std::size_t i = 0; i < count; ++i) {
        unbounded[i] = Draw_list_bounds::get_unbounded_flag(ex[i]);
    }
}

// One axis of the range sphere test: the gap along the axis from the
// sphere center coordinate to the box's interval (zero where the center
// coordinate lies within it) into gap[i], its square added to distance2[i].
// |c - s| - e is written as the larger of the two signed gaps (the same
// bits) so the loop holds only selects.
void sphere_axis_lanes(const float* const center, const float* const extent, const float sphere_center, const std::size_t count, float* const gap, float* const distance2)
{
    for (std::size_t i = 0; i < count; ++i) {
        const float offset = center[i] - sphere_center;
        const float below  = (-offset) - extent[i];
        const float above  = offset - extent[i];
        const float signed_gap = (below > above) ? below : above;
        const float d      = (signed_gap > 0.0f) ? signed_gap : 0.0f;
        gap[i]        = d;
        distance2[i] += d * d;
    }
}

// Range sphere: inside[i] = 0 where the squared distance from the sphere
// center to the box exceeds the squared radius (the box lies wholly outside
// the sphere), 1 otherwise. One loop per axis: MSVC does not vectorize the
// three axes in one loop.
void sphere_lanes(const Cull_sphere& sphere, const Draw_list_bounds& bounds, const std::size_t count, const Sphere_gaps& gaps, float* const distance2, uint32_t* const inside)
{
    std::fill(distance2, distance2 + count, 0.0f);
    sphere_axis_lanes(bounds.center_x.data(), bounds.extent_x.data(), sphere.center.x, count, gaps.x, distance2);
    sphere_axis_lanes(bounds.center_y.data(), bounds.extent_y.data(), sphere.center.y, count, gaps.y, distance2);
    sphere_axis_lanes(bounds.center_z.data(), bounds.extent_z.data(), sphere.center.z, count, gaps.z, distance2);
    const float r2 = sphere.radius * sphere.radius;
    for (std::size_t i = 0; i < count; ++i) {
        inside[i] = (distance2[i] > r2) ? 0u : 1u;
    }
}

// One plane: d = n . c + w + |n| . e (the expressions and the evaluation
// order of erhe::math::aabb_in_convex_volume()), inside[i] cleared where
// d < 0.
void plane_lanes(const glm::vec4& plane, const Draw_list_bounds& bounds, const std::size_t count, uint32_t* const inside)
{
    const float nx = plane.x;
    const float ny = plane.y;
    const float nz = plane.z;
    const float w  = plane.w;
    const float ax = std::abs(nx);
    const float ay = std::abs(ny);
    const float az = std::abs(nz);
    const float* const cx = bounds.center_x.data();
    const float* const cy = bounds.center_y.data();
    const float* const cz = bounds.center_z.data();
    const float* const ex = bounds.extent_x.data();
    const float* const ey = bounds.extent_y.data();
    const float* const ez = bounds.extent_z.data();
    for (std::size_t i = 0; i < count; ++i) {
        const float center_distance  = (((nx * cx[i]) + (ny * cy[i])) + (nz * cz[i])) + w;
        const float projected_extent = ((ax * ex[i]) + (ay * ey[i])) + (az * ez[i]);
        inside[i] &= ((center_distance + projected_extent) < 0.0f) ? 0u : 1u;
    }
}

// One point light cube face (doc/plans/culling.md phase 3): the face looking
// along +axis (direction 1) or -axis (direction -1) sees the octant where
// the coordinate along the axis relative to the light is at least the
// absolute coordinates along the other two axes. A box reaches into it
// where its farthest extent along the face direction, direction * (c - l)
// + e, is at least the larger of its distances from the light along the
// other two axes (the sphere test's gaps, gap_a and gap_b). That is exact
// for the octant, so a box it rejects covers no texel of the face.
// inside[i] = sphere_inside[i] narrowed by that test.
void cube_face_lanes(
    const float* const    center,
    const float* const    extent,
    const float           light,
    const float           direction,
    const float* const    gap_a,
    const float* const    gap_b,
    const uint32_t* const sphere_inside,
    const std::size_t     count,
    uint32_t* const       inside
)
{
    for (std::size_t i = 0; i < count; ++i) {
        const float reach   = (direction * (center[i] - light)) + extent[i];
        const float lateral = (gap_a[i] > gap_b[i]) ? gap_a[i] : gap_b[i];
        inside[i] = sphere_inside[i] & ((reach >= lateral) ? 1u : 0u);
    }
}

// The view's bit of every entry inside the view or without bounds; returns
// the number of those entries (the view's survivors in this list).
auto write_view_bit_lanes(const uint32_t* const inside, const uint32_t* const unbounded, const std::size_t count, uint64_t* const masks, const uint32_t bit_index) -> uint32_t
{
    uint32_t survivors = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const uint32_t kept = inside[i] | unbounded[i];
        masks[i] |= static_cast<uint64_t>(kept) << bit_index;
        survivors += kept;
    }
    return survivors;
}

void set_view_bit_lanes(uint64_t* const masks, const std::size_t count, const uint64_t bit)
{
    for (std::size_t i = 0; i < count; ++i) {
        masks[i] |= bit;
    }
}

} // namespace erhe::scene_renderer::cull_lanes
