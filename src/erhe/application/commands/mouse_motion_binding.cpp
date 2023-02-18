#include "erhe/application/commands/mouse_motion_binding.hpp"
#include "erhe/application/commands/command.hpp"

namespace erhe::application {

Mouse_motion_binding::Mouse_motion_binding(Command* const command)
    : Mouse_binding{command}
{
}

Mouse_motion_binding::Mouse_motion_binding() = default;

Mouse_motion_binding::~Mouse_motion_binding() noexcept = default;

auto Mouse_motion_binding::on_motion(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    // Motion binding never consumes the event, so that all
    // motion bindings can process the motion.
    static_cast<void>(command->try_call_with_input(input));
    return false;
}

} // namespace erhe::application
