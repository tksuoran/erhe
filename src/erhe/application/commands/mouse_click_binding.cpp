#include "erhe/application/commands/mouse_click_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/log.hpp"

namespace erhe::application {

Mouse_click_binding::Mouse_click_binding(
    Command* const                    command,
    const erhe::toolkit::Mouse_button button
)
    : Mouse_binding{command}
    , m_button     {button }
{
}

Mouse_click_binding::Mouse_click_binding()
{
}

Mouse_click_binding::~Mouse_click_binding()
{
}

Mouse_click_binding::Mouse_click_binding(Mouse_click_binding&& other) noexcept
    : Mouse_binding{std::move(other)}
    , m_button     {other.m_button}
{
}

auto Mouse_click_binding::operator=(Mouse_click_binding&& other) noexcept -> Mouse_click_binding&
{
    Mouse_binding::operator=(std::move(other));
    this->m_button = other.m_button;
    return *this;
}

auto Mouse_click_binding::on_button(
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
        // Paranoid check
        if (command->state() != State::Inactive)
        {
            command->set_inactive(context);
        }
        return false;
    }

    // Mouse button down
    if (count > 0)
    {
        if (command->state() == State::Inactive)
        {
            command->try_ready(context);
        }
        return false;
    }
    else // count == 0
    {
        bool consumed{false};
        if (command->state() == State::Ready)
        {
            consumed = command->try_call(context);
            log_input_event_consumed.trace(
                "{} consumed mouse button {} click\n",
                command->name(),
                erhe::toolkit::c_str(button)
            );
        }
        command->set_inactive(context);
        return consumed;
    }
}

auto Mouse_click_binding::on_motion(Command_context& context) -> bool
{
    auto* const command = get_command();

    if (command->state() == State::Disabled)
    {
        return false;
    }

    // Motion when not in Inactive state -> transition to inactive state
    if (command->state() != State::Inactive)
    {
        command->set_inactive(context);
    }

    return false;
}

} // namespace erhe::application

