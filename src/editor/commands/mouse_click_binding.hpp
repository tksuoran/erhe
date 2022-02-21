#pragma once

#include "commands/mouse_binding.hpp"

namespace editor {

// Mouse pressed and released while not being moved
class Mouse_click_binding
    : public Mouse_binding
{
public:
    Mouse_click_binding(
        Command* const                    command,
        const erhe::toolkit::Mouse_button button
    );

    auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool override;

    auto on_motion(Command_context& context) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
};

} // namespace Editor

