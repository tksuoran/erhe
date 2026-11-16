#pragma once

#include "erhe_window/window_event_handler.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace editor {

class Input_state
{
public:
    bool      mouse_button[static_cast<int>(erhe::window::Mouse_button_count)]{};
    glm::vec2 mouse_position{0.0f, 0.0f};
    bool      shift  {false};
    bool      control{false};
    bool      alt    {false};
    // Every key event updates these, whichever window owns the keyboard.
    bool      key_down[static_cast<int>(erhe::window::Key_last) + 1]{};
    uint32_t  modifier_mask{0};
};

}
