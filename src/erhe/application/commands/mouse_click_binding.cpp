#include "erhe/application/commands/mouse_click_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

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

Mouse_click_binding::~Mouse_click_binding() noexcept
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

[[nodiscard]] auto Mouse_click_binding::get_button() const -> erhe::toolkit::Mouse_button
{
    return m_button;
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

    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    if (!context.accept_mouse_command(command))
    {
        // Paranoid check
        if (command->get_command_state() != State::Inactive)
        {
            command->set_inactive(context);
        }
        return false;
    }

    // Mouse button down
    if (count > 0)
    {
        if (command->get_command_state() == State::Inactive)
        {
            command->try_ready(context);
        }
        return false;
    }
    else // count == 0
    {
        bool consumed{false};
        if (command->get_command_state() == State::Ready)
        {
            consumed = command->try_call(context);
            log_input_event_consumed->trace(
                "{} consumed mouse button {} click",
                command->get_name(),
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

    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    // Motion when not in Inactive state -> transition to inactive state
    if (command->get_command_state() != State::Inactive)
    {
        command->set_inactive(context);
    }

    return false;
}

} // namespace erhe::application

