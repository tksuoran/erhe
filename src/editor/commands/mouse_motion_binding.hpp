#pragma once

#include "commands/mouse_binding.hpp"

namespace editor {

// Mouse moved
class Mouse_motion_binding
    : public Mouse_binding
{
public:
    explicit Mouse_motion_binding(Command* const command);

    auto on_motion(Command_context& context) -> bool override;
};

} // namespace Editor

