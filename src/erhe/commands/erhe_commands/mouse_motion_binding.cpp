#include "erhe_commands/mouse_motion_binding.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/commands_log.hpp"
#include "erhe_commands/input_arguments.hpp"

namespace erhe::commands {

Mouse_motion_binding::Mouse_motion_binding(Command* const command, const std::optional<uint32_t> modifier_mask)
    : Mouse_binding{command, modifier_mask}
{
}

Mouse_motion_binding::Mouse_motion_binding() = default;

Mouse_motion_binding::~Mouse_motion_binding() noexcept = default;

auto Mouse_motion_binding::on_motion(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (m_modifier_mask.has_value() && (m_modifier_mask.value() != input.modifier_mask)) {
        log_input_event_filtered->trace("{} rejected motion due to modifier mask mismatch", command->get_name());
        return false;
    }

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    // Motion binding never consumes the event, so that all
    // motion bindings can process the motion.
    static_cast<void>(command->try_call_with_input(input));
    return false;
}

} // namespace erhe::commands
