#pragma once

#include "erhe_math/aabb.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace erhe::math {

// Persistent buffers of calculate_bounding_convex_hull_of_boxes(). Every
// buffer is cleared (capacity kept) at point of use, so steady-state calls
// perform no heap allocations.
class Box_hull_scratch
{
public:
    // Light plane footprint of every box: the projected center and the images
    // of the three half axes (structure of arrays, one entry per box).
    std::vector<float>     center_x;
    std::vector<float>     center_y;
    std::vector<float>     axis_x_x;
    std::vector<float>     axis_x_y;
    std::vector<float>     axis_y_x;
    std::vector<float>     axis_y_y;
    std::vector<float>     axis_z_x;
    std::vector<float>     axis_z_y;
    // Inner polygon edges: a point p is inside edge i when
    // normal_x[i] * p.x + normal_y[i] * p.y >= offset[i].
    std::vector<float>     edge_normal_x;
    std::vector<float>     edge_normal_y;
    std::vector<float>     edge_offset;
    std::vector<uint8_t>   inside;           // per box: proven inside the inner polygon
    std::vector<glm::vec2> inner_points;     // real set points the inner polygon is built from
    std::vector<glm::vec2> inner_hull;       // hull of inner_points
    std::vector<glm::vec2> projected_points; // points left for the final hull
};

// Counter-clockwise 2D convex hull (no repeated wrap vertex, strictly convex)
// of the projected corners of boxes plus the projected extra points. A point p
// projects to glm::vec2{light_from_world * p}.
//
// The result is bit-identical to calculate_bounding_convex_hull() over all
// 8 * boxes.size() + points.size() projected points. The boxes are not sorted
// as 8 points each: a polygon is built from the extreme corners in 16 fixed
// directions, and every box proven to lie strictly inside it (a center and
// extents test per polygon edge, with a safety margin far above float
// rounding) cannot contribute a hull vertex and is dropped before the sort;
// of the remaining boxes, corner points inside the polygon are dropped too.
// Small box counts skip the pruning and take the plain path.
void calculate_bounding_convex_hull_of_boxes(
    std::span<const Aabb>      boxes,
    std::span<const glm::vec3> points,
    const glm::mat3&           light_from_world,
    Box_hull_scratch&          scratch,
    std::vector<glm::vec2>&    out_hull
);

} // namespace erhe::math
