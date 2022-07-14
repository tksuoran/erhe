#pragma once

#include <cstdint>

namespace erhe::application
{

class View;
class Window;

class Imgui_viewport
{
public:
    virtual ~Imgui_viewport();

    virtual void begin_imgui_frame(View& view) = 0;
    virtual void end_imgui_frame  () = 0;

    virtual [[nodiscard]] auto want_capture_mouse() const -> bool = 0;

    virtual void on_key         (const signed int keycode, const uint32_t modifier_mask, const bool pressed) = 0;
    virtual void on_char        (const unsigned int codepoint) = 0;
    virtual void on_focus       (int focused) = 0;
    virtual void on_cursor_enter(int entered) = 0;
    virtual void on_mouse_click (const uint32_t button, const int count) = 0;
    virtual void on_mouse_wheel (const double x, const double y) = 0;
};

} // namespace erhe::application
