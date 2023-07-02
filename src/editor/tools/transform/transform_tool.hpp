#pragma once

#include "editor_message.hpp"
#include "operations/node_operation.hpp"
#include "tools/transform/move_tool.hpp"
#include "tools/transform/rotate_tool.hpp"
#include "tools/transform/scale_tool.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/handle_visualizations.hpp"
#include "tools/transform/rotation_inspector.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"

#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/physics/imotion_state.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>

#include <array>
#include <map>
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
}

namespace editor
{

class Compound_operation;
class Headset_view;
class Node_physics;
class Scene_root;
class Subtool;
class Tools;
class Transform_tool;
class Viewport_window;

class Transform_tool_drag_command
    : public erhe::commands::Command
{
public:
    Transform_tool_drag_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );
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

//class Debug_rendering
//{
//public:
//    glm::vec3 m_P0{};
//    glm::vec3 m_P1{};
//    glm::vec3 m_Q0{};
//    glm::vec3 m_Q1{};
//    glm::vec3 m_R0{};
//    glm::vec3 m_R1{};
//    glm::vec2 m_ss_closest{};
//    uint32_t  m_debug_color{0x00000000u};
//    float     m_v_dot_n{0.0f};
//    glm::vec3 m_pw{};
//    glm::vec3 m_q0{};
//    glm::vec3 m_q{};
//};

class Transform_tool_shared
{
public:
    Transform_tool_settings              settings;
    std::vector<Transform_entry>         entries;
    glm::vec3                            initial_drag_position_in_world{0.0f};
    float                                initial_drag_distance         {0.0f};
    erhe::scene::Trs_transform           world_from_anchor_initial_state;
    erhe::scene::Trs_transform           world_from_anchor;
    bool                                 touched          {false};
    //Debug_rendering                      debug_rendering;
    std::optional<Handle_visualizations> visualization;
};

enum class Reference_mode : unsigned int
{
    local = 0,
    parent,
    world,
};

static constexpr std::array<std::string_view, 3> c_reference_mode_strings =
{
    "Local",
    "Parent",
    "World"
};

class Transform_tool
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    static constexpr int c_priority{1};

    Transform_tool(
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
    void on_message          (Editor_message& message);
    void acquire_node_physics();
    void release_node_physics();
    void update_entry        ();
    void update_for_view     (Scene_view* scene_view);
    void update_hover        ();
    void render_rays         (erhe::scene::Node& node);

    Transform_tool_drag_command         m_drag_command;
    erhe::commands::Redirect_command    m_drag_redirect_update_command;
    erhe::commands::Drag_enable_command m_drag_enable_command;

public:

private:
    Handle                             m_hover_handle {Handle::e_handle_none};
    Handle                             m_active_handle{Handle::e_handle_none};
    std::shared_ptr<erhe::scene::Node> m_tool_node;
    Subtool*                           m_hover_tool   {nullptr};
    Subtool*                           m_active_tool  {nullptr};
    Rotation_inspector                 m_rotation;
};

} // namespace editor
