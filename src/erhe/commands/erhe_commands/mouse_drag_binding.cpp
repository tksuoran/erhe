#include "erhe_commands/mouse_drag_binding.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands_log.hpp"

#include "erhe_verify/verify.hpp"

namespace erhe::commands {

Mouse_drag_binding::Mouse_drag_binding(
    Command* const                   command,
    const erhe::window::Mouse_button button,
    const bool                       call_on_button_down_without_motion,
    const std::optional<uint32_t>    modifier_mask
)
    : Mouse_binding                       {command, modifier_mask}
    , m_button                            {button }
    , m_call_on_button_down_without_motion{call_on_button_down_without_motion}
{
}

Mouse_drag_binding::Mouse_drag_binding() = default;

Mouse_drag_binding::~Mouse_drag_binding() noexcept = default;

auto Mouse_drag_binding::get_button() const -> erhe::window::Mouse_button
{
    return m_button;
}

auto Mouse_drag_binding::on_button(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (
        m_modifier_mask.has_value() &&
        (m_modifier_mask.value() & input.modifier_mask) != m_modifier_mask.value()
    ) {
        log_input_event_filtered->trace(
            "{} rejected drag due to modifier mask mismatch",
            command->get_name()
        );
        return ensure_inactive();
    }

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    if (!command->is_accepted()) {
        ERHE_VERIFY(command->get_command_state() == State::Inactive);
        return false;
    }

    // Mouse button down when in Inactive state -> transition to Ready state
    if (input.variant.button_pressed) {
        if (command->get_command_state() == State::Inactive) {
            command->try_ready();
        }
        if (m_call_on_button_down_without_motion) {
            const bool active = command->get_command_state() == State::Ready;
            if (active) {
                command->try_call_with_input(input);
            }
            return active; // Consumes event if command transitioned directly to active
        }
        return false;
    } else {
        return ensure_inactive();
    }
}

auto Mouse_drag_binding::ensure_inactive() -> bool
{
    auto* const command = get_command();
    bool consumed = false;
    if (command->get_command_state() != State::Inactive) {
        // Drag binding consumes button release event only
        // if command was in active state.
        consumed = command->get_command_state() == State::Active;
        command->set_inactive();
        log_input_event_consumed->trace(
            "{} consumed mouse drag release {}",
            command->get_name(),
            erhe::window::c_str(m_button)
        );
    }
    return consumed;
}

auto Mouse_drag_binding::on_motion(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    if (command->get_command_state() == State::Ready) {
        const auto value = input.variant.vector2.relative_value;
        if ((value.x != 0.0f) || (value.y != 0.0f)) {
            command->set_active();
        }
    }               

    if (command->get_command_state() != State::Active) {
        return false;
    }

    if (
        m_modifier_mask.has_value() &&
        (m_modifier_mask.value() & input.modifier_mask) != m_modifier_mask.value()
    ) {
        log_input_event_filtered->trace(
            "{} rejected drag {} due to modifier mask mismatch",
            command->get_name(),
            input.variant.button_pressed ? "press" : "release"
        );
        return ensure_inactive();
    }

    const bool consumed = command->try_call_with_input(input);
    if (consumed) {
        log_input_event_consumed->trace(
            "{} consumed mouse drag motion",
            command->get_name()
        );
    }
    return consumed;
}

} // namespace erhe::commands

