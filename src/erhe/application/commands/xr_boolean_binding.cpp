#include "erhe/application/commands/Xr_boolean_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Xr_boolean_binding::Xr_boolean_binding(
    Command* const                     command,
    erhe::xr::Xr_action_boolean* const xr_action
)
    : Command_binding{command}
    , xr_action      {xr_action}
{
}

Xr_boolean_binding::Xr_boolean_binding()
{
}

Xr_boolean_binding::~Xr_boolean_binding() noexcept
{
}

Xr_boolean_binding::Xr_boolean_binding(
    Xr_boolean_binding&& other
) noexcept = default;

auto Xr_boolean_binding::operator=(
    Xr_boolean_binding&& other
) noexcept -> Xr_boolean_binding& = default;

[[nodiscard]] auto Xr_boolean_binding::get_type() const -> Type
{
    return Type::Xr_boolean;
}

auto Xr_boolean_binding::on_value_changed(Input_arguments& input) -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled)
    {
        //log_input->info("    (binding command {} is disabled)", command->get_name());
        return false;
    }

    //const bool click = input.button_bits != 0;
    if (command->get_command_state() == State::Inactive)
    {
        //log_input->info("    ({}->try_ready())", command->get_name());
        command->try_ready(input);
    }
    bool consumed{false};
    const auto command_state = command->get_command_state();
    if (
        (command_state == State::Ready) ||
        (command_state == State::Active)
    )
    {
        //log_input->info("    ({}->try_call())", command->get_name());
        consumed = command->try_call(input);
        log_input_event_consumed->trace(
            "{} {} OpenXR boolean input event button bits = {}",
            command->get_name(),
            consumed ? "consumed" : "did not consume",
            input.button_bits
        );
    }
    //else
    //{
    //    log_input->info(
    //        "    (binding command {} is not in ready state, state = {})",
    //        command->get_name(),
    //        erhe::application::c_str(command_state)
    //    );
    //}
    //log_input->info("    ({}->set_inactive())", command->get_name());
    command->set_inactive();
    return consumed;
}

} // namespace erhe::application

