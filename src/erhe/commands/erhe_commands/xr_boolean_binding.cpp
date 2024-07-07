#include "erhe_commands/xr_boolean_binding.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands_log.hpp"

namespace erhe::commands {

Xr_boolean_binding::Xr_boolean_binding(
    Command* const                     command,
    erhe::xr::Xr_action_boolean* const xr_action,
    const Button_trigger               trigger
)
    : Command_binding{command}
    , xr_action      {xr_action}
    , m_trigger      {trigger}
{
}

Xr_boolean_binding::Xr_boolean_binding() = default;

Xr_boolean_binding::~Xr_boolean_binding() noexcept = default;

auto Xr_boolean_binding::get_type() const -> Type
{
    return Type::Xr_boolean;
}

auto c_str(const Button_trigger value) -> const char*
{
    switch (value) {
        case Button_trigger::Button_pressed:  return "button pressed";
        case Button_trigger::Button_released: return "button released";
        case Button_trigger::Any:             return "any";
        default:                              return "?";
    }
}

auto Xr_boolean_binding::test_trigger(Input_arguments& input) const -> bool
{
    switch (m_trigger) {
        case Button_trigger::Button_pressed:  return  input.variant.button_pressed;
        case Button_trigger::Button_released: return !input.variant.button_pressed;
        case Button_trigger::Any:             return true;
        default: return false;
    }
}

auto Xr_boolean_binding::on_value_changed(Input_arguments& input) -> bool
{
    auto* const command = get_command();
    if (command->get_command_state() == State::Disabled) {
        log_input->trace("  binding command {} is disabled", command->get_name());
        return false;
    }

    const bool triggered = test_trigger(input);
    if (!triggered) {
        log_input->trace(
            "  binding trigger condition {} not met with value = {} for {}",
            c_str(m_trigger),
            input.variant.button_pressed,
            command->get_name()
        );
        return false;
    }

    if (command->get_command_state() == State::Inactive) {
        log_input->trace("  {}->try_ready()", command->get_name());
        command->try_ready();
    }

    bool consumed{false};
    const auto command_state = command->get_command_state();

    if (
        (command_state == State::Ready) ||
        (command_state == State::Active)
    ) {
        log_input->trace("  {}->try_call()", command->get_name());
        consumed = command->try_call_with_input(input);
        log_input_event_consumed->trace(
            "  {} {} XR bool {}",
            command->get_name(),
            consumed ? "consumed" : "did not consume",
            input.variant.button_pressed ? "pressed" : "released"
        );
    } else {
        log_input->trace(
            "  command {} is not in ready state, state = {}",
            command->get_name(),
            erhe::commands::c_str(command_state)
        );
    }
    log_input->trace("  {}->set_inactive()", command->get_name());
    command->set_inactive();
    return consumed;
}

} // namespace erhe::commands

