#pragma once

#include "commands/command_binding.hpp"

#include "erhe/toolkit/view.hpp"

namespace editor {

class Command;
class Command_context;

class Mouse_binding
    : public Command_binding
{
public:
    explicit Mouse_binding(Command* const command);

    virtual auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool;

    virtual auto on_motion(Command_context& context) -> bool;
};


} // namespace Editor

