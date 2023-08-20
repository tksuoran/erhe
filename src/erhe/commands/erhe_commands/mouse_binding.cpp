#include "erhe_commands/mouse_binding.hpp"
#include "erhe_commands/command.hpp"

namespace erhe::commands {

Mouse_binding::Mouse_binding(Command* const command)
    : Command_binding{command}
{
}

Mouse_binding::Mouse_binding() = default;

Mouse_binding::~Mouse_binding() noexcept = default;

auto Mouse_binding::get_button() const -> erhe::window::Mouse_button
{
    return erhe::window::Mouse_button_none;
}

auto Mouse_binding::on_button(
    Input_arguments& input
) -> bool
{
    static_cast<void>(input);
    return false;
}

auto Mouse_binding::on_motion(Input_arguments& input) -> bool
{
    static_cast<void>(input);
    return false;
}

} // namespace erhe::commands

