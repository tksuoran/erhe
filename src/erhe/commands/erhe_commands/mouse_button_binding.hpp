 #pragma once

#include "erhe_commands/mouse_binding.hpp"

namespace erhe::commands {

// Mouse pressed and released while not being moved
class Mouse_button_binding : public Mouse_binding
{
public:
    Mouse_button_binding(
        Command*                      command,
        erhe::window::Mouse_button    button,
        bool                          trigger_on_pressed = false,
        const std::optional<uint32_t> modifier_mask = {}
    );
    Mouse_button_binding();
    ~Mouse_button_binding() noexcept override;

    auto get_type  () const -> Type override { return Command_binding::Type::Mouse_button; }
    auto get_button() const -> erhe::window::Mouse_button override;

    auto on_button(Input_arguments& input) -> bool override;
    auto on_motion(Input_arguments& input) -> bool override;

private:
    erhe::window::Mouse_button m_button;
    bool                       m_trigger_on_pressed;
};

} // namespace erhe::commands
