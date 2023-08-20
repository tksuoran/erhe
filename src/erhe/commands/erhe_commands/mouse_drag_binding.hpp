#pragma once

#include "erhe_commands/mouse_binding.hpp"

namespace erhe::commands {

// Mouse button pressed and then moved while pressed
class Mouse_drag_binding
    : public Mouse_binding
{
public:
    Mouse_drag_binding(
        Command*                   command,
        erhe::window::Mouse_button button,
        bool                       call_on_button_down_without_motion
    );
    Mouse_drag_binding();
    ~Mouse_drag_binding() noexcept override;

    [[nodiscard]] auto get_type  () const -> Type override { return Command_binding::Type::Mouse_drag; }
    [[nodiscard]] auto get_button() const -> erhe::window::Mouse_button override;

    auto on_button(Input_arguments& input) -> bool override;
    auto on_motion(Input_arguments& input) -> bool override;

private:
    erhe::window::Mouse_button m_button;
    bool                       m_call_on_button_down_without_motion;
};

} // namespace erhe::commands
