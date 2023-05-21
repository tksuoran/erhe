#pragma once

#include "editor_message.hpp"
#include "operations/node_operation.hpp"
#include "tools/transform/handle_enums.hpp"
#include "tools/transform/handle_visualizations.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tool.hpp"

#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/message_bus/message_bus.hpp"
#include "erhe/physics/imotion_state.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

namespace erhe::physics
{
    enum class Motion_mode : unsigned int;
}

namespace erhe::scene
{
    class Mesh;
    class Node;
}

namespace editor
{

class Compound_operation;
class Node_physics;
class Scene_root;
class Viewport_window;

class Subtool;

class Transform_tool_drag_command
    : public erhe::application::Command
{
public:
    Transform_tool_drag_command();
    void try_ready  () override;
    auto try_call   () -> bool override;
    void on_inactive() override;
};

class Transform_tool
    : public erhe::application::Imgui_window
    , public erhe::components::Component
    , public Tool
{
public:
    class Drag
    {
    public:
        glm::mat4 initial_world_from_anchor{1.0f};
        glm::mat4 initial_anchor_from_world{1.0f};
        glm::vec3 initial_position_in_world{0.0f, 0.0f, 0.0f};
        float     initial_distance         {0.0f};
    };

    class Anchor_state
    {
    public:
        glm::vec3 pivot_point_in_world{0.0f, 0.0f, 0.0f};
        glm::mat4 anchor_translation  {1.0f};
        glm::quat anchor_rotation     {1.0f, 0.0f, 0.0f, 0.0f};
        glm::mat4 world_from_anchor   {1.0f};
    };

    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Node>        node;
        erhe::scene::Transform                    parent_from_node_before;
        std::optional<erhe::physics::Motion_mode> original_motion_mode;
        erhe::physics::Motion_mode                motion_mode{erhe::physics::Motion_mode::e_invalid};
    };

    class Debug_rendering
    {
    public:
        glm::vec3 m_P0{};
        glm::vec3 m_P1{};
        glm::vec3 m_Q0{};
        glm::vec3 m_Q1{};
        glm::vec3 m_R0{};
        glm::vec3 m_R1{};
        glm::vec2 m_ss_closest{};
        uint32_t  m_debug_color{0x00000000u};
        float     m_v_dot_n{0.0f};
        glm::vec3 m_pw{};
        glm::vec3 m_q0{};
        glm::vec3 m_q{};
    };

    class Shared
    {
    public:
        Transform_tool_settings              settings;
        std::vector<Entry>                   entries;
        Drag                                 drag;
        Anchor_state                         anchor_state_initial;
        glm::mat4                            world_from_anchor{1.0f};
        glm::mat4                            anchor_from_world{1.0f};
        bool                                 touched          {false};
        Debug_rendering                      debug_rendering;
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

    static constexpr int              c_priority {1};
    static constexpr std::string_view c_type_name{"Transform_tool"};
    static constexpr std::string_view c_title    {"Transform"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Transform_tool ();
    ~Transform_tool() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

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
    [[nodiscard]] auto get_tool_scene_root() -> std::shared_ptr<Scene_root>;

    void touch();
    void record_transform_operation();

    // Interface for Properties window
    void transform_properties();

    // Interface for Subtool usage
    void update_world_from_anchor_transform(const glm::mat4& updated_world_from_anchor);
    void acquire_node_physics              ();
    void release_node_physics              ();
    void update_visibility                 ();
    void update_transforms                 ();

    Shared shared;

private:
    void on_message         (Editor_message& message);
    void update_for_view    (Scene_view* scene_view);
    void update_hover       ();
    void update_target_nodes();

    Transform_tool_drag_command            m_drag_command;
    erhe::application::Redirect_command    m_drag_redirect_update_command;
    erhe::application::Drag_enable_command m_drag_enable_command;

    Handle                             m_hover_handle {Handle::e_handle_none};
    Handle                             m_active_handle{Handle::e_handle_none};
    std::shared_ptr<erhe::scene::Node> m_tool_node;
    Subtool*                           m_hover_tool   {nullptr};
    Subtool*                           m_active_tool  {nullptr};
};

extern Transform_tool* g_transform_tool;

} // namespace editor
