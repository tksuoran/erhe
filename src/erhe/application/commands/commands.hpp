#pragma once

#include "erhe/application/commands/command_binding.hpp"
#include "erhe/application/imgui/imgui_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <mutex>

namespace erhe::application {

class Command;
class Command_binding;
class Controller_trigger_binding;
class Controller_trigger_click_binding;
class Controller_trigger_drag_binding;
class Controller_trigger_value_binding;
class Controller_trackpad_binding;
class Key_binding;
class Mouse_binding;
class Mouse_click_binding;
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
    void register_command(Command* const command);
    void imgui           ();
    void sort_bindings   ();

    [[nodiscard]] auto get_commands                    () const -> const std::vector<Command*>&;
    [[nodiscard]] auto get_key_bindings                () const -> const std::vector<Key_binding>&;
    [[nodiscard]] auto get_mouse_bindings              () const -> const std::vector<std::unique_ptr<Mouse_binding>>&;
    [[nodiscard]] auto get_mouse_wheel_bindings        () const -> const std::vector<std::unique_ptr<Mouse_wheel_binding>>&;
    [[nodiscard]] auto get_controller_trigger_bindings () const -> const std::vector<std::unique_ptr<Controller_trigger_binding>>&;
    [[nodiscard]] auto get_controller_trackpad_bindings() const -> const std::vector<Controller_trackpad_binding>&;
    [[nodiscard]] auto get_update_bindings             () const -> const std::vector<Update_binding>&;

    auto bind_command_to_key(
        Command*                command,
        erhe::toolkit::Keycode  code,
        bool                    pressed       = true,
        std::optional<uint32_t> modifier_mask = {}
    ) -> erhe::toolkit::Unique_id<Key_binding>::id_type;

    auto bind_command_to_mouse_click(
        Command*                    command,
        erhe::toolkit::Mouse_button button
    ) -> erhe::toolkit::Unique_id<Mouse_click_binding>::id_type;

    auto bind_command_to_mouse_wheel(
        Command* command
    ) -> erhe::toolkit::Unique_id<Mouse_wheel_binding>::id_type;

    auto bind_command_to_mouse_motion(
        Command* command
    ) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type;

    auto bind_command_to_mouse_drag(
        Command*                    command,
        erhe::toolkit::Mouse_button button
    ) -> erhe::toolkit::Unique_id<Mouse_motion_binding>::id_type;

    auto bind_command_to_controller_trigger_click(
        Command* command
    ) -> erhe::toolkit::Unique_id<Controller_trigger_click_binding>::id_type;

    auto bind_command_to_controller_trigger_drag(
        Command* command
    ) -> erhe::toolkit::Unique_id<Controller_trigger_drag_binding>::id_type;

    auto bind_command_to_controller_trigger_value(
        Command* command
    ) -> erhe::toolkit::Unique_id<Controller_trigger_value_binding>::id_type;

    auto bind_command_to_controller_trackpad(
        Command* command,
        bool     click
    ) -> erhe::toolkit::Unique_id<Controller_trackpad_binding>::id_type;

    auto bind_command_to_update(
        Command* command
    ) -> erhe::toolkit::Unique_id<Update_binding>::id_type;

    void remove_command_binding(erhe::toolkit::Unique_id<Command_binding>::id_type binding_id);

    [[nodiscard]] auto accept_mouse_command(Command* command) const -> bool
    {
        return
            (m_active_mouse_command == nullptr) ||
            (m_active_mouse_command == command);
    }

    [[nodiscard]] auto accept_controller_trigger_command(Command* command) const -> bool
    {
        return
            (m_active_trigger_command == nullptr) ||
            (m_active_trigger_command == command);
    }

    void command_inactivated(Command* const command);

    [[nodiscard]] auto last_mouse_button_bits       () const -> uint32_t;
    [[nodiscard]] auto last_mouse_position          () const -> glm::dvec2;
    [[nodiscard]] auto last_mouse_position_delta    () const -> glm::dvec2;
    [[nodiscard]] auto last_mouse_wheel_delta       () const -> glm::dvec2;
    [[nodiscard]] auto last_controller_trigger_value() const -> float;
    [[nodiscard]] auto last_controller_trackpad_x   () const -> float;
    [[nodiscard]] auto last_controller_trackpad_y   () const -> float;

    // Subset of erhe::toolkit::View
    void on_key                      (erhe::toolkit::Keycode code, uint32_t modifier_mask, bool pressed);
    void on_mouse_move               (double x, const double y);
    void on_mouse_click              (erhe::toolkit::Mouse_button button, int count);
    void on_mouse_wheel              (double x, double y);
    void on_update                   ();
    void on_controller_trigger_value (float trigger_value);
    void on_controller_trigger_click (bool click);
    void on_controller_trackpad_touch(float x, float y, bool touch);
    void on_controller_trackpad_click(float x, float y, bool click);

private:
    [[nodiscard]] auto get_command_priority(Command* const command) const -> int;

    void sort_mouse_bindings          ();
    void sort_trigger_bindings        ();
    void inactivate_ready_commands    ();
    void update_active_mouse_command  (Command* const command);
    void update_active_trigger_command(Command* const command);
    void commands                     (State filter);

    std::mutex m_command_mutex;
    Command*   m_active_mouse_command          {nullptr}; // does not tell if command(s) is/are ready
    Command*   m_active_trigger_command        {nullptr};
    uint32_t   m_last_mouse_button_bits        {0u};
    glm::dvec2 m_last_mouse_position           {0.0, 0.0};
    glm::dvec2 m_last_mouse_position_delta     {0.0, 0.0};
    glm::dvec2 m_last_mouse_wheel_delta        {0.0, 0.0};
    float      m_last_controller_trigger_value {0.0f};
    bool       m_last_controller_trigger_click {false};
    float      m_last_controller_trackpad_x    {0.0f};
    float      m_last_controller_trackpad_y    {0.0f};
    bool       m_last_controller_trackpad_touch{false};
    bool       m_last_controller_trackpad_click{false};

    std::vector<Command*>                                    m_commands;
    std::vector<Key_binding>                                 m_key_bindings;
    std::vector<std::unique_ptr<Mouse_binding>>              m_mouse_bindings;
    std::vector<std::unique_ptr<Mouse_wheel_binding>>        m_mouse_wheel_bindings;
    std::vector<std::unique_ptr<Controller_trigger_binding>> m_controller_trigger_bindings;
    std::vector<Controller_trackpad_binding>                 m_controller_trackpad_bindings;
    std::vector<Update_binding>                              m_update_bindings;
};

extern Commands* g_commands;

} // namespace erhe::application
