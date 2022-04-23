#pragma once

#include "erhe/application/commands/mouse_binding.hpp"

namespace erhe::application {

// Mouse button pressed and then moved while pressed
class Mouse_drag_binding
    : public Mouse_binding
{
public:
    Mouse_drag_binding(
        Command* const                    command,
        const erhe::toolkit::Mouse_button button
    );
    ~Mouse_drag_binding() noexcept override;

    Mouse_drag_binding();
    Mouse_drag_binding(const Mouse_drag_binding&) = delete;
    Mouse_drag_binding(Mouse_drag_binding&& other) noexcept;
    auto operator=(const Mouse_drag_binding&) -> Mouse_drag_binding& = delete;
    auto operator=(Mouse_drag_binding&& other) noexcept -> Mouse_drag_binding&;

    auto on_button(
        Command_context&                  context,
        const erhe::toolkit::Mouse_button button,
        const int                         count
    ) -> bool override;

    auto on_motion(Command_context& context) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
};

} // namespace erhe::application
