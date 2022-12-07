#include "erhe/application/commands/controller_trigger_drag_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Controller_trigger_drag_binding::Controller_trigger_drag_binding(Command* const command)
    : Controller_trigger_binding{command}
{
}

Controller_trigger_drag_binding::Controller_trigger_drag_binding()
{
}

Controller_trigger_drag_binding::~Controller_trigger_drag_binding() noexcept
{
}

Controller_trigger_drag_binding::Controller_trigger_drag_binding(
    Controller_trigger_drag_binding&& other
) noexcept = default;

auto Controller_trigger_drag_binding::operator=(
    Controller_trigger_drag_binding&& other
) noexcept -> Controller_trigger_drag_binding& = default;

[[nodiscard]] auto Controller_trigger_drag_binding::get_type() const -> Type
{
    return Type::Controller_trigger_drag;
}

auto Controller_trigger_drag_binding::on_trigger_click(
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
        ERHE_VERIFY(command->get_command_state() == State::Inactive);
        log_input_event_filtered->trace(
            "{} not active so button event ignored",
            command->get_name()
        );
        return false;
    }

    // Mouse button down when in Inactive state -> transition to Ready state
    if (click)
    {
        if (command->get_command_state() == State::Inactive)
        {
            command->try_ready(context);
        }
        return command->get_command_state() == State::Active; // Consumes event if command transitioned directly to active
    }
    else
    {
        bool consumed = false;
        if (command->get_command_state() != State::Inactive)
        {
            // Drag binding consumes button release event only
            // if command was ini active state.
            consumed = command->get_command_state() == State::Active;
            command->set_inactive(context);
            log_input_event_consumed->trace(
                "{} consumed trigger drag release",
                command->get_name()
            );
        }
        return consumed;
    }
}

auto Controller_trigger_drag_binding::on_trigger_update(
    Command_context& context
) -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    if (command->get_command_state() == State::Ready)
    {
        //const auto value = context.get_vec2_relative_value();
        //if ((value.x != 0.0f) || (value.y != 0.0f))
        //{
        // TODO move to active state directly?
        command->set_active(context);
        //}
    }

    if (command->get_command_state() != State::Active)
    {
        return false;
    }

    const bool consumed = command->try_call(context);
    if (consumed)
    {
        log_input_event_consumed->trace(
            "{} consumed trigger drag motion",
            command->get_name()
        );
    }
    return consumed;
}

} // namespace erhe::application

