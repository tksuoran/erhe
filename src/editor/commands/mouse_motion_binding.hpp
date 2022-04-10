#pragma once

#include "commands/mouse_binding.hpp"

namespace editor {

// Mouse moved
class Mouse_motion_binding
    : public Mouse_binding
{
public:
    explicit Mouse_motion_binding(Command* const command);
    ~Mouse_motion_binding() noexcept override;

    Mouse_motion_binding();
    Mouse_motion_binding(const Mouse_motion_binding&) = delete;
    Mouse_motion_binding(Mouse_motion_binding&& other) noexcept;
    auto operator=(const Mouse_motion_binding&) -> Mouse_motion_binding& = delete;
    auto operator=(Mouse_motion_binding&& other) noexcept -> Mouse_motion_binding&;

    auto on_motion(Command_context& context) -> bool override;
};

} // namespace Editor

