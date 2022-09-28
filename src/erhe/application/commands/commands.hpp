#pragma once

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
class Input_context;
class Key_binding;
class Mouse_binding;
class Mouse_click_binding;
class Mouse_drag_binding;
class Mouse_motion_binding;
class Mouse_wheel_binding;
class Update_binding;

class Configuration;
class Imgui_windows;
class Rendergraph;
class Time;
class View;
class Window;

class Hover_item
{
public:
    virtual void on_hover() = 0;
};

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
    void post_initialize            () override;

    // Public API
    void register_command(Command* const command);
    void imgui();

    [[nodiscard]] auto get_commands                   () const -> const std::vector<Command*>&;
    [[nodiscard]] auto get_key_bindings               () const -> const std::vector<Key_binding>&;
    [[nodiscard]] auto get_mouse_bindings             () const -> const std::vector<std::unique_ptr<Mouse_binding>>&;
    [[nodiscard]] auto get_mouse_wheel_bindings       () const -> const std::vector<std::unique_ptr<Mouse_wheel_binding>>&;
    [[nodiscard]] auto get_controller_trigger_bindings() const -> const std::vector<Controller_trigger_binding>&;
    [[nodiscard]] auto get_update_bindings            () const -> const std::vector<Update_binding>&;

    auto bind_command_to_key(
        Command* const                command,
        const erhe::toolkit::Keycode  code,
        const bool                    pressed       = true,
        const std::optional<uint32_t> modifier_mask = {}
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

    auto bind_command_to_controller_trigger(
        Command* const command,
        float          min_value_to_activate,
        float          max_value_to_deactivate,
        bool           drag
    ) -> erhe::toolkit::Unique_id<Controller_trigger_binding>::id_type;

    auto bind_command_to_update(
        Command* const command
    ) -> erhe::toolkit::Unique_id<Key_binding>::id_type;

    void remove_command_binding(const erhe::toolkit::Unique_id<Command_binding>::id_type binding_id);

    [[nodiscard]] auto accept_mouse_command(Command* const command) const -> bool
    {
        return
            (m_active_mouse_command == nullptr) ||
            (m_active_mouse_command == command);
    }

    void command_inactivated(Command* const command);

    [[nodiscard]] auto get_input_context() const -> Input_context*;
    void set_input_context(Input_context* input_context);

    [[nodiscard]] auto last_mouse_button_bits       () const -> uint32_t;
    [[nodiscard]] auto last_mouse_position          () const -> glm::dvec2;
    [[nodiscard]] auto last_mouse_position_delta    () const -> glm::dvec2;
    [[nodiscard]] auto last_mouse_wheel_delta       () const -> glm::dvec2;
    [[nodiscard]] auto last_controller_trigger_value() const -> float;

    // Subset of erhe::toolkit::View
    void on_key               (erhe::toolkit::Keycode code, uint32_t modifier_mask, bool pressed);
    void on_mouse_move        (double x, const double y);
    void on_mouse_click       (erhe::toolkit::Mouse_button button, int count);
    void on_mouse_wheel       (double x, double y);
    void on_update            ();
    void on_controller_trigger(float trigger_value);

private:
    [[nodiscard]] auto get_command_priority   (Command* const command) const -> int;
    //[[nodiscard]] auto get_imgui_capture_mouse() const -> bool;

    void sort_mouse_bindings        ();
    void inactivate_ready_commands  ();
    void update_active_mouse_command(Command* const command);

    // Component dependencies
    std::shared_ptr<Configuration> m_configuration;

    std::mutex     m_command_mutex;
    Command*       m_active_mouse_command{nullptr};
    Input_context* m_input_context       {nullptr};

    uint32_t   m_last_mouse_button_bits       {0u};
    glm::dvec2 m_last_mouse_position          {0.0, 0.0};
    glm::dvec2 m_last_mouse_position_delta    {0.0, 0.0};
    glm::dvec2 m_last_mouse_wheel_delta       {0.0, 0.0};
    float      m_last_controller_trigger_value{0.0f};

    std::vector<Command*>                             m_commands;
    std::vector<Key_binding>                          m_key_bindings;
    std::vector<std::unique_ptr<Mouse_binding>>       m_mouse_bindings;
    std::vector<std::unique_ptr<Mouse_wheel_binding>> m_mouse_wheel_bindings;
    std::vector<Controller_trigger_binding>           m_controller_trigger_bindings;
    std::vector<Update_binding>                       m_update_bindings;

    std::mutex m_hover_mutex;
    std::vector<std::weak_ptr<Hover_item>> m_hover_items;
};

} // namespace erhe::application
