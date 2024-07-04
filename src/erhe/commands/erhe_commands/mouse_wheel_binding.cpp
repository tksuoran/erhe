#include "erhe_commands/mouse_wheel_binding.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/commands_log.hpp"

namespace erhe::commands {

Mouse_wheel_binding::Mouse_wheel_binding(Command* const command, const std::optional<uint32_t> modifier_mask)
    : Command_binding{command}
    , m_modifier_mask{modifier_mask}
{
}

Mouse_wheel_binding::Mouse_wheel_binding() = default;

Mouse_wheel_binding::~Mouse_wheel_binding() noexcept = default;

auto Mouse_wheel_binding::on_wheel(Input_arguments& input) -> bool
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
        return false;
    }

    return command->try_call_with_input(input);
}

} // namespace erhe::commands

