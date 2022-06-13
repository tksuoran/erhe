#include "erhe/application/commands/mouse_binding.hpp"
#include "erhe/application/commands/command.hpp"

namespace erhe::application {

Mouse_binding::Mouse_binding(Command* const command)
    : Command_binding{command}
{
}

Mouse_binding::Mouse_binding()
{
}

Mouse_binding::~Mouse_binding() noexcept
{
}

Mouse_binding::Mouse_binding(Mouse_binding&& other) noexcept
    : Command_binding{std::move(other)}
{
}

auto Mouse_binding::operator=(Mouse_binding&& other) noexcept -> Mouse_binding&
{
    Command_binding::operator=(std::move(other));
    return *this;
}

auto Mouse_binding::on_button(
    Command_context&                  context,
    const erhe::toolkit::Mouse_button button,
    const int                         count
) -> bool
{
    static_cast<void>(context);
    static_cast<void>(button);
    static_cast<void>(count);
    return false;
}

auto Mouse_binding::on_motion(Command_context& context) -> bool
{
    static_cast<void>(context);
    return false;
}

} // namespace erhe::application

