#pragma once

#include "erhe_window/window_event_handler.hpp"

#include <glm/glm.hpp>

namespace editor {

class Input_state
    : public erhe::window::Window_event_handler
{
public:
    // Implements Window_event_handler
    [[nodiscard]] auto get_name() const -> const char* override { return "Input_state"; }

    auto on_key         (signed int keycode, uint32_t modifier_mask, bool pressed) -> bool override;
    auto on_mouse_move  (float x, float y) -> bool                                         override;
    auto on_mouse_button(uint32_t button, bool pressed) -> bool                            override;

    bool      mouse_button[static_cast<int>(erhe::window::Mouse_button_count)]{};
    glm::vec2 mouse_position{0.0f, 0.0f};
    bool      shift  {false};
    bool      control{false};
    bool      alt    {false};
};

} // namespace editor
