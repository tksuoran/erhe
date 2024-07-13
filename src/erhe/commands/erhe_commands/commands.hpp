#pragma once

#include "erhe_commands/key_binding.hpp"
#include "erhe_commands/menu_binding.hpp"
#include "erhe_commands/controller_axis_binding.hpp"
#include "erhe_commands/controller_button_binding.hpp"

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "erhe_commands/xr_boolean_binding.hpp"
#endif

#include "erhe_window/window_event_handler.hpp"

#include <glm/glm.hpp>

#include <mutex>
#include <optional>

#if defined(ERHE_XR_LIBRARY_OPENXR)
namespace erhe::xr {
    class Xr_action_boolean;
    class Xr_action_float;
    class Xr_action_vector2f;
    class Xr_action_pose;
}
#endif

namespace erhe::commands {

class Command;
class Command_binding;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Xr_boolean_binding;
class Xr_float_binding;
class Xr_vector2f_binding;
#endif
class Key_binding;
class Menu_binding;
class Mouse_binding;
class Mouse_button_binding;
class Mouse_drag_binding;
class Mouse_motion_binding;
class Mouse_wheel_binding;
class Update_binding;

class Commands : public erhe::window::Window_event_handler
{
public:
    // Implements Window_event_handler
    [[nodiscard]] auto get_name() const -> const char* override { return "Commands"; }

    Commands();
    ~Commands() noexcept override;

    // Public API
    void register_command(Command* command);
    void sort_bindings   ();

    [[nodiscard]] auto get_commands                  () const -> const std::vector<Command*>&;
    [[nodiscard]] auto get_key_bindings              () const -> const std::vector<Key_binding>&;
    [[nodiscard]] auto get_menu_bindings             () const -> const std::vector<Menu_binding>&;
    [[nodiscard]] auto get_mouse_bindings            () const -> const std::vector<std::unique_ptr<Mouse_binding>>&;
    [[nodiscard]] auto get_mouse_wheel_bindings      () const -> const std::vector<std::unique_ptr<Mouse_wheel_binding>>&;
    [[nodiscard]] auto get_controller_axis_bindings  () const -> const std::vector<Controller_axis_binding>&;
    [[nodiscard]] auto get_controller_button_bindings() const -> const std::vector<Controller_button_binding>&;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    [[nodiscard]] auto get_xr_boolean_bindings () const -> const std::vector<Xr_boolean_binding>&;
    [[nodiscard]] auto get_xr_float_bindings   () const -> const std::vector<Xr_float_binding>&;
    [[nodiscard]] auto get_xr_vector2f_bindings() const -> const std::vector<Xr_vector2f_binding>&;
#endif
    [[nodiscard]] auto get_update_bindings     () const -> const std::vector<Update_binding>&;

    void bind_command_to_key(
        Command*                command,
        erhe::window::Keycode   code,
        bool                    pressed       = true,
        std::optional<uint32_t> modifier_mask = {}
    );

    void bind_command_to_mouse_button(
        Command*                   command,
        erhe::window::Mouse_button button,
        bool                       trigger_on_pressed,
        std::optional<uint32_t>    modifier_mask = {}
    );

    void bind_command_to_mouse_wheel(Command* command, std::optional<uint32_t> modifier_mask = {});
    void bind_command_to_mouse_motion(Command* command, std::optional<uint32_t> modifier_mask = {});

    void bind_command_to_menu(Command* command, std::string_view menu_path, std::function<bool()> enabled_callback = {});

    void bind_command_to_controller_axis(Command* command, int axis, std::optional<uint32_t> modifier_mask = {});
    void bind_command_to_controller_button(
        Command*                   command,
        erhe::window::Mouse_button button,
        Button_trigger             button_trigger,
        std::optional<uint32_t>    modifier_mask = {}
    );

    void bind_command_to_mouse_drag(
        Command*                   command,
        erhe::window::Mouse_button button,
        bool                       call_on_button_down_without_motion,
        std::optional<uint32_t>    modifier_mask = {}
    );

#if defined(ERHE_XR_LIBRARY_OPENXR)
    void bind_command_to_xr_boolean_action (Command* command, erhe::xr::Xr_action_boolean* xr_action, Button_trigger button_trigger);
    void bind_command_to_xr_float_action   (Command* command, erhe::xr::Xr_action_float* xr_action);
    void bind_command_to_xr_vector2f_action(Command* command, erhe::xr::Xr_action_vector2f* xr_action);
#endif

    void bind_command_to_update(Command* command);

    [[nodiscard]] auto accept_mouse_command(const Command* command) const -> bool
    {
        return
            (m_active_mouse_command == nullptr) ||
            (m_active_mouse_command == command);
    }

    void command_inactivated(Command* command);

    [[nodiscard]] auto last_mouse_button_bits   () const -> uint32_t;
    [[nodiscard]] auto last_mouse_position      () const -> glm::vec2;
    [[nodiscard]] auto last_mouse_position_delta() const -> glm::vec2;

    // Implements erhe::window::Window_event_handler
    auto has_active_mouse    () const                                                                                         -> bool override;
    auto on_key              (erhe::window::Keycode code, uint32_t modifier_mask, bool pressed)                               -> bool override;
    auto on_mouse_move       (float absolute_x, float absolute_y, float relative_x, float relative_y, uint32_t modifier_mask) -> bool override;
    auto on_mouse_button     (erhe::window::Mouse_button button, bool pressed, uint32_t modifier_mask)                        -> bool override;
    auto on_mouse_wheel      (float x, float y, uint32_t modifier_mask)                                                       -> bool override;
    auto on_idle             ()                                                                                               -> bool override;
    auto on_controller_axis  (int axis, float value, uint32_t modifier_mask)                                                  -> bool override;
    auto on_controller_button(int button, bool value, uint32_t modifier_mask)                                                 -> bool override;

#if defined(ERHE_XR_LIBRARY_OPENXR)
    void on_xr_action   (erhe::xr::Xr_action_boolean&  xr_action);
    void on_xr_action   (erhe::xr::Xr_action_float&    xr_action);
    void on_xr_action   (erhe::xr::Xr_action_vector2f& xr_action);
#endif

    [[nodiscard]] auto get_command_priority(Command* command) const -> int;
    [[nodiscard]] auto get_active_mouse_command() -> Command* { return m_active_mouse_command; }

private:
    void sort_mouse_bindings        ();
    void sort_controller_bindings   ();
    void sort_xr_bindings           ();
    void inactivate_ready_commands  ();
    void update_active_mouse_command(Command* command);

    std::mutex m_command_mutex;
    Command*   m_active_mouse_command     {nullptr}; // does not tell if command(s) is/are ready
    uint32_t   m_last_mouse_button_bits   {0u};
    glm::vec2  m_last_mouse_position      {0.0f, 0.0f};
    glm::vec2  m_last_mouse_position_delta{0.0f, 0.0f};
    uint32_t   m_last_modifier_mask       {0};

    std::vector<Command*>                             m_commands;
    std::vector<Key_binding>                          m_key_bindings;
    std::vector<Menu_binding>                         m_menu_bindings;
    std::vector<Controller_axis_binding>              m_controller_axis_bindings;
    std::vector<Controller_button_binding>            m_controller_button_bindings;
    std::vector<std::unique_ptr<Mouse_binding>>       m_mouse_bindings;
    std::vector<std::unique_ptr<Mouse_wheel_binding>> m_mouse_wheel_bindings;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::vector<Xr_boolean_binding>                   m_xr_boolean_bindings;
    std::vector<Xr_float_binding>                     m_xr_float_bindings;
    std::vector<Xr_vector2f_binding>                  m_xr_vector2f_bindings;
#endif
    std::vector<Update_binding>                       m_update_bindings;
};

} // namespace erhe::commands
