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
// active. The selection can span multiple meshes (one Group per live selection
// entry); the gizmo anchors to the combined centroid of all selected components and
// applies its world-from-anchor delta to each group's geometry vertices through that
// group's own node transform.
//
// Two-tier update:
//   - update_anchor(): each idle frame, recompute the affected vertices + combined
//     centroid and place the gizmo anchor (sets Transform_tool_shared::component_mode).
//   - begin()/apply()/commit(): during a drag or numeric edit, snapshot each group's
//     vertices once (begin), poke moved positions into the geometry + GPU vertex buffer
//     each update (apply, like Paint_tool - visual feedback only), and on release queue a
//     Move_mesh_vertices_operation per group (wrapped in a Compound_operation when more
//     than one) that does the authoritative rebuild and provides undo/redo (commit).
class Mesh_component_transform
{
public:
    auto update_anchor(App_context& context, Transform_tool_shared& shared) -> bool;
    void begin        (App_context& context);
    void apply        (App_context& context, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor);
    void commit       (App_context& context);

    [[nodiscard]] auto is_active() const -> bool { return m_active; }

private:
    // One selected mesh+primitive's affected vertices and the transforms needed to
    // move them. The geometry is held by shared_ptr for the duration of the drag so
    // it stays the same object (keeping the selection entry live).
    class Group
    {
    public:
        std::weak_ptr<erhe::scene::Mesh>          mesh;
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry;
        glm::mat4                                 world_from_node{1.0f};
        glm::mat4                                 node_from_world{1.0f};
        std::vector<GEO::index_t>                 vertices;     // unique affected
        std::vector<glm::vec3>                    before_local; // captured at begin(), parallel to vertices
    };

    // Resolve the live component-selection entries into editable groups (one per
    // single-geometry, in-scene mesh). Returns false when no editable target exists.
    auto gather(App_context& context) -> bool;

    void enqueue_gpu_position(App_context& context, const Group& group, GEO::index_t vertex, const glm::vec3& local_position);

    std::vector<Group> m_groups;        // persistent scratch, cleared per gather()
    bool               m_active{false};
};

}
