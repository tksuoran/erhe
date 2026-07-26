#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace erhe::scene { class Node; }

namespace editor {

class App_context;
class Lattice_node;
class Transform_tool_shared;

// Drives the transform gizmo when a display / ghost designated Lattice_node is
// bound into the active scene (Lattice_tool finds the target and owns the
// control point selection). The gizmo anchors to the selected control point's
// deformed position; the drag's world-from-anchor delta is mapped back into
// the bound node's local space and written into the lattice node's offset for
// that point (mark_dirty -> background re-evaluation gives live feedback).
//
// Two-tier update mirroring Mesh_component_transform:
//   - update_anchor(): each idle frame, re-resolve the target and place the
//     gizmo anchor (sets Transform_tool_shared::component_mode).
//   - begin()/apply()/commit(): snapshot the node's parameter JSON and the
//     point's offset at drag start, write the moved offset each update, and on
//     release push one Geometry_graph_parameter_operation for the whole
//     gesture (the values are already live; the operation's first execute is
//     a record-only step).
class Lattice_point_transform
{
public:
    auto update_anchor(App_context& context, Transform_tool_shared& shared) -> bool;
    void begin        (App_context& context);
    void apply        (App_context& context, Transform_tool_shared& shared, const glm::mat4& updated_world_from_anchor);
    void commit       (App_context& context);

    [[nodiscard]] auto is_active() const -> bool { return m_active; }

private:
    std::weak_ptr<Lattice_node>      m_lattice;
    std::weak_ptr<erhe::scene::Node> m_bound_node;
    glm::ivec3                       m_point           {0, 0, 0};
    glm::vec3                        m_rest_local      {0.0f}; // rest grid position, pre-driver-transform lattice space
    glm::vec3                        m_before_offset   {0.0f}; // captured at begin()
    glm::mat4                        m_world_from_node {1.0f};
    glm::mat4                        m_node_from_world {1.0f};
    glm::mat4                        m_driver_transform{1.0f}; // captured at begin(): the driver node's parent transform
    glm::mat4                        m_driver_inverse  {1.0f};
    std::string                      m_before_parameters;
    bool                             m_active{false};
};

}
