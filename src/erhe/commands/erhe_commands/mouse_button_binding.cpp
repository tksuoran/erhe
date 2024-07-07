#include "erhe_commands/mouse_button_binding.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands_log.hpp"

namespace erhe::commands {

Mouse_button_binding::Mouse_button_binding(
    Command* const                   command,
    const erhe::window::Mouse_button button,
    const bool                       trigger_on_pressed,
    const std::optional<uint32_t>    modifier_mask
)
    : Mouse_binding       {command, modifier_mask}
    , m_button            {button }
    , m_trigger_on_pressed{trigger_on_pressed}
{
}

Mouse_button_binding::Mouse_button_binding() = default;

Mouse_button_binding::~Mouse_button_binding() noexcept = default;

auto Mouse_button_binding::get_button() const -> erhe::window::Mouse_button
{
    return m_button;
}

auto Mouse_button_binding::on_button(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (
        m_modifier_mask.has_value() &&
        m_modifier_mask.value() != input.modifier_mask
    ) {
        log_input_event_filtered->trace(
            "{} rejected button {} due to modifier mask mismatch",
            command->get_name(),
            input.variant.button_pressed ? "press" : "release"
        );
        return false;
    }

    if (command->get_command_state() == State::Disabled) {
        log_input->trace("  binding command {} is disabled", command->get_name());
        return false;
    }

    if (!command->is_accepted()) {
        log_input->trace(
            "  command not accepted for mouse command{}",
            command->get_name()
        );
        // Paranoid check
        if (command->get_command_state() != State::Inactive) {
            command->set_inactive();
        }
        return false;
    }

    if (input.variant.button_pressed) {
        if (command->get_command_state() == State::Inactive) {
            log_input->trace("  {}->try_ready()", command->get_name());
            command->try_ready();
        }
        if (!m_trigger_on_pressed) {
            return false;
        }
    }
    if (!input.variant.button_pressed || m_trigger_on_pressed) {
        bool consumed{false};
        const auto command_state = command->get_command_state();
        if (command_state == State::Ready) {
            log_input->trace("  {}->try_call()", command->get_name());
            consumed = command->try_call_with_input(input);
            log_input_event_consumed->trace(
                "{} consumed mouse button {} {}",
                command->get_name(),
                erhe::window::c_str(m_button),
                input.variant.button_pressed ? "pressed" : "released"
            );
            log_input_event_consumed->trace(
                "  {} {} mouse button {}",
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
    return false;
}

auto Mouse_button_binding::on_motion(Input_arguments& input) -> bool
{
    static_cast<void>(input);

    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled){
        return false;
    }

    // Motion when not in Inactive state -> transition to inactive state
    if (command->get_command_state() != State::Inactive) {
        command->set_inactive();
    }

    return false;
}

} // namespace erhe::commands

