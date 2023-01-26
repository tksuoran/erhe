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

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Mouse_drag; }

    [[nodiscard]] auto get_button() const -> erhe::toolkit::Mouse_button;

    auto on_button(
        Input_arguments&            input,
        erhe::toolkit::Mouse_button button,
        bool                        pressed
    ) -> bool override;

    auto on_motion(Input_arguments& input) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
};

} // namespace erhe::application
