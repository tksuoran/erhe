#include "erhe/application/commands/mouse_motion_binding.hpp"
#include "erhe/application/commands/command.hpp"

namespace erhe::application {

Mouse_motion_binding::Mouse_motion_binding(Command* const command)
    : Mouse_binding{command}
{
}

Mouse_motion_binding::Mouse_motion_binding()
{
}

Mouse_motion_binding::~Mouse_motion_binding() noexcept
{
}

Mouse_motion_binding::Mouse_motion_binding(Mouse_motion_binding&& other) noexcept
    : Mouse_binding{std::move(other)}
{
}

auto Mouse_motion_binding::operator=(Mouse_motion_binding&& other) noexcept -> Mouse_motion_binding&
{
    Mouse_binding::operator=(std::move(other));
    return *this;
}

auto Mouse_motion_binding::on_motion(Command_context& context) -> bool
{
    auto* const command = get_command();

    if (command->state() == State::Disabled)
    {
        return false;
    }

    // Motion binding never consumes the event, so that all
    // motion bindings can process the motion.
    static_cast<void>(command->try_call(context));
    return false;
}

} // namespace erhe::application
