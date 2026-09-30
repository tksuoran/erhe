#pragma once

#include <geogram/basic/numeric.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace editor {

class Viewport_scene_view;

// Screen-space snapping to the vertices and edges of one facet
// (doc/plans/mesh_modeling.md D5, doc/editor/mesh_modeling.md "Screen
// snap"): the knife tool and the move transform mode's vertex / edge snap.

enum class Screen_snap_kind : unsigned int
{
    none   = 0,
    vertex = 1,
    edge   = 2
};

// Where an edge snap lands on the edge.
enum class Screen_snap_edge_point : unsigned int
{
    nearest  = 0, // the edge point under the pointer (the point of the edge nearest to the pointer ray)
    midpoint = 1  // the edge's midpoint
};

class Screen_snap_query
{
public:
    const Viewport_scene_view*      view             {nullptr};
    glm::mat4                       world_from_node  {1.0f};
    const erhe::geometry::Geometry* geometry         {nullptr};
    GEO::index_t                    facet            {GEO::NO_INDEX};
    glm::vec2                       cursor           {0.0f, 0.0f}; // viewport pixels (Viewport_scene_view::get_position_in_viewport())
    float                           vertex_radius_px {8.0f};
    float                           edge_radius_px   {10.0f};
    Screen_snap_edge_point          edge_point       {Screen_snap_edge_point::nearest};
    // Indexed by vertex; a non-zero entry excludes the vertex and the edges
    // at it (the vertices a move drags). Empty: nothing is excluded.
    std::span<const std::uint8_t>   excluded_vertices{};
};

class Screen_snap_result
{
public:
    Screen_snap_kind kind                {Screen_snap_kind::none};
    GEO::index_t     vertex              {GEO::NO_INDEX}; // vertex snap
    GEO::index_t     edge_v0             {GEO::NO_INDEX}; // edge snap
    GEO::index_t     edge_v1             {GEO::NO_INDEX};
    float            edge_t              {0.0f};          // edge snap: parameter from edge_v0 to edge_v1
    glm::vec3        position_in_mesh    {0.0f};
    glm::vec3        position_in_world   {0.0f};
    glm::vec2        position_in_viewport{0.0f, 0.0f};
    float            distance_px         {0.0f};          // from the cursor to the snapped vertex / edge
};

// Projects the facet's corners with Viewport_scene_view::project_to_viewport()
// and reports the nearest vertex within vertex_radius_px, else the nearest
// edge within edge_radius_px, else none. The projection scratch is kept on
// the object (cleared at use, capacity kept), so a call allocates nothing
// after warm-up.
class Screen_snap
{
public:
    // The pixel radius shrunk by candidate density (Blender's knife rule):
    // with n cut vertices within twice the base radius of the cursor, the
    // radius is base / (n / 2), never more than base.
    [[nodiscard]] static auto get_snap_radius(float base_radius_px, std::size_t nearby_vertex_count) -> float;

    // False (out_result.kind none) when nothing lies within the radii or the
    // query is incomplete.
    auto snap(const Screen_snap_query& query, Screen_snap_result& out_result) -> bool;

private:
    std::vector<glm::vec2>    m_projected;       // per facet corner
    std::vector<std::uint8_t> m_projected_valid; // per facet corner
};

} // namespace editor
