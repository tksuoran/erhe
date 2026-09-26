 #pragma once

#include "erhe_commands/mouse_binding.hpp"

namespace erhe::commands {

// Mouse button click. A press readies the command; the trigger selects
// whether the command is called on the press (Button_pressed) or on the
// release when the mouse has not moved in between (Button_released).
class Mouse_button_binding : public Mouse_binding
{
public:
    Mouse_button_binding(
        Command*                   command,
        erhe::window::Mouse_button button,
        Button_trigger             trigger,
        std::optional<uint32_t>    modifier_mask = {}
    );
    Mouse_button_binding();
    ~Mouse_button_binding() noexcept override;

    [[nodiscard]] auto get_type  () const -> Type override { return Command_binding::Type::Mouse_button; }
    [[nodiscard]] auto get_button () const -> erhe::window::Mouse_button override;
    [[nodiscard]] auto get_trigger() const -> Button_trigger;

    auto on_button(Input_arguments& input) -> bool override;
    auto on_motion(Input_arguments& input) -> bool override;

private:
    erhe::window::Mouse_button m_button;
    bool                       m_trigger_on_pressed{false};
};

} // namespace erhe::commands
