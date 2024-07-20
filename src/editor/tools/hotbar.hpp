#pragma once

#include "tools/tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_imgui/imgui_window.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::commands {
    class Commands;
}
namespace erhe::graphics {
    class Instance;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
    class Node;
}

namespace editor {

class Editor_context;
class Editor_message_bus;
class Hotbar;
class Icon_set;
class Mesh_memory;
class Rendertarget_imgui_host;
class Rendertarget_mesh;
class Scene_builder;
class Scene_root;
class Tools;
class Scene_views;
class Headset_view;

class Toggle_menu_visibility_command : public erhe::commands::Command
{
public:
    Toggle_menu_visibility_command(erhe::commands::Commands& commands, Editor_context& context);
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
};

class Hotbar_trackpad_command : public erhe::commands::Command
{
public:
    Hotbar_trackpad_command(erhe::commands::Commands& commands, Editor_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
};

class Hotbar_rotate_tool_command : public erhe::commands::Command
{
public:
    Hotbar_rotate_tool_command(erhe::commands::Commands& commands, Editor_context& context, int rotate_direction);
    auto try_call() -> bool override;

private:
    Editor_context& m_context;
    int m_rotate_direction;
};

class Hotbar_thumbstick_command : public erhe::commands::Command
{
public:
    Hotbar_thumbstick_command(erhe::commands::Commands& commands, Editor_context& context);
    auto try_call_with_input(erhe::commands::Input_arguments& input) -> bool override;

private:
    Editor_context& m_context;
    glm::vec2       m_last_value          {0.0f, 0.0f};
    float           m_activate_threshold  {0.6f};
    float           m_deactivate_threshold{0.4f};
    bool            m_left_active         {false};
    bool            m_right_active        {false};
    bool            m_up_active           {false};
    bool            m_down_active         {false};
};

class Hotbar
    : public erhe::imgui::Imgui_window
    , public Tool
{
public:
    Hotbar(
        erhe::commands::Commands&    commands,
        erhe::imgui::Imgui_renderer& imgui_renderer,
        erhe::imgui::Imgui_windows&  imgui_windows,
        Editor_context&              editor_context,
        Editor_message_bus&          editor_message_bus,
        Headset_view&                headset_view,
        Mesh_memory&                 mesh_memory,
        Scene_builder&               scene_builder,
        Tools&                       tools
    );

    void get_all_tools();

    // Implements Imgui_window
    auto flags   () -> ImGuiWindowFlags override;
    void on_begin() override;
    void on_end  () override;
    void imgui   () override;

    // Public API
    auto try_trackpad          (erhe::commands::Input_arguments& input) -> bool;
    void rotate_tool           (int direction);
    auto get_color             (int color) -> glm::vec4&;
    auto toggle_mesh_visibility() -> bool;
    void set_mesh_visibility   (bool value);
    auto get_position          () const -> glm::vec3;
    void set_position          (glm::vec3 position);
    auto get_locked            () const -> bool;
    void set_locked            (bool value);

private:
    void on_message            (Editor_message& message);
    void update_node_transform ();
    void tool_button           (uint32_t id, Tool* tool);
    void handle_slot_update    ();
    void update_slot_from_tool (Tool* tool);

    void init_hotbar();
    void init_radial_menu(Mesh_memory& mesh_memory, Scene_root& scene_root);

    [[nodiscard]] auto get_camera() const -> std::shared_ptr<erhe::scene::Camera>;

    // Commands
    Toggle_menu_visibility_command            m_toggle_visibility_command;
    Hotbar_rotate_tool_command                m_prev_tool_command;
    Hotbar_rotate_tool_command                m_next_tool_command;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    Hotbar_trackpad_command                   m_trackpad_command;
    erhe::commands::Xr_vector2f_click_command m_trackpad_click_command;
    Hotbar_thumbstick_command                 m_thumbstick_command;
#endif

    std::shared_ptr<erhe::scene::Node>              m_rendertarget_node;
    std::shared_ptr<Rendertarget_mesh>              m_rendertarget_mesh;
    std::shared_ptr<Rendertarget_imgui_host>        m_rendertarget_imgui_host;

    std::shared_ptr<erhe::scene::Node>              m_radial_menu_node;
    std::shared_ptr<erhe::scene::Mesh>              m_radial_menu_background_mesh;
    std::vector<std::shared_ptr<erhe::scene::Mesh>> m_radial_menu_icons;
    bool                                            m_mesh_visible{true};

    bool  m_enabled   {true};
    bool  m_show      {true};
    bool  m_use_radial{true};
    bool  m_locked    {false};
    float m_x         { 0.0f};
    float m_y         { 0.07f};
    float m_z         {-0.4f};

    std::size_t        m_slot      {0};
    std::size_t        m_slot_first{0};
    std::size_t        m_slot_last {0};
    std::vector<Tool*> m_slots;

    glm::vec4 m_color_active  {0.3f, 0.3f, 0.8f, 0.8f};
    glm::vec4 m_color_hover   {0.4f, 0.4f, 0.6f, 0.8f};
    glm::vec4 m_color_inactive{0.1f, 0.1f, 0.4f, 0.8f};
};

} // namespace editor
