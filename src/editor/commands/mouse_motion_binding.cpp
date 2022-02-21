#include "commands/mouse_motion_binding.hpp"
#include "commands/command.hpp"

namespace editor {

Mouse_motion_binding::Mouse_motion_binding(Command* const command)
    : Mouse_binding{command}
{
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

} // namespace Editor

