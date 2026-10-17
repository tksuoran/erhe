#include "erhe_math/box_hull.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_profile/profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace erhe::math {

namespace {

// Below this many boxes the pruning setup costs more than it saves.
constexpr std::size_t min_boxes_to_prune       = 64;
constexpr std::size_t max_inner_polygon_edges  = 32;
constexpr std::size_t max_inner_extra_points   = 256;
constexpr std::size_t lane_count               = 8;
// Margin by which a box must lie inside the inner polygon to be dropped,
// relative to the largest world coordinate of the boxes. Float rounding of the
// footprint arithmetic is about 1e-7 of that scale; the margin is 100 times
// larger, so a dropped box is strictly inside the polygon in the exact
// projection as well.
constexpr float prune_margin_relative          = 1e-5f;

// Unit directions at multiples of 22.5 degrees in the light plane.
constexpr std::array<glm::vec2, 16> directions{{
    { 1.0f,         0.0f        }, { 0.92387953f,  0.38268343f }, { 0.70710678f,  0.70710678f }, { 0.38268343f,  0.92387953f },
    { 0.0f,         1.0f        }, {-0.38268343f,  0.92387953f }, {-0.70710678f,  0.70710678f }, {-0.92387953f,  0.38268343f },
    {-1.0f,         0.0f        }, {-0.92387953f, -0.38268343f }, {-0.70710678f, -0.70710678f }, {-0.38268343f, -0.92387953f },
    { 0.0f,        -1.0f        }, { 0.38268343f, -0.92387953f }, { 0.70710678f, -0.70710678f }, { 0.92387953f, -0.38268343f }
}};

// Corner numbering of the fit: bit 2 selects max x, bit 1 max y, bit 0 max z.
[[nodiscard]] auto box_corner(const Aabb& box, const std::size_t corner_index) -> glm::vec3
{
    return glm::vec3{
        ((corner_index & 4u) != 0u) ? box.max.x : box.min.x,
        ((corner_index & 2u) != 0u) ? box.max.y : box.min.y,
        ((corner_index & 1u) != 0u) ? box.max.z : box.min.z
    };
}

[[nodiscard]] auto project(const glm::mat3& light_from_world, const glm::vec3& p) -> glm::vec2
{
    return glm::vec2{light_from_world * p};
}

// Footprints of all boxes; returns the largest world coordinate magnitude.
[[nodiscard]] auto build_footprints(std::span<const Aabb> boxes, const glm::mat3& light_from_world, Box_hull_scratch& s) -> float
{
    const std::size_t count = boxes.size();
    s.center_x.resize(count);
    s.center_y.resize(count);
    s.axis_x_x.resize(count);
    s.axis_x_y.resize(count);
    s.axis_y_x.resize(count);
    s.axis_y_y.resize(count);
    s.axis_z_x.resize(count);
    s.axis_z_y.resize(count);
    // Rows of light_from_world (glm matrices are column major).
    const glm::vec3 rx{light_from_world[0][0], light_from_world[1][0], light_from_world[2][0]};
    const glm::vec3 ry{light_from_world[0][1], light_from_world[1][1], light_from_world[2][1]};
    float scale = 1.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const Aabb&     box = boxes[i];
        const glm::vec3 c   = 0.5f * (box.min + box.max);
        const glm::vec3 e   = 0.5f * (box.max - box.min);
        s.center_x[i] = glm::dot(rx, c);
        s.center_y[i] = glm::dot(ry, c);
        s.axis_x_x[i] = e.x * rx.x;
        s.axis_x_y[i] = e.x * ry.x;
        s.axis_y_x[i] = e.y * rx.y;
        s.axis_y_y[i] = e.y * ry.y;
        s.axis_z_x[i] = e.z * rx.z;
        s.axis_z_y[i] = e.z * ry.z;
        const glm::vec3 magnitude = glm::max(glm::abs(box.min), glm::abs(box.max));
        scale = std::max(scale, std::max(magnitude.x, std::max(magnitude.y, magnitude.z)));
    }
    return scale;
}

// Index of the box whose support value in the direction is largest. Eight
// independent lanes keep the loop vectorizable.
[[nodiscard]] auto find_extreme_box(const Box_hull_scratch& s, const glm::vec2& u) -> std::size_t
{
    const std::size_t count = s.center_x.size();
    const float* cx  = s.center_x.data();
    const float* cy  = s.center_y.data();
    const float* axx = s.axis_x_x.data();
    const float* axy = s.axis_x_y.data();
    const float* ayx = s.axis_y_x.data();
    const float* ayy = s.axis_y_y.data();
    const float* azx = s.axis_z_x.data();
    const float* azy = s.axis_z_y.data();

    float    best      [lane_count];
    uint32_t best_index[lane_count];
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        best      [lane] = std::numeric_limits<float>::lowest();
        best_index[lane] = 0;
    }
    const std::size_t block_count = count / lane_count;
    for (std::size_t block = 0; block < block_count; ++block) {
        const std::size_t base = block * lane_count;
        for (std::size_t lane = 0; lane < lane_count; ++lane) {
            const std::size_t i = base + lane;
            const float support =
                (u.x * cx[i]) + (u.y * cy[i]) +
                std::abs((u.x * axx[i]) + (u.y * axy[i])) +
                std::abs((u.x * ayx[i]) + (u.y * ayy[i])) +
                std::abs((u.x * azx[i]) + (u.y * azy[i]));
            const bool better = support > best[lane];
            best      [lane] = better ? support : best[lane];
            best_index[lane] = better ? static_cast<uint32_t>(i) : best_index[lane];
        }
    }
    float    top       = std::numeric_limits<float>::lowest();
    uint32_t top_index = 0;
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        if (best[lane] > top) {
            top       = best[lane];
            top_index = best_index[lane];
        }
    }
    for (std::size_t i = block_count * lane_count; i < count; ++i) {
        const float support =
            (u.x * cx[i]) + (u.y * cy[i]) +
            std::abs((u.x * axx[i]) + (u.y * axy[i])) +
            std::abs((u.x * ayx[i]) + (u.y * ayy[i])) +
            std::abs((u.x * azx[i]) + (u.y * azy[i]));
        if (support > top) {
            top       = support;
            top_index = static_cast<uint32_t>(i);
        }
    }
    return top_index;
}

// Marks the boxes lying inside every polygon edge of the scratch.
void mark_boxes_inside(Box_hull_scratch& s)
{
    const std::size_t count      = s.center_x.size();
    const std::size_t edge_count = s.edge_offset.size();
    s.inside.assign(count, 1);
    uint8_t* const out = s.inside.data();
    const float* cx  = s.center_x.data();
    const float* cy  = s.center_y.data();
    const float* axx = s.axis_x_x.data();
    const float* axy = s.axis_x_y.data();
    const float* ayx = s.axis_y_x.data();
    const float* ayy = s.axis_y_y.data();
    const float* azx = s.axis_z_x.data();
    const float* azy = s.axis_z_y.data();
    for (std::size_t edge = 0; edge < edge_count; ++edge) {
        const float nx     = s.edge_normal_x[edge];
        const float ny     = s.edge_normal_y[edge];
        const float offset = s.edge_offset[edge];
        for (std::size_t i = 0; i < count; ++i) {
            const float support =
                std::abs((nx * axx[i]) + (ny * axy[i])) +
                std::abs((nx * ayx[i]) + (ny * ayy[i])) +
                std::abs((nx * azx[i]) + (ny * azy[i]));
            const bool edge_ok = (((nx * cx[i]) + (ny * cy[i])) - support) >= offset;
            out[i] = static_cast<uint8_t>(out[i] & (edge_ok ? 1u : 0u));
        }
    }
}

[[nodiscard]] auto point_in_polygon(const Box_hull_scratch& s, const glm::vec2& p) -> bool
{
    const std::size_t edge_count = s.edge_offset.size();
    for (std::size_t edge = 0; edge < edge_count; ++edge) {
        if (((s.edge_normal_x[edge] * p.x) + (s.edge_normal_y[edge] * p.y)) < s.edge_offset[edge]) {
            return false;
        }
    }
    return true;
}

// Builds the polygon edges from s.inner_hull (counter-clockwise), keeping at
// most max_inner_polygon_edges vertices. A subset of the hull vertices is
// still a convex polygon whose vertices are real set points.
void build_inner_polygon(Box_hull_scratch& s, const float margin)
{
    s.edge_normal_x.clear();
    s.edge_normal_y.clear();
    s.edge_offset.clear();
    const std::size_t hull_count = s.inner_hull.size();
    if (hull_count < 3) {
        return;
    }
    const std::size_t vertex_count = std::min(hull_count, max_inner_polygon_edges);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        const glm::vec2& a   = s.inner_hull[(i * hull_count) / vertex_count];
        const glm::vec2& b   = s.inner_hull[(((i + 1) % vertex_count) * hull_count) / vertex_count];
        const glm::vec2  d   = b - a;
        const float      len = glm::length(d);
        if (len < 1e-6f) {
            continue;
        }
        const glm::vec2 normal{-d.y / len, d.x / len}; // inward for a counter-clockwise polygon
        s.edge_normal_x.push_back(normal.x);
        s.edge_normal_y.push_back(normal.y);
        s.edge_offset  .push_back(glm::dot(normal, a) + margin);
    }
    if (s.edge_offset.size() < 3) {
        s.edge_normal_x.clear();
        s.edge_normal_y.clear();
        s.edge_offset.clear();
    }
}

} // anonymous namespace

void calculate_bounding_convex_hull_of_boxes(
    const std::span<const Aabb>      boxes,
    const std::span<const glm::vec3> points,
    const glm::mat3&                 light_from_world,
    Box_hull_scratch&                scratch,
    std::vector<glm::vec2>&          out_hull
)
{
    ERHE_PROFILE_FUNCTION();

    Box_hull_scratch& s = scratch;
    s.projected_points.clear();
    s.edge_normal_x.clear();
    s.edge_normal_y.clear();
    s.edge_offset.clear();

    const std::size_t box_count = boxes.size();
    bool pruning = false;
    if (box_count >= min_boxes_to_prune) {
        const float scale = build_footprints(boxes, light_from_world, s);

        // Inner polygon: the hull of real set points, the extreme corner in
        // each of the fixed directions plus a sample of the extra points.
        s.inner_points.clear();
        for (const glm::vec2& u : directions) {
            const std::size_t i = find_extreme_box(s, u);
            const Aabb& box = boxes[i];
            // The corner on the side the direction points to along each box axis.
            const bool max_x = ((u.x * s.axis_x_x[i]) + (u.y * s.axis_x_y[i])) >= 0.0f;
            const bool max_y = ((u.x * s.axis_y_x[i]) + (u.y * s.axis_y_y[i])) >= 0.0f;
            const bool max_z = ((u.x * s.axis_z_x[i]) + (u.y * s.axis_z_y[i])) >= 0.0f;
            const glm::vec3 corner{
                max_x ? box.max.x : box.min.x,
                max_y ? box.max.y : box.min.y,
                max_z ? box.max.z : box.min.z
            };
            s.inner_points.push_back(project(light_from_world, corner));
        }
        const std::size_t extra_stride = std::max<std::size_t>(1, (points.size() + max_inner_extra_points - 1) / max_inner_extra_points);
        for (std::size_t i = 0; i < points.size(); i += extra_stride) {
            s.inner_points.push_back(project(light_from_world, points[i]));
        }
        calculate_bounding_convex_hull(std::span<const glm::vec2>{s.inner_points}, s.inner_hull);
        build_inner_polygon(s, prune_margin_relative * scale);
        if (!s.edge_offset.empty()) {
            mark_boxes_inside(s);
            pruning = true;
        }
    }

    for (std::size_t i = 0; i < box_count; ++i) {
        if (pruning && (s.inside[i] != 0)) {
            continue;
        }
        for (std::size_t corner_index = 0; corner_index < 8; ++corner_index) {
            const glm::vec2 q = project(light_from_world, box_corner(boxes[i], corner_index));
            if (pruning && point_in_polygon(s, q)) {
                continue;
            }
            s.projected_points.push_back(q);
        }
    }
    for (const glm::vec3& p : points) {
        s.projected_points.push_back(project(light_from_world, p));
    }
    calculate_bounding_convex_hull(std::span<const glm::vec2>{s.projected_points}, out_hull);
}

} // namespace erhe::math
