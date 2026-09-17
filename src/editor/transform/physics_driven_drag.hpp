#pragma once

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace erhe {
    class Item_host;
}
namespace erhe::scene {
    class Xformable; using Node = Xformable;
}

namespace editor {

class App_context;
class Node_physics;
class Physics_drag_constraint;
class Transform_entry;
struct Removed_items;

// Which gizmo drag a node drag is.
enum class Transform_drag_kind : unsigned int {
    translate = 0,
    rotate    = 1,
    scale     = 2
};

// The physics side of a Transform tool node drag.
//
// A dragged node whose rigid body is dynamic in a scene whose simulation runs
// is handled here at drag start:
// - translate / rotate of a body held by a live Node_joint: the body stays
//   dynamic and is pulled by a spring (Physics_drag_constraint,
//   make_jointed_body_drag_settings()) toward the center of mass of the pose
//   the gizmo asks for, projected onto the positions the body's joint lets
//   the center of mass reach; the simulation (joints, gravity, contacts)
//   decides where it goes and writes the node transform as usual. Release
//   keeps the body's velocity.
// - any other drag of a dynamic body (a scale drag, or a body without a live
//   joint): the body is kinematic for the duration of the drag and follows
//   the node transform the tool writes.
// Every other node (no body, a non-dynamic body, a scene whose simulation is
// not running) is left to the tool's direct node writes.
class Physics_driven_drag
{
public:
    Physics_driven_drag();
    ~Physics_driven_drag() noexcept;
    Physics_driven_drag(const Physics_driven_drag&) = delete;
    auto operator=(const Physics_driven_drag&) -> Physics_driven_drag& = delete;

    // Re-captures the drag-start pose of each entry it takes over (a dynamic
    // body may have moved since the entries were built).
    void begin(App_context& context, std::vector<Transform_entry>& entries, Transform_drag_kind kind);

    // Called with the pose the gizmo asks for; returns true when the node is
    // pulled by a spring, so the caller must not write the node transform.
    auto drive(const erhe::scene::Node* node, const glm::mat4& world_from_node) -> bool;

    // True when the node is pulled by a spring in the current drag.
    [[nodiscard]] auto is_spring_driven(const erhe::scene::Node* node) const -> bool;
    [[nodiscard]] auto is_active       () const -> bool;

    // Detaches every spring and hands kinematic bodies back to their own
    // motion mode.
    void end();

    // Scene close / content removal during a drag: release what belongs to it.
    void on_close_scene  (const erhe::Item_host* closing_host);
    void on_items_removed(const Removed_items& removed);

private:
    class Driven_node
    {
    public:
        Driven_node();
        Driven_node(Driven_node&&) noexcept;
        auto operator=(Driven_node&&) noexcept -> Driven_node&;
        ~Driven_node() noexcept;

        std::shared_ptr<Node_physics>            node_physics;
        const erhe::scene::Node*                 node{nullptr};
        const erhe::Item_host*                   item_host{nullptr};
        std::unique_ptr<Physics_drag_constraint> spring;             // nullptr: kinematic for the drag
        glm::vec3                                center_of_mass_in_node{0.0f};
    };

    static void release(Driven_node& driven_node);

    std::vector<Driven_node> m_driven_nodes;
};

}
