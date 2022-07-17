#include "erhe/application/commands/mouse_drag_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Mouse_drag_binding::Mouse_drag_binding(
    Command* const                    command,
    const erhe::toolkit::Mouse_button button
)
    : Mouse_binding{command}
    , m_button     {button }
{
}

Mouse_drag_binding::Mouse_drag_binding()
{
}

Mouse_drag_binding::~Mouse_drag_binding() noexcept
{
}

Mouse_drag_binding::Mouse_drag_binding(Mouse_drag_binding&& other) noexcept
    : Mouse_binding{std::move(other)}
    , m_button     {other.m_button}
{
}

auto Mouse_drag_binding::operator=(Mouse_drag_binding&& other) noexcept -> Mouse_drag_binding&
{
    Mouse_binding::operator=(std::move(other));
    this->m_button = other.m_button;
    return *this;
}

auto Mouse_drag_binding::on_button(
    Command_context&                  context,
    const erhe::toolkit::Mouse_button button,
    const int                         count
) -> bool
{
    if (m_button != button)
    {
        return false;
    }

    auto* const command = get_command();

    if (command->state() == State::Disabled)
    {
        return false;
    }

    if (!context.accept_mouse_command(command))
    {
        ERHE_VERIFY(command->state() == State::Inactive);
        log_input_event_filtered->trace(
            "{} not active so button event ignored",
            command->name()
        );
        return false;
    }

    // Mouse button down when in Inactive state -> transition to Ready state
    if (count > 0)
    {
        if (command->state() == State::Inactive)
        {
            command->try_ready(context);
        }
        return command->state() == State::Active; // Consumes event if command transitioned directly to active
    }
    else
    {
        bool consumed = false;
        if (command->state() != State::Inactive)
        {
            // Drag binding consumes button release event only
            // if command was ini active state.
            consumed = command->state() == State::Active;
            command->set_inactive(context);
            log_input_event_consumed->trace(
                "{} consumed mouse drag release {}",
                command->name(),
                erhe::toolkit::c_str(button)
            );
        }
        return consumed;
    }
}

auto Mouse_drag_binding::on_motion(Command_context& context) -> bool
{
    auto* const command = get_command();

    if (command->state() == State::Disabled)
    {
        return false;
    }

    if (command->state() == State::Ready)
    {
        command->set_active(context);
    }

    const bool consumed = command->try_call(context);;
    if (consumed)
    {
        log_input_event_consumed->trace(
            "{} consumed mouse drag motion",
            command->name()
        );
    }
    return consumed;
}

} // namespace erhe::application

