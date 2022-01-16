#pragma once

#if defined(ERHE_WINDOW_LIBRARY_GLFW)

#include "erhe/toolkit/view.hpp"

#include <string>

struct GLFWwindow;
struct GLFWcursor;

namespace erhe::toolkit
{

using Mouse_cursor = signed int;
constexpr Mouse_cursor Mouse_cursor_None       = -1;
constexpr Mouse_cursor Mouse_cursor_Arrow      = 0;
constexpr Mouse_cursor Mouse_cursor_TextInput  = 1;   // When hovering over InputText, etc.
constexpr Mouse_cursor Mouse_cursor_ResizeAll  = 2;   // (Unused by Dear ImGui functions)
constexpr Mouse_cursor Mouse_cursor_ResizeNS   = 3;   // When hovering over an horizontal border
constexpr Mouse_cursor Mouse_cursor_ResizeEW   = 4;   // When hovering over a vertical border or a column
constexpr Mouse_cursor Mouse_cursor_ResizeNESW = 5;   // When hovering over the bottom-left corner of a window
constexpr Mouse_cursor Mouse_cursor_ResizeNWSE = 6;   // When hovering over the bottom-right corner of a window
constexpr Mouse_cursor Mouse_cursor_Hand       = 7;   // (Unused by Dear ImGui functions. Use for e.g. hyperlinks)
constexpr Mouse_cursor Mouse_cursor_NotAllowed = 8;   // When hovering something with disallowed interaction. Usually a crossed circle.
constexpr Mouse_cursor Mouse_cursor_COUNT      = 9;

class Context_window
{
public:
    Context_window(const int width, const int height, const int msaa_sample_count);

    explicit Context_window(Context_window* share);
    virtual ~Context_window();

    auto open(
        const int          width,
        const int          height,
        const int          msaa_sample_count,
        const std::string& title,
        const int          opengl_major_version,
        const int          opengl_minor_version,
        Context_window*    share
    ) -> bool;

    void make_current() const;

    void clear_current() const;

    void swap_buffers() const;

    void break_event_loop();

    void enter_event_loop();

    [[nodiscard]] auto get_width() const -> int;

    [[nodiscard]] auto get_height() const -> int;

    [[nodiscard]] auto get_root_view() -> Root_view&
    {
        return m_root_view;
    }

    void get_cursor_position(double& xpos, double& ypos);

    void set_visible(const bool visible);

    void set_cursor(const Mouse_cursor cursor);

    void capture_mouse(const bool capture);

    [[nodiscard]] auto is_mouse_captured() const -> bool;

    [[nodiscard]] auto get_glfw_window() const -> GLFWwindow*;

    [[nodiscard]] auto get_opengl_major_version() const -> int
    {
        return m_opengl_major_version;
    }

    [[nodiscard]] auto get_opengl_minor_version() const -> int
    {
        return m_opengl_minor_version;
    }

private:
    void get_extensions();

    Root_view    m_root_view;
    GLFWwindow*  m_glfw_window          {nullptr};
    Mouse_cursor m_current_mouse_cursor {Mouse_cursor_Arrow};
    bool         m_is_event_loop_running{false};
    bool         m_is_mouse_captured    {false};
    bool         m_is_window_visible    {false};
    int          m_opengl_major_version {0};
    int          m_opengl_minor_version {0};
    GLFWcursor*  m_mouse_cursor         [Mouse_cursor_COUNT];

    static int s_window_count;
};

} // namespace erhe::toolkit

#endif // defined(ERHE_WINDOW_LIBRARY_GLFW)
