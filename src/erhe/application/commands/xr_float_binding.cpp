#include "erhe/application/commands/xr_float_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Xr_float_binding::Xr_float_binding(
    Command* const                   command,
    erhe::xr::Xr_action_float* const xr_action
)
    : Command_binding{command}
    , xr_action      {xr_action}
{
}

Xr_float_binding::Xr_float_binding()
{
}

Xr_float_binding::~Xr_float_binding() noexcept
{
}

Xr_float_binding::Xr_float_binding(
    Xr_float_binding&& other
) noexcept = default;

auto Xr_float_binding::operator=(
    Xr_float_binding&& other
) noexcept -> Xr_float_binding& = default;

[[nodiscard]] auto Xr_float_binding::get_type() const -> Type
{
    return Type::Xr_float;
}

auto Xr_float_binding::on_value_changed(
    Input_arguments& input
) -> bool
{
    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    bool consumed{false};
    command->try_ready(input);
    consumed = command->try_call(input);
    log_input_event_consumed->trace(
        "{} consumed OpenXR float input event",
        command->get_name()
    );
    return consumed;
}


} // namespace erhe::application

