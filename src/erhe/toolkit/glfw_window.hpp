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
    bool            fullscreen              {false};
    bool            use_finish              {false};
    bool            framebuffer_transparency{false};
    int             gl_major                {4};
    int             gl_minor                {6};
    int             width                   {1920};
    int             height                  {1080};
    int             msaa_sample_count       {0};
    int             swap_interval           {1};
    float           sleep_time              {0.0f};
    float           wait_time               {0.01f};
    const char*     title                   {nullptr};
    Context_window* share                   {nullptr};
};

class Context_window
{
public:
    explicit Context_window(const Window_configuration& configuration);

    explicit Context_window(Context_window* share);
    virtual ~Context_window() noexcept;

    [[nodiscard]] auto get_width        () const -> int;
    [[nodiscard]] auto get_height       () const -> int;
    [[nodiscard]] auto get_root_view    () -> Root_view&;
    [[nodiscard]] auto is_mouse_captured() const -> bool;
    [[nodiscard]] auto get_glfw_window  () const -> GLFWwindow*;

    auto open               (const Window_configuration& configuration) -> bool;
    void make_current       () const;
    void clear_current      () const;
    void swap_buffers       () const;
    void break_event_loop   ();
    void enter_event_loop   ();
    void poll_events        ();
    void get_cursor_position(float& xpos, float& ypos);
    void set_visible        (bool visible);
    void set_cursor         (Mouse_cursor cursor);
    void capture_mouse      (bool capture);

    auto get_device_pointer() const -> void*;
    auto get_window_handle () const -> void*;

private:
    void get_extensions();

    Root_view            m_root_view;
    GLFWwindow*          m_glfw_window          {nullptr};
    Mouse_cursor         m_current_mouse_cursor {Mouse_cursor_Arrow};
    bool                 m_is_event_loop_running{false};
    bool                 m_is_mouse_captured    {false};
    bool                 m_is_window_visible    {false};
    GLFWcursor*          m_mouse_cursor         [Mouse_cursor_COUNT];
    Window_configuration m_configuration;

    static int s_window_count;
};

} // namespace erhe::toolkit

#endif // defined(ERHE_WINDOW_LIBRARY_GLFW)
