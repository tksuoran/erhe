#pragma once

#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::scene    { class Node; }

namespace editor {

class App_message_bus;
class Lattice_node;
class Scene_view;
class Tools;
class Viewport_scene_view;

class Lattice_select_command : public erhe::commands::Command
{
public:
    Lattice_select_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready() override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

// Viewport editing of a Lattice_node's control points.
//
// Active when a Geometry_graph_mesh attachment in the active scene binds a
// Graph_mesh whose display or ghost designated node is a Lattice_node (the
// designation is what puts the lattice-deformed geometry in the viewport, so
// it doubles as the edit-mode switch). While active:
//   - tool_render draws the deformed cage wireframe and a billboard handle
//     per control point (selected / hovered highlighted);
//   - left click selects the control point under the pointer (screen-space
//     pick, consumed so the scene selection does not change);
//   - the transform gizmo anchors to the selected point and drags edit the
//     point's offset undoably (Lattice_point_transform).
// The control point selection is the node's own selected point, so the node
// UI's offset editor and the viewport stay in sync.
class Lattice_tool : public Tool
{
public:
    static constexpr int c_priority{4};

    Lattice_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Tools&                    tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // The lattice being edited and the scene node whose transform maps the
    // lattice's local space to world. Both empty when no designated lattice
    // is bound into the active scene.
    class Active_lattice
    {
    public:
        std::shared_ptr<Lattice_node>      lattice;
        std::shared_ptr<erhe::scene::Node> bound_node;
    };

    // Re-scans the active scene (unless a gizmo edit is in progress) and
    // returns the current target. Called by Transform_tool::update_for_view
    // each idle frame; tool_render reuses the result.
    auto update_active_lattice() -> const Active_lattice&;
    [[nodiscard]] auto get_active_lattice() const -> const Active_lattice&;

    // Commands
    [[nodiscard]] auto try_ready() -> bool;
    auto on_select() -> bool;

private:
    [[nodiscard]] auto pick_control_point(Scene_view& scene_view) const -> std::optional<glm::ivec3>;
    void append_point_quad(const glm::vec3& position_in_world, const glm::vec3& camera_right, const glm::vec3& camera_up, float half_size);

    erhe::message_bus::Subscription<Hover_scene_view_message> m_hover_scene_view_subscription;
    Lattice_select_command    m_select_command;
    Active_lattice            m_active;
    std::optional<glm::ivec3> m_pick_point; // armed by try_ready(), consumed by on_select()

    // Scratch for billboard quads (cleared per use, capacity kept)
    std::vector<glm::vec3> m_scratch_positions;
    std::vector<uint32_t>  m_scratch_indices;
};

}
