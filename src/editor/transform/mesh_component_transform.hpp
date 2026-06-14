#pragma once

#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;
class Transform_tool_shared;

// Drives the transform gizmo when a mesh component selection (vertex/edge/face) is
// active: it anchors the gizmo to the centroid of the selected components and applies
// the gizmo's world-from-anchor delta to the underlying geometry vertices.
//
// Two-tier update:
//   - update_anchor(): each idle frame, recompute the affected vertices + centroid and
//     place the gizmo anchor (sets Transform_tool_shared::component_mode).
//   - begin()/apply()/commit(): during a drag or numeric edit, snapshot the affected
//     vertices' positions once (begin), poke moved positions into the geometry + GPU
//     vertex buffer each update (apply, like Paint_tool - visual feedback only), and on
//     release queue a Move_mesh_vertices_operation that does the authoritative rebuild
//     and provides undo/redo (commit).
class Mesh_component_transform
{
public:
    // Recompute the affected vertices + world centroid for the current component
    // selection and write the anchor into shared. Returns true (and sets
    // shared.component_mode = true) when a valid, non-empty, editable component
    // selection exists; otherwise clears component_mode and returns false. Called
    // each idle frame.
    auto update_anchor(App_context& context, Transform_tool_shared& shared) -> bool;

    // Snapshot the target geometry, node transforms, affected vertices and their
    // before-positions at the start of a drag / numeric edit.
    void begin(App_context& context);

    // Apply an updated world-from-anchor transform to the captured vertices: move the
    // geometry positions and poke them into the GPU vertex buffer for live feedback.
    void apply(App_context& context, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor);

    // Queue the undo-able Move_mesh_vertices_operation for the accumulated edit.
    void commit(App_context& context);

    [[nodiscard]] auto is_active() const -> bool { return m_active; }

private:
    // Resolve the active component selection into (mesh, primitive_index, geometry) and
    // fill m_vertices with the unique affected geometry vertices. Returns false when
    // there is no valid, editable (single-geometry) target.
    auto gather(App_context& context) -> bool;

    void enqueue_gpu_position(App_context& context, GEO::index_t vertex, const glm::vec3& local_position);

    std::weak_ptr<erhe::scene::Mesh>          m_mesh;
    std::size_t                               m_primitive_index{0};
    std::shared_ptr<erhe::geometry::Geometry> m_geometry;
    glm::mat4                                 m_world_from_node{1.0f};
    glm::mat4                                 m_node_from_world{1.0f};
    std::vector<GEO::index_t>                 m_vertices;     // unique affected, persistent scratch
    std::vector<glm::vec3>                    m_before_local; // captured at begin(), parallel to m_vertices
    bool                                      m_active{false};
};

}
