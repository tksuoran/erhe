#pragma once

#include "erhe/application/commands/command_binding.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <mutex>

#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "erhe/application/commands/xr_boolean_binding.hpp"
#endif

#if defined(ERHE_XR_LIBRARY_OPENXR)
namespace erhe::xr {

class Xr_action_boolean;
class Xr_action_float;
class Xr_action_vector2f;
class Xr_action_pose;

}
#endif

namespace erhe::application {

class Command;
class Command_binding;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Xr_boolean_binding;
class Xr_float_binding;
class Xr_vector2f_binding;
#endif
class Key_binding;
class Mouse_binding;
class Mouse_button_binding;
class Mouse_drag_binding;
class Mouse_motion_binding;
class Mouse_wheel_binding;
class Update_binding;

class Commands
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Commands"};
    static constexpr std::string_view c_title    {"Commands"};
    static constexpr uint32_t         c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Commands ();
    ~Commands() noexcept override;

    // Implements erhe::components::Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    void register_command(Command* command);
    void imgui           ();
    void sort_bindings   ();

    [[nodiscard]] auto get_commands            () const -> const std::vector<Command*>&;
    [[nodiscard]] auto get_key_bindings        () const -> const std::vector<Key_binding>&;
    [[nodiscard]] auto get_mouse_bindings      () const -> const std::vector<std::unique_ptr<Mouse_binding>>&;
    [[nodiscard]] auto get_mouse_wheel_bindings() const -> const std::vector<std::unique_ptr<Mouse_wheel_binding>>&;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    [[nodiscard]] auto get_xr_boolean_bindings () const -> const std::vector<Xr_boolean_binding>&;
    [[nodiscard]] auto get_xr_float_bindings   () const -> const std::vector<Xr_float_binding>&;
    [[nodiscard]] auto get_xr_vector2f_bindings() const -> const std::vector<Xr_vector2f_binding>&;
#endif
    [[nodiscard]] auto get_update_bindings     () const -> const std::vector<Update_binding>&;

    void bind_command_to_key(
        Command*                command,
        erhe::toolkit::Keycode  code,
        bool                    pressed       = true,
        std::optional<uint32_t> modifier_mask = {}
    );

    void bind_command_to_mouse_button(
        Command*                    command,
        erhe::toolkit::Mouse_button button,
        bool                        trigger_on_pressed
    );

    void bind_command_to_mouse_wheel(Command* command);
    void bind_command_to_mouse_motion(Command* command);

    void bind_command_to_mouse_drag(
        Command*                    command,
        erhe::toolkit::Mouse_button button,
        bool                        call_on_button_down_without_motion
    );

#if defined(ERHE_XR_LIBRARY_OPENXR)
    void bind_command_to_xr_boolean_action(
        Command*                     command,
        erhe::xr::Xr_action_boolean* xr_action,
        Button_trigger               button_trigger
    );

    void bind_command_to_xr_float_action(
        Command*                   command,
        erhe::xr::Xr_action_float* xr_action
    );

    void bind_command_to_xr_vector2f_action(
        Command*                      command,
        erhe::xr::Xr_action_vector2f* xr_action
    );
#endif

    void bind_command_to_update(
        Command* command
    );

    [[nodiscard]] auto accept_mouse_command(Command* command) const -> bool
    {
        return
            (m_active_mouse_command == nullptr) ||
            (m_active_mouse_command == command);
    }

    void command_inactivated(Command* command);

    [[nodiscard]] auto last_mouse_button_bits   () const -> uint32_t;
    [[nodiscard]] auto last_mouse_position      () const -> glm::vec2;
    [[nodiscard]] auto last_mouse_position_delta() const -> glm::vec2;

    // Subset of erhe::toolkit::View
    void on_key         (erhe::toolkit::Keycode code, uint32_t modifier_mask, bool pressed);
    void on_mouse_move  (float x, float y);
    void on_mouse_button(erhe::toolkit::Mouse_button button, bool pressed);
    void on_mouse_wheel (float x, float y);
    void on_update      ();
#if defined(ERHE_XR_LIBRARY_OPENXR)
    void on_xr_action   (erhe::xr::Xr_action_boolean&  xr_action);
    void on_xr_action   (erhe::xr::Xr_action_float&    xr_action);
    void on_xr_action   (erhe::xr::Xr_action_vector2f& xr_action);
#endif

private:
    [[nodiscard]] auto get_command_priority(Command* command) const -> int;

    void sort_mouse_bindings        ();
    void sort_xr_bindings           ();
    void inactivate_ready_commands  ();
    void update_active_mouse_command(Command* command);
    void commands                   (State filter);

    std::mutex m_command_mutex;
    Command*   m_active_mouse_command     {nullptr}; // does not tell if command(s) is/are ready
    uint32_t   m_last_mouse_button_bits   {0u};
    glm::vec2  m_last_mouse_position      {0.0f, 0.0f};
    glm::vec2  m_last_mouse_position_delta{0.0f, 0.0f};

    std::vector<Command*>                             m_commands;
    std::vector<Key_binding>                          m_key_bindings;
    std::vector<std::unique_ptr<Mouse_binding>>       m_mouse_bindings;
    std::vector<std::unique_ptr<Mouse_wheel_binding>> m_mouse_wheel_bindings;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::vector<Xr_boolean_binding>                   m_xr_boolean_bindings;
    std::vector<Xr_float_binding>                     m_xr_float_bindings;
    std::vector<Xr_vector2f_binding>                  m_xr_vector2f_bindings;
#endif
    std::vector<Update_binding>                       m_update_bindings;
};

extern Commands* g_commands;

} // namespace erhe::application
