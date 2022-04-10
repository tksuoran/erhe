#pragma once

#include "windows/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <functional>
#include <mutex>

namespace editor {

class Command;
class Command_binding;
class Key_binding;
class Mouse_binding;
class Mouse_click_binding;
class Mouse_drag_binding;
class Mouse_motion_binding;
class Mouse_wheel_binding;

class Configuration;
class Editor_imgui_windows;
class Editor_rendering;
class Editor_time;
class Editor_tools;
class Editor_view;
class Fly_camera_tool;
class Log_window;
class Id_renderer;
class Pointer_context;
class Operation_stack;
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

    Editor_view ();
    ~Editor_view() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    void run();

    // Implements View
    void update         () override;
    void on_close       () override;
    void on_focus       (int focused) override;
    void on_cursor_enter(int entered) override;
    void on_refresh     () override;
    void on_enter       () override;
    void on_mouse_move  (const double x, const double y) override;
    void on_mouse_click (const erhe::toolkit::Mouse_button button, const int count) override;
    void on_mouse_wheel (const double x, const double y) override;
    void on_key         (const erhe::toolkit::Keycode code, const uint32_t modifier_mask, const bool pressed) override;
    void on_char        (const unsigned int codepoint) override;
    //void on_key         (bool pressed, erhe::toolkit::Keycode code, uint32_t modifier_mask);

    // Implements Imgui_window
    void imgui() override;

    // Public API

    void register_command(Command* const command);

    auto bind_command_to_key(
        Command* const                   command,
        const erhe::toolkit::Keycode     code,
        const bool                       pressed       = true,
        const nonstd::optional<uint32_t> modifier_mask = {}
    ) -> erhe::toolkit::Unique_id<Key_binding>::id_type;

    auto bind_command_to_mouse_click(
        Command* const                    command,
        const erhe::toolkit::Mouse_button button
    ) -> erhe::toolkit::Unique_id<Mouse_click_binding>::id_type;

    auto bind_command_to_mouse_wheel(
        Command* const command
    ) -> erhe::toolkit::Unique_id<Mouse_wheel_binding>::id_type;

    auto bind_command_to_mouse_motion(
        Command* const command
    ) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type;

    auto bind_command_to_mouse_drag(
        Command* const                    command,
        const erhe::toolkit::Mouse_button button
    ) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type;

    void remove_command_binding(const erhe::toolkit::Unique_id<Command_binding>::id_type binding_id);

    [[nodiscard]] auto accept_mouse_command(Command* const command) const -> bool
    {
        return
            (m_active_mouse_command == nullptr) ||
            (m_active_mouse_command == command);
    }

    void command_inactivated(Command* const command);

    [[nodiscard]] auto mouse_input_sink() const -> Imgui_window*;
    [[nodiscard]] auto to_window       (const glm::vec2 position_in_root) const -> glm::vec2;
    [[nodiscard]] auto pointer_context () const -> Pointer_context*;

    void set_mouse_input_sink(Imgui_window* mouse_input_sink);

private:
    [[nodiscard]] auto get_command_priority   (Command* const command) const -> int;
    [[nodiscard]] auto get_imgui_capture_mouse() const -> bool;

    void sort_mouse_bindings        ();
    void inactivate_ready_commands  ();
    void update_active_mouse_command(Command* const command);

    // Component dependencies
    std::shared_ptr<Configuration>        m_configuration;
    std::shared_ptr<Editor_imgui_windows> m_editor_imgui_windows;
    std::shared_ptr<Editor_rendering>     m_editor_rendering;
    std::shared_ptr<Editor_time>          m_editor_time;
    std::shared_ptr<Editor_tools>         m_editor_tools;
    std::shared_ptr<Fly_camera_tool>      m_fly_camera_tool;
    std::shared_ptr<Log_window>           m_log_window;
    std::shared_ptr<Operation_stack>      m_operation_stack;
    std::shared_ptr<Pointer_context>      m_pointer_context;
    std::shared_ptr<Scene_root>           m_scene_root;
    std::shared_ptr<Viewport_windows>     m_viewport_windows;
    std::shared_ptr<Window>               m_window;

    std::mutex                                        m_command_mutex;
    Command*                                          m_active_mouse_command{nullptr};

    Imgui_window*                                     m_mouse_input_sink{nullptr};
    glm::vec2                                         m_window_position          {};
    glm::vec2                                         m_window_size              {};
    glm::vec2                                         m_window_content_region_min{};
    glm::vec2                                         m_window_content_region_max{};

    std::vector<Command*>                             m_commands;
    std::vector<Key_binding>                          m_key_bindings;
    std::vector<std::unique_ptr<Mouse_binding>>       m_mouse_bindings;
    std::vector<std::unique_ptr<Mouse_wheel_binding>> m_mouse_wheel_bindings;
    bool                                              m_ready          {false};
    bool                                              m_close_requested{false};
    glm::dvec2                                        m_last_mouse_position;
};

}
