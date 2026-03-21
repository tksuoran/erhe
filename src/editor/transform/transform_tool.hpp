#pragma once

#include "transform/handle_enums.hpp"
#include "transform/handle_visualizations.hpp"
#include "transform/rotation_inspector.hpp"
#include "tools/tool.hpp"

#include "windows/property_editor.hpp"

#include "erhe_commands/command.hpp"
#include "app_message.hpp"
#include "erhe_message_bus/message_bus.hpp"
#include "tools/tool_window.hpp"
#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_scene/node.hpp"

#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <string_view>

namespace erhe::imgui {
    class Value_edit_state;
}
namespace erhe::physics {
    enum class Motion_mode : unsigned int;
}
namespace erhe::scene {
    class Mesh;
    class Node;
    class Trs_transform;
}

struct Transform_tool_config;

namespace tf {
    class Executor;
}

namespace editor {

class Compound_operation;
class App_message_bus;
struct Hover_scene_view_message;
struct Hover_mesh_message;
struct Selection_message;
struct Animation_update_message;
struct Node_touched_message;
struct Render_scene_view_message;
class Headset_view;
class Node_physics;
class Scene_root;
class Subtool;
class Tools;
class Transform_tool;
class Viewport_scene_view;

class Transform_tool_drag_command : public erhe::commands::Command
{
public:
    Transform_tool_drag_command(erhe::commands::Commands& commands, App_context& context);
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;

private:
    App_context& m_context;
};

class Transform_entry
{
public:
    std::shared_ptr<erhe::scene::Node>        node;
    erhe::scene::Trs_transform                parent_from_node_before;
    erhe::scene::Trs_transform                world_from_node_before;
    std::optional<erhe::physics::Motion_mode> original_motion_mode;
    erhe::physics::Motion_mode                motion_mode{erhe::physics::Motion_mode::e_invalid};
};

class Transform_tool_shared
{
public:
    auto get_visualizations() const -> Handle_visualizations*
    {
        if (!visualizations_ready.load()) {
            return nullptr;
        }
        return visualizations.get();
    }

    [[nodiscard]] auto get_initial_drag_position_in_world() const -> glm::vec3
    {
        return world_from_anchor_initial_state.get_translation() + m_initial_drag_position_from_anchor_initial_state;
    }
    void set_initial_drag_position_in_world(glm::vec3 in_initial_drag_position_in_world)
    {
        m_initial_drag_position_from_anchor_initial_state = in_initial_drag_position_in_world - world_from_anchor_initial_state.get_translation();
    }

    Transform_tool_settings                settings;
    std::vector<Transform_entry>           entries;
    glm::vec3                              m_initial_drag_position_from_anchor_initial_state{0.0f};
    float                                  initial_drag_position_distance_to_camera{0.0f};
    erhe::scene::Trs_transform             world_from_anchor_initial_state;
    erhe::scene::Trs_transform             world_from_anchor;
    bool                                   touched             {false};
    std::atomic<bool>                      visualizations_ready{false};
    std::unique_ptr<Handle_visualizations> visualizations      {};
};

enum class Reference_mode : unsigned int {
    local = 0,
    parent,
    world,
};

static constexpr std::array<std::string_view, 3> c_reference_mode_strings = {
    "Local",
    "Parent",
    "World"
};

class Edit_state
{
public:
    Edit_state();
    Edit_state(
        Transform_tool_shared& shared,
        Transform_tool&        transform_tool,
        Rotation_inspector&    rotation_inspector,
        Property_editor&       property_editor
    );

    bool                               m_multiselect       {false};
    std::shared_ptr<erhe::scene::Node> m_first_node        {};
    glm::mat4                          m_world_from_parent {1.0f};
    bool                               m_use_world_mode    {false};
    erhe::scene::Trs_transform*        m_transform         {nullptr};
    erhe::scene::Trs_transform*        m_rotation_transform{nullptr};

    glm::vec3                          m_scale      {1.0f};
    glm::quat                          m_rotation   {1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3                          m_translation{0.0f};
    glm::vec3                          m_skew       {0.0f};

    erhe::imgui::Value_edit_state      m_translate_state{};
    erhe::imgui::Value_edit_state      m_rotate_quaternion_state;
    erhe::imgui::Value_edit_state      m_rotate_euler_state;
    erhe::imgui::Value_edit_state      m_rotate_axis_angle_state;
    erhe::imgui::Value_edit_state      m_scale_state;
    erhe::imgui::Value_edit_state      m_skew_state;
};

class Transform_tool : public Tool
{
public:
    static constexpr int c_priority{1};

    Transform_tool(
        const Transform_tool_config& transform_tool_config,
        tf::Executor&                executor,
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        App_context&                 app_context,
        App_message_bus&             app_message_bus,
        Headset_view&                headset_view,
        Mesh_memory&                 mesh_memory,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Public API
    void viewport_toolbar();

    [[nodiscard]] auto is_transform_tool_active() const -> bool;

    // Commands
    auto on_drag_ready() -> bool;
    auto on_drag      () -> bool;
    void end_drag     ();

    // For Handle_visualizations
    [[nodiscard]] auto get_active_handle  () const -> Handle;
    [[nodiscard]] auto get_hover_handle   () const -> Handle;
    [[nodiscard]] auto get_handle         (erhe::scene::Mesh* mesh) const -> Handle;

    void touch();
    void record_transform_operation();

    // Exposed for Node_transform_operation
    void update_target_nodes(erhe::scene::Node* node_filter);

    // Interface for Properties window
    void transform_properties();

    // Interface for Subtool usage
    void adjust            (const glm::mat4& updated_world_from_anchor);
    void adjust_translation(glm::vec3 translation);
    void adjust_rotation   (glm::vec3 center_of_rotation, glm::quat rotation);
    void adjust_scale      (glm::vec3 center_of_scale,    glm::vec3 scale);
    void update_visibility ();
    void update_transforms ();

    Transform_tool_shared shared;

private:
    void window_imgui       ();
    void on_hover_scene_view(Hover_scene_view_message& message);
    void on_hover_mesh      (Hover_mesh_message& message);
    void on_selection       (Selection_message& message);
    void on_animation_update(Animation_update_message& message);
    void on_node_touched    (Node_touched_message& message);
    void on_render_scene_view(Render_scene_view_message& message);
    void update_for_view    (Scene_view* scene_view);
    void update_hover       ();
    void render_rays    (erhe::scene::Node& node);

    Tool_window                         m_window;
    erhe::message_bus::Subscription<Hover_scene_view_message>  m_hover_scene_view_subscription;
    erhe::message_bus::Subscription<Hover_mesh_message>        m_hover_mesh_subscription;
    erhe::message_bus::Subscription<Selection_message>         m_selection_subscription;
    erhe::message_bus::Subscription<Animation_update_message>  m_animation_update_subscription;
    erhe::message_bus::Subscription<Node_touched_message>      m_node_touched_subscription;
    erhe::message_bus::Subscription<Render_scene_view_message> m_render_scene_view_subscription;
    Transform_tool_drag_command         m_drag_command;
    erhe::commands::Redirect_command    m_drag_redirect_update_command;
    erhe::commands::Drag_enable_command m_drag_enable_command;
    Handle                              m_hover_handle {Handle::e_handle_none};
    Handle                              m_active_handle{Handle::e_handle_none};
    std::shared_ptr<erhe::scene::Node>  m_tool_node;
    Subtool*                            m_hover_tool      {nullptr};
    Subtool*                            m_active_tool     {nullptr};
    Subtool*                            m_last_active_tool{nullptr};
    Rotation_inspector                  m_rotation;

    Property_editor                     m_property_editor;

    Edit_state m_edit_state;

};

}
