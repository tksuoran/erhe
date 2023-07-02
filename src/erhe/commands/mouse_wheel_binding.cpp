#include "erhe/commands/mouse_wheel_binding.hpp"
#include "erhe/commands/command.hpp"

namespace erhe::commands {

Mouse_wheel_binding::Mouse_wheel_binding(Command* const command)
    : Command_binding{command}
{
}

Mouse_wheel_binding::Mouse_wheel_binding() = default;

Mouse_wheel_binding::~Mouse_wheel_binding() noexcept = default;

auto Mouse_wheel_binding::on_wheel(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    return command->try_call_with_input(input);
}

} // namespace erhe::commands

