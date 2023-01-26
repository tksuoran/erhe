#include "erhe/application/commands/mouse_wheel_binding.hpp"
#include "erhe/application/commands/command.hpp"

namespace erhe::application {

Mouse_wheel_binding::Mouse_wheel_binding(Command* const command)
    : Command_binding{command}
{
}

Mouse_wheel_binding::Mouse_wheel_binding()
{
}

Mouse_wheel_binding::~Mouse_wheel_binding() noexcept
{
}

Mouse_wheel_binding::Mouse_wheel_binding(Mouse_wheel_binding&& other) noexcept
    : Command_binding{std::move(other)}
{
}

auto Mouse_wheel_binding::operator=(Mouse_wheel_binding&& other) noexcept -> Mouse_wheel_binding&
{
    Command_binding::operator=(std::move(other));
    return *this;
}

auto Mouse_wheel_binding::on_wheel(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled)
    {
        return false;
    }

    return command->try_call(input);
}

} // namespace erhe::application

