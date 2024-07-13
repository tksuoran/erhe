#include "erhe_commands/controller_button_binding.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands_log.hpp"

namespace erhe::commands {

Controller_button_binding::Controller_button_binding(Command* const command, const int button, const Button_trigger trigger, const std::optional<uint32_t> modifier_mask)
    : Command_binding{command}
    , m_button       {button}
    , m_trigger      {trigger}
    , m_modifier_mask{modifier_mask}
{
}

Controller_button_binding::Controller_button_binding() = default;

Controller_button_binding::~Controller_button_binding() noexcept = default;

auto Controller_button_binding::get_type() const -> Type
{
    return Type::Xr_boolean;
}

auto Controller_button_binding::get_button() const -> int
{
    return m_button;
}

auto Controller_button_binding::get_modifier_mask() const -> const std::optional<uint32_t>&
{
    return m_modifier_mask;
}

auto Controller_button_binding::test_trigger(Input_arguments& input) const -> bool
{
    switch (m_trigger) {
        case Button_trigger::Button_pressed:  return  input.variant.button_pressed;
        case Button_trigger::Button_released: return !input.variant.button_pressed;
        case Button_trigger::Any:             return true;
        default: return false;
    }
}

auto Controller_button_binding::on_value_changed(Input_arguments& input) -> bool
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

    if (m_modifier_mask.has_value() && m_modifier_mask.value() != input.modifier_mask) {
        log_input_event_filtered->trace("{} rejected controller button {} due to modifier mask mismatch", command->get_name(), m_button);
        return false;
    }


    bool consumed{false};
    const auto command_state = command->get_command_state();

    if ((command_state == State::Ready) || (command_state == State::Active)) {
        log_input->trace("  {}->try_call()", command->get_name());
        consumed = command->try_call_with_input(input);
        log_input_event_consumed->trace(
            "  {} {} controller button {} {}",
            command->get_name(),
            consumed ? "consumed" : "did not consume",
            m_button,
            input.variant.button_pressed ? "pressed" : "released"
        );
    } else {
        log_input->trace("  command {} is not in ready state, state = {}", command->get_name(), erhe::commands::c_str(command_state));
    }
    log_input->trace("  {}->set_inactive()", command->get_name());
    command->set_inactive();
    return consumed;
}

} // namespace erhe::commands

