#include "erhe/application/commands/controller_trigger_value_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Controller_trigger_value_binding::Controller_trigger_value_binding(
    Command* const command
)
    : Controller_trigger_binding{command}
{
}

Controller_trigger_value_binding::Controller_trigger_value_binding()
{
}

Controller_trigger_value_binding::~Controller_trigger_value_binding() noexcept
{
}

Controller_trigger_value_binding::Controller_trigger_value_binding(
    Controller_trigger_value_binding&& other
) noexcept = default;

auto Controller_trigger_value_binding::operator=(
    Controller_trigger_value_binding&& other
) noexcept -> Controller_trigger_value_binding& = default;

[[nodiscard]] auto Controller_trigger_value_binding::get_type() const -> Type
{
    return Type::Controller_trigger_value;
}

auto Controller_trigger_value_binding::on_trigger_value(
    Command_context& context
) -> bool
{
    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    bool consumed{false};
    command->try_ready(context);
    consumed = command->try_call(context);
    log_input_event_consumed->info(
        "{} consumed controller trigger value",
        command->get_name()
    );
    return consumed;
}


} // namespace erhe::application

