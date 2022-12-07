#include "erhe/application/commands/controller_trigger_click_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Controller_trigger_click_binding::Controller_trigger_click_binding(
    Command* const command
)
    : Controller_trigger_binding{command}
{
}

Controller_trigger_click_binding::Controller_trigger_click_binding()
{
}

Controller_trigger_click_binding::~Controller_trigger_click_binding() noexcept
{
}

Controller_trigger_click_binding::Controller_trigger_click_binding(
    Controller_trigger_click_binding&& other
) noexcept = default;

auto Controller_trigger_click_binding::operator=(
    Controller_trigger_click_binding&& other
) noexcept -> Controller_trigger_click_binding& = default;

[[nodiscard]] auto Controller_trigger_click_binding::get_type() const -> Type
{
    return Type::Controller_trigger_click;
}

auto Controller_trigger_click_binding::on_trigger_click(
    Command_context& context,
    const bool       click
) -> bool
{
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
    if (click)
    {
        if (command->get_command_state() == State::Inactive)
        {
            command->try_ready(context);
        }
        return false;
    }
    else
    {
        bool consumed{false};
        if (command->get_command_state() == State::Ready)
        {
            consumed = command->try_call(context);
            log_input_event_consumed->trace(
                "{} consumed trigger click",
                command->get_name()
            );
        }
        command->set_inactive(context);
        return consumed;
    }
}

} // namespace erhe::application

