#pragma once

#if defined(ERHE_WINDOW_LIBRARY_GLFW)

#include "erhe_window/window_event_handler.hpp"

#include <string>

struct GLFWwindow;
struct GLFWcursor;

namespace erhe::window
{

using Mouse_cursor = signed int;
constexpr Mouse_cursor Mouse_cursor_None       = -1;
constexpr Mouse_cursor Mouse_cursor_Arrow      =  0;
constexpr Mouse_cursor Mouse_cursor_TextInput  =  1;   // When hovering over InputText, etc.
constexpr Mouse_cursor Mouse_cursor_ResizeAll  =  2;   // (Unused by Dear ImGui functions)
constexpr Mouse_cursor Mouse_cursor_ResizeNS   =  3;   // When hovering over an horizontal border
constexpr Mouse_cursor Mouse_cursor_ResizeEW   =  4;   // When hovering over a vertical border or a column
constexpr Mouse_cursor Mouse_cursor_ResizeNESW =  5;   // When hovering over the bottom-left corner of a window
constexpr Mouse_cursor Mouse_cursor_ResizeNWSE =  6;   // When hovering over the bottom-right corner of a window
constexpr Mouse_cursor Mouse_cursor_Hand       =  7;   // (Unused by Dear ImGui functions. Use for e.g. hyperlinks)
constexpr Mouse_cursor Mouse_cursor_NotAllowed =  8;   // When hovering something with disallowed interaction. Usually a crossed circle.
constexpr Mouse_cursor Mouse_cursor_Crosshair  =  9;   // Crosshair cursor
constexpr Mouse_cursor Mouse_cursor_COUNT      = 10;

class Window_configuration
{
public:
    bool        show                    {true};
    bool        fullscreen              {false};
    bool        use_finish              {false};
    bool        framebuffer_transparency{false};
    int         width                   {1920};
    int         height                  {1080};
    float       sleep_time              {0.0f};
    float       wait_time               {0.01f};
    std::string title                   {};
};

class Context_window
{
public:
    explicit Context_window(const Window_configuration& configuration);

    virtual ~Context_window() noexcept;

    [[nodiscard]] auto get_width                    () const -> int;
    [[nodiscard]] auto get_height                   () const -> int;
    [[nodiscard]] auto get_root_window_event_handler() -> Root_window_event_handler&;
    [[nodiscard]] auto is_mouse_captured            () const -> bool;
    [[nodiscard]] auto get_glfw_window              () const -> GLFWwindow*;

    auto open               (const Window_configuration& configuration) -> bool;
    void break_event_loop   ();
    void enter_event_loop   ();
    void poll_events        ();
    void get_cursor_position(float& xpos, float& ypos);
    void set_visible        (bool visible);
    void set_cursor         (Mouse_cursor cursor);
    void capture_mouse      (bool capture);

    [[nodiscard]] auto get_window_handle () const -> void*;
    [[nodiscard]] auto get_scale_factor  () const -> float;

private:
    Root_window_event_handler m_root_window_event_handler;
    GLFWwindow*               m_glfw_window          {nullptr};
    Mouse_cursor              m_current_mouse_cursor {Mouse_cursor_Arrow};
    bool                      m_is_event_loop_running{false};
    bool                      m_is_mouse_captured    {false};
    bool                      m_is_window_visible    {false};
    GLFWcursor*               m_mouse_cursor         [Mouse_cursor_COUNT];
    Window_configuration      m_configuration;

    static int s_window_count;
};

}

#endif // defined(ERHE_WINDOW_LIBRARY_GLFW)
