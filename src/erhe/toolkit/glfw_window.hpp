#ifndef glfw_window_hpp_erhe_toolkit
#define glfw_window_hpp_erhe_toolkit

#if defined(ERHE_WINDOW_TOOLKIT_GLFW)

#include "erhe/toolkit/view.hpp"

#include <string>

namespace erhe::toolkit
{

class Context_window
{
public:
    Context_window(int width, int height);

    explicit Context_window(Context_window* share);

    ~Context_window();

    auto open(int width, int height, const std::string& title, int opengl_major_version, int opengl_minor_version, Context_window* share)
    -> bool;

    void make_current() const;

    void clear_current() const;

    void swap_buffers() const;

    void break_event_loop();

    void enter_event_loop();

    auto get_width() const
    -> int;

    auto get_height() const
    -> int;

    auto
    get_root_view()
    -> Root_view&
    {
        return m_root_view;
    }

    void get_cursor_position(double& xpos, double& ypos);

    void set_visible(bool visible);

    void show_ursor(bool show);

    void capture_mouse(bool capture);

    auto is_mouse_captured() const
    -> bool;

    auto get_glfw_window() const
    -> void*;

    auto get_opengl_major_version() const
    -> int
    {
        return m_opengl_major_version;
    }

    auto get_opengl_minor_version() const
    -> int
    {
        return m_opengl_minor_version;
    }

private:
    void get_extensions();

    Root_view  m_root_view;
    void*      m_glfw_window{nullptr};
    bool       m_is_event_loop_running{false};
    bool       m_is_mouse_captured{false};
    bool       m_is_window_visible{false};
    int        m_opengl_major_version{0};
    int        m_opengl_minor_version{0};

    static int s_window_count;
};

} // namespace erhe::toolkit

#endif // defined(ERHE_WINDOW_TOOLKIT_GLFW)
#endif // glfw_window_hpp_erhe_toolkit
