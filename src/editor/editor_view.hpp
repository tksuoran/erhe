#pragma once

#include "command.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/component.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <functional>

namespace editor {

class Configuration;
class Editor_rendering;
class Editor_time;
class Editor_tools;
class Editor_view;
class Fly_camera_tool;
class Log_window;
class Id_renderer;
class Pointer_context;
class Operation_stack;
class Scene_builder;
class Scene_root;
class Viewport_window;
class Viewport_windows;
class Window;


class Editor_view
    : public erhe::components::Component
    , public erhe::toolkit::View
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name       {"Editor_view"};
    static constexpr std::string_view c_description{"Commands"};
    static constexpr uint32_t         hash{
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Editor_view();
    ~Editor_view() override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements View
    void update        () override;
    void on_enter      () override;
    void on_mouse_move (const double x, const double y) override;
    void on_mouse_click(const erhe::toolkit::Mouse_button button, const int count) override;
    void on_key_press  (const erhe::toolkit::Keycode code, const uint32_t modifier_mask) override;
    void on_key_release(const erhe::toolkit::Keycode code, const uint32_t modifier_mask) override;
    void on_key        (bool pressed, erhe::toolkit::Keycode code, uint32_t modifier_mask);

    // Implements Imgui_window
    void imgui() override;

    // Public API

    void register_command(Command* command);

    auto bind_command_to_key(
        Command*                     command,
        const erhe::toolkit::Keycode code,
        const bool                   pressed       = true,
        const uint32_t               modifier_mask = 0
    ) -> erhe::toolkit::Unique_id<Key_binding>::id_type;

    auto bind_command_to_mouse_click(
        Command*                          command,
        const erhe::toolkit::Mouse_button button
    ) -> erhe::toolkit::Unique_id<Mouse_click_binding>::id_type;

    auto bind_command_to_mouse_motion(
        Command* command
    ) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type;

    auto bind_command_to_mouse_drag(
        Command*                          command,
        const erhe::toolkit::Mouse_button button
    ) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type;

    void remove_command_binding(const erhe::toolkit::Unique_id<Command_binding>::id_type binding_id);

    [[nodiscard]] auto accept_mouse_command(Command* command) const -> bool
    {
        return 
            (m_active_mouse_command == nullptr) ||
            (m_active_mouse_command == command);
    }

private:
    [[nodiscard]] auto get_command_priority(Command* command) const -> int;

    void sort_mouse_bindings        ();
    void inactivate_ready_commands  ();
    void update_active_mouse_command(Command* command);

    Configuration*    m_configuration   {nullptr};
    Editor_rendering* m_editor_rendering{nullptr};
    Editor_time*      m_editor_time     {nullptr};
    Editor_tools*     m_editor_tools    {nullptr};
    Fly_camera_tool*  m_fly_camera_tool {nullptr};
    Log_window*       m_log_window      {nullptr};
    Operation_stack*  m_operation_stack {nullptr};
    Pointer_context*  m_pointer_context {nullptr};
    Scene_builder*    m_scene_builder   {nullptr};
    Scene_root*       m_scene_root      {nullptr};
    Viewport_windows* m_viewport_windows{nullptr};
    Window*           m_window          {nullptr};

    Command*                                    m_active_mouse_command{nullptr};
    std::vector<Command*>                       m_commands;
    std::vector<Key_binding>                    m_key_bindings;
    std::vector<std::unique_ptr<Mouse_binding>> m_mouse_bindings;
};

}
