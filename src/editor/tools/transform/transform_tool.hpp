#pragma once

#include "editor_message.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/handle_visualizations.hpp"
#include "tools/transform/rotation_inspector.hpp"
#include "tools/tool.hpp"

#include "windows/property_editor.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"
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

namespace tf {
    class Executor;
}

namespace editor {

class Compound_operation;
class Editor_message_bus;
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
    Transform_tool_drag_command(erhe::commands::Commands& commands, Editor_context& context);
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;

private:
    Editor_context& m_context;
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

    Transform_tool_settings                settings;
    std::vector<Transform_entry>           entries;
    glm::vec3                              initial_drag_position_in_world{0.0f};
    float                                  initial_drag_distance         {0.0f};
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

    bool                               m_multiselect;
    std::shared_ptr<erhe::scene::Node> m_first_node;
    glm::mat4                          m_world_from_parent;
    bool                               m_use_world_mode;
    erhe::scene::Trs_transform*        m_transform{nullptr};
    erhe::scene::Trs_transform*        m_rotation_transform{nullptr};

    glm::vec3                          m_scale;
    glm::quat                          m_rotation;
    glm::vec3                          m_translation;
    glm::vec3                          m_skew;

    erhe::imgui::Value_edit_state      m_translate_state;
    erhe::imgui::Value_edit_state      m_rotate_quaternion_state;
    erhe::imgui::Value_edit_state      m_rotate_euler_state;
    erhe::imgui::Value_edit_state      m_rotate_axis_angle_state;
    erhe::imgui::Value_edit_state      m_scale_state;
    erhe::imgui::Value_edit_state      m_skew_state;
};

class Transform_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    static constexpr int c_priority{1};

    Transform_tool(
        tf::Executor&                executor,
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus,
        Headset_view&                headset_view,
        Mesh_memory&                 mesh_memory,
        Tools&                       tools
    );

    // Implements Tool
    void tool_render(const Render_context& context) override;

    // Implements Imgui_window
    void imgui() override;

    // Public API
    void viewport_toolbar(bool& hovered);

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
    void on_message     (Editor_message& message);
    void update_for_view(Scene_view* scene_view);
    void update_hover   ();
    void render_rays    (erhe::scene::Node& node);

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

} // namespace editor
