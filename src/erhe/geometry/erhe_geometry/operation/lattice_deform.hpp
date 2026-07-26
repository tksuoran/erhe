#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

enum class Lattice_interpolation : int {
    trilinear = 0, // per-cell interpolation of the 8 surrounding control points; local, C0 across cells
    bezier    = 1  // Bernstein basis over the whole grid (classic FFD); global, smooth
};

// Free-form deformation (Sederberg & Parry 1986) through a regular control
// point lattice over the axis aligned box [cage_min, cage_max]. divisions is
// the cell count per axis; the lattice has (x+1)*(y+1)*(z+1) control points.
// control_point_offsets holds each control point's displacement from its rest
// grid position, x-fastest:
//     index = i + (divisions.x + 1) * (j + (divisions.y + 1) * k)
// Vertices outside the cage are clamped into it before basis evaluation, so
// they displace with the nearest cage face (continuous, no cracks).
//
// cage_transform places the cage in the geometry's space: the cage box and
// the offsets live in CAGE space, and each vertex is mapped into cage space
// (inverse transform) to find its lattice coordinates; its displacement is
// the interpolated offset rotated back by the transform's linear part. With
// zero offsets the deformation is identity for ANY cage_transform - moving
// the cage repositions the deformation region, it does not move geometry.
struct Lattice_deform_parameters {
    glm::vec3              cage_min{-1.0f};
    glm::vec3              cage_max{ 1.0f};
    glm::ivec3             divisions{2, 2, 2};
    glm::mat4              cage_transform{1.0f};
    Lattice_interpolation  interpolation{Lattice_interpolation::trilinear};
    std::vector<glm::vec3> control_point_offsets;

    // Topology is unchanged so source attributes stay valid, except normals
    // which a non-affine map invalidates: true recomputes smooth vertex
    // normals from the deformed positions, false keeps the source normals.
    bool                   regenerate_attributes{true};

    // Emit the deformed lattice wireframe into the destination's debug lines.
    bool                   make_cage_debug_lines{false};
};

[[nodiscard]] auto lattice_control_point_count(const glm::ivec3& divisions) -> std::size_t;
[[nodiscard]] auto lattice_offset_index(const glm::ivec3& divisions, int i, int j, int k) -> std::size_t;

void lattice_deform(const Geometry& source, Geometry& destination, const Lattice_deform_parameters& parameters);

} // namespace erhe::geometry::operation
