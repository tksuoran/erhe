#pragma once

#include "level/kit_shape.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace GEO { class Mesh; }

namespace editor {

class Kit_mesh_report
{
public:
    unsigned int             vertex_count   {0};
    unsigned int             facet_count    {0};
    unsigned int             triangle_count {0};
    unsigned int             component_count{0};
    glm::vec3                bounds_min     {0.0f};
    glm::vec3                bounds_max     {0.0f};
    std::vector<std::string> problems;

    [[nodiscard]] auto is_valid() const -> bool;
};

// R2.5: manifold, consistent winding, convex planar non-degenerate facets, no duplicates; a solid is closed with
// outward winding, a surface may have an open boundary.
[[nodiscard]] auto check_kit_mesh(const GEO::Mesh& mesh, Kit_mesh_kind mesh_kind = Kit_mesh_kind::solid) -> Kit_mesh_report;

// R2.4: the facets on the given side of the bounds fill that side's rectangle exactly. Empty when it holds.
[[nodiscard]] auto check_kit_seam_side(const GEO::Mesh& mesh, Kit_side side) -> std::string;

// R2.2: the base lies at y = 0 and sits at the origin by the given rule. Empty when it holds.
[[nodiscard]] auto check_kit_origin(const GEO::Mesh& mesh, Kit_origin_rule rule) -> std::string;

[[nodiscard]] auto count_kit_triangles(const GEO::Mesh& mesh) -> unsigned int;

// Bitwise equal positions and equal facets in equal order.
[[nodiscard]] auto kit_meshes_equal(const GEO::Mesh& lhs, const GEO::Mesh& rhs) -> bool;

// Generates the shape for the values and runs every check above that the definition states (R11.1).
[[nodiscard]] auto check_kit_shape(const Kit_parameter_values& values) -> Kit_mesh_report;

} // namespace editor
