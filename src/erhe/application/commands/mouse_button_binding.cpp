#include "erhe/application/commands/mouse_button_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

namespace erhe::application {

Mouse_button_binding::Mouse_button_binding(
    Command* const                    command,
    const erhe::toolkit::Mouse_button button
)
    : Mouse_binding{command}
    , m_button     {button }
{
}

Mouse_button_binding::Mouse_button_binding()
{
}

Mouse_button_binding::~Mouse_button_binding() noexcept
{
}

Mouse_button_binding::Mouse_button_binding(Mouse_button_binding&& other) noexcept
    : Mouse_binding{std::move(other)}
    , m_button     {other.m_button}
{
}

auto Mouse_button_binding::operator=(Mouse_button_binding&& other) noexcept -> Mouse_button_binding&
{
    Mouse_binding::operator=(std::move(other));
    this->m_button = other.m_button;
    return *this;
}

[[nodiscard]] auto Mouse_button_binding::get_button() const -> erhe::toolkit::Mouse_button
{
    return m_button;
}

auto Mouse_button_binding::on_button(
    Input_arguments&                  input,
    const erhe::toolkit::Mouse_button button,
    const bool                        pressed
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

    if (!g_commands->accept_mouse_command(command))
    {
        // Paranoid check
        if (command->get_command_state() != State::Inactive)
        {
            command->set_inactive();
        }
        return false;
    }

    if (pressed)
    {
        if (command->get_command_state() == State::Inactive)
        {
            command->try_ready(input);
        }
        return false;
    }
    else
    {
        bool consumed{false};
        if (command->get_command_state() == State::Ready)
        {
            consumed = command->try_call(input);
            log_input_event_consumed->trace(
                "{} consumed mouse button {} click",
                command->get_name(),
                erhe::toolkit::c_str(button)
            );
        }
        command->set_inactive();
        return consumed;
    }
}

auto Mouse_button_binding::on_motion(Input_arguments& input) -> bool
{
    static_cast<void>(input);

    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    // Motion when not in Inactive state -> transition to inactive state
    if (command->get_command_state() != State::Inactive)
    {
        command->set_inactive();
    }

    return false;
}

} // namespace erhe::application

