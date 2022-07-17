#pragma once

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

#include <cstdint>

namespace erhe::application
{

class View;
class Window;

/// Base class for imgui viewports - where imgui windows can be shown.
/// Currently must be window imgui viewport,
/// rendertarget imgui viewport is in TODO list.
class Imgui_viewport
{
public:
    Imgui_viewport();
    virtual ~Imgui_viewport();

    virtual void begin_imgui_frame() = 0;
    virtual void end_imgui_frame  () = 0;

    [[nodiscard]] auto want_capture_mouse() const -> bool;
    [[nodiscard]] auto has_cursor        () const -> bool;

    void make_imgui_context_current  ();
    void make_imgui_context_uncurrent();

    void on_key         (const signed int keycode, const uint32_t modifier_mask, const bool pressed);
    void on_char        (const unsigned int codepoint);
    void on_focus       (int focused);
    void on_cursor_enter(int entered);
    void on_mouse_move  (const double x, const double y);
    void on_mouse_click (const uint32_t button, const int count);
    void on_mouse_wheel (const double x, const double y);

protected:
    double        m_time      {0.0};
    bool          m_has_cursor{false};

#if defined(ERHE_GUI_LIBRARY_IMGUI)
    bool          m_mouse_just_pressed[ImGuiMouseButton_COUNT];
    ImGuiContext* m_imgui_context{nullptr};
#endif

};

} // namespace erhe::application
