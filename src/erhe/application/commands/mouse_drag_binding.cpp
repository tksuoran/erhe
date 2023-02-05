#include "erhe/application/commands/mouse_drag_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Mouse_drag_binding::Mouse_drag_binding(
    Command* const                    command,
    const erhe::toolkit::Mouse_button button,
    const bool                        call_on_button_down_without_motion
)
    : Mouse_binding                       {command}
    , m_button                            {button }
    , m_call_on_button_down_without_motion{call_on_button_down_without_motion}
{
}

Mouse_drag_binding::Mouse_drag_binding() = default;

Mouse_drag_binding::~Mouse_drag_binding() noexcept = default;

[[nodiscard]] auto Mouse_drag_binding::get_button() const -> erhe::toolkit::Mouse_button
{
    return m_button;
}

auto Mouse_drag_binding::on_button(
    Input_arguments& input
) -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    if (!g_commands->accept_mouse_command(command))
    {
        ERHE_VERIFY(command->get_command_state() == State::Inactive);
        log_input_event_filtered->trace(
            "{} not active so button event ignored",
            command->get_name()
        );
        return false;
    }

    // Mouse button down when in Inactive state -> transition to Ready state
    if (input.button_pressed)
    {
        if (command->get_command_state() == State::Inactive)
        {
            command->try_ready();
        }
        if (m_call_on_button_down_without_motion)
        {
            const bool active = command->get_command_state() == State::Ready;
            if (active)
            {
                command->try_call(input);
            }
            return active; // Consumes event if command transitioned directly to active
        }
        return false;
    }
    else
    {
        bool consumed = false;
        if (command->get_command_state() != State::Inactive)
        {
            // Drag binding consumes button release event only
            // if command was ini active state.
            consumed = command->get_command_state() == State::Active;
            command->set_inactive();
            log_input_event_consumed->trace(
                "{} consumed mouse drag release {}",
                command->get_name(),
                erhe::toolkit::c_str(m_button)
            );
        }
        return consumed;
    }
}

auto Mouse_drag_binding::on_motion(Input_arguments& input) -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    if (command->get_command_state() == State::Ready)
    {
        const auto value = input.vector2.relative_value;
        if ((value.x != 0.0f) || (value.y != 0.0f))
        {
            command->set_active();
        }
    }

    if (command->get_command_state() != State::Active)
    {
        return false;
    }

    const bool consumed = command->try_call(input);
    if (consumed)
    {
        log_input_event_consumed->trace(
            "{} consumed mouse drag motion",
            command->get_name()
        );
    }
    return consumed;
}

} // namespace erhe::application

