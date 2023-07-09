#pragma once

#include "erhe/toolkit/window_event_handler.hpp"

#include <glm/glm.hpp>

namespace editor {

class Input_state
    : public erhe::toolkit::Window_event_handler
{
public:
    auto on_key         (signed int keycode, uint32_t modifier_mask, bool pressed) -> bool override;
    auto on_mouse_move  (float x, float y) -> bool                                         override;
    auto on_mouse_button(uint32_t button, bool pressed) -> bool                            override;

    bool      mouse_button[static_cast<int>(erhe::toolkit::Mouse_button_count)]{};
    glm::vec2 mouse_position{0.0f, 0.0f};
    bool      shift  {false};
    bool      control{false};
    bool      alt    {false};
};

} // namespace editor