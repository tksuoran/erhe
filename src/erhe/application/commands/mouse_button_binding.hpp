 #pragma once

#include "erhe/application/commands/mouse_binding.hpp"

namespace erhe::application {

// Mouse pressed and released while not being moved
class Mouse_button_binding
    : public Mouse_binding
{
public:
    Mouse_button_binding(
        Command*                    command,
        erhe::toolkit::Mouse_button button,
        bool                        trigger_on_pressed = false
    );
    Mouse_button_binding();
    ~Mouse_button_binding() noexcept override;

    [[nodiscard]] auto get_type  () const -> Type override { return Command_binding::Type::Mouse_button; }
    [[nodiscard]] auto get_button() const -> erhe::toolkit::Mouse_button override;

    auto on_button(Input_arguments& input) -> bool override;

    auto on_motion(Input_arguments& input) -> bool override;

private:
    erhe::toolkit::Mouse_button m_button;
    bool                        m_trigger_on_pressed;
};

} // namespace erhe/application
