#include "input_state.hpp"

namespace editor {

auto Input_state::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
) -> bool
{
    static_cast<void>(keycode);
    static_cast<void>(pressed);
    shift   = (modifier_mask & erhe::toolkit::Key_modifier_bit_shift) == erhe::toolkit::Key_modifier_bit_shift;
    alt     = (modifier_mask & erhe::toolkit::Key_modifier_bit_menu ) == erhe::toolkit::Key_modifier_bit_menu;
    control = (modifier_mask & erhe::toolkit::Key_modifier_bit_ctrl ) == erhe::toolkit::Key_modifier_bit_ctrl;
    return false; // not consumed
}

auto Input_state::on_mouse_move(
    const float x,
    const float y
) -> bool
{
    mouse_position = glm::vec2{x, y};
    return false; // not consumed
}

auto Input_state::on_mouse_button(
    const uint32_t button,
    const bool     pressed
) -> bool
{
    mouse_button[button] = pressed;
    return false; // not consumed
}

} // namespace editor

