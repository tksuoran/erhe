#pragma once

#include "app_message.hpp"
#include "tools/point_set_tool.hpp"

#include "erhe_message_bus/message_bus.hpp"
#include "erhe_renderer/primitive_renderer.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace erhe::commands { class Commands; }
namespace erhe::scene    { class Xformable; using Node = Xformable; }

namespace editor {

class App_message_bus;
class Lattice_node;
class Scene_root;
class Tools;

// Viewport editing of a Lattice_node's control points
// (doc/editor/lattice_deform_geometry_node.md "Viewport editing"): active when
// a display or ghost designated Lattice_node is bound into the active scene.
// The point set is the control points; the selection is the node's own
// selected point, so the node UI's offset editor and the viewport stay in sync.
class Lattice_tool : public Point_set_tool
{
public:
    Lattice_tool(
        erhe::commands::Commands& commands,
        App_context&              context,
        App_message_bus&          app_message_bus,
        Tools&                    tools
    );

    // The lattice being edited and the scene node whose transform maps the
    // lattice's local space to world. Both empty when no designated lattice
    // is bound into the active scene.
    class Active_lattice
    {
    public:
        std::shared_ptr<Lattice_node>      lattice;
        std::shared_ptr<erhe::scene::Node> bound_node;
    };

    // Returns the current target, re-scanning the active scene first when a
    // change it depends on was reported (never during a gizmo edit).
    auto update_active_lattice() -> const Active_lattice&;
    [[nodiscard]] auto get_active_lattice() const -> const Active_lattice&;
    void on_items_removed(const Removed_items& removed);

    // Implements IPoint_set
    auto update_point_set() -> bool override;
    [[nodiscard]] auto has_point_set_target    () const -> bool override;
    [[nodiscard]] auto get_point_set_scene_root() const -> std::shared_ptr<Scene_root> override;
    void get_point_world_positions(std::vector<std::optional<glm::vec3>>& out_positions) const override;
    [[nodiscard]] auto is_point_selected       (std::size_t index) const -> bool override;
    [[nodiscard]] auto has_point_selection     () const -> bool override;
    void select_point                          (std::size_t index, Point_select select) override;
    auto clear_point_selection                 () -> bool override;
    [[nodiscard]] auto get_point_frame_rotation() const -> glm::quat override;
    [[nodiscard]] auto get_point_drawing       () const -> Point_drawing override;
    auto begin_point_edit () -> bool override;
    void apply_point_edit (const std::optional<glm::mat4>& world_delta) override;
    void commit_point_edit() override;
    void cancel_point_edit() override;

protected:
    void render_extras(const Render_context& context) override;

private:
    [[nodiscard]] auto point_from_index(std::size_t index) const -> glm::ivec3;
    [[nodiscard]] auto get_point_count () const -> std::size_t;

    erhe::message_bus::Subscription<Graph_mesh_binding_changed_message> m_graph_mesh_binding_changed_subscription;
    erhe::message_bus::Subscription<Close_scene_message>                m_close_scene_subscription;
    erhe::message_bus::Subscription<Items_removed_message>              m_items_removed_subscription;
    Active_lattice            m_active;
    bool                      m_active_stale{true};
    std::weak_ptr<Scene_root> m_active_scene_root; // the scene m_active was scanned from

    // Cage wireframe lines of render_extras (cleared per use, capacity kept)
    std::vector<erhe::renderer::Line> m_scratch_lines;

    // The point edit gesture (begin_point_edit .. commit / cancel)
    class Edit
    {
    public:
        std::weak_ptr<Lattice_node> lattice;
        glm::ivec3                  point           {0, 0, 0};
        glm::vec3                   rest_local      {0.0f}; // rest grid position, pre-driver-transform lattice space
        glm::vec3                   before_offset   {0.0f};
        glm::mat4                   world_from_node {1.0f};
        glm::mat4                   node_from_world {1.0f};
        glm::mat4                   driver_transform{1.0f}; // the driver node's parent transform
        glm::mat4                   driver_inverse  {1.0f};
        std::string                 before_parameters;
        bool                        active{false};
    };
    Edit m_edit;
};

}
