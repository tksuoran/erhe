#if defined(ERHE_WINDOW_LIBRARY_GLFW)

#include "erhe/toolkit/glfw_window.hpp"
#include "erhe/gl/dynamic_load.hpp"
#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/sleep.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/printf.h>
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32 1
#define GLFW_EXPOSE_NATIVE_WGL 1
#include <GLFW/glfw3native.h>
#endif

#include <gsl/assert>

#include <cstdlib>
#include <stdexcept>
#include <ctime>

namespace gl
{

using glproc = void (*)();

auto get_proc_address(const char* procname) -> glproc
{
    auto proc = glfwGetProcAddress(procname);
    return proc;
}

} // namespace gl

namespace erhe::toolkit
{

namespace
{

auto glfw_key_to_erhe(const int glfw_key) -> Keycode
{
    switch (glfw_key)
    {
        case GLFW_KEY_SPACE              : return Key_space;
        case GLFW_KEY_APOSTROPHE         : return Key_apostrophe;
        case GLFW_KEY_COMMA              : return Key_comma;
        case GLFW_KEY_MINUS              : return Key_minus;
        case GLFW_KEY_PERIOD             : return Key_period;
        case GLFW_KEY_SLASH              : return Key_slash;
        case GLFW_KEY_0                  : return Key_0;
        case GLFW_KEY_1                  : return Key_1;
        case GLFW_KEY_2                  : return Key_2;
        case GLFW_KEY_3                  : return Key_3;
        case GLFW_KEY_4                  : return Key_4;
        case GLFW_KEY_5                  : return Key_5;
        case GLFW_KEY_6                  : return Key_6;
        case GLFW_KEY_7                  : return Key_7;
        case GLFW_KEY_8                  : return Key_8;
        case GLFW_KEY_9                  : return Key_9;
        case GLFW_KEY_SEMICOLON          : return Key_semicolon;
        case GLFW_KEY_EQUAL              : return Key_equal;
        case GLFW_KEY_A                  : return Key_a;
        case GLFW_KEY_B                  : return Key_b;
        case GLFW_KEY_C                  : return Key_c;
        case GLFW_KEY_D                  : return Key_d;
        case GLFW_KEY_E                  : return Key_e;
        case GLFW_KEY_F                  : return Key_f;
        case GLFW_KEY_G                  : return Key_g;
        case GLFW_KEY_H                  : return Key_h;
        case GLFW_KEY_I                  : return Key_i;
        case GLFW_KEY_J                  : return Key_j;
        case GLFW_KEY_K                  : return Key_k;
        case GLFW_KEY_L                  : return Key_l;
        case GLFW_KEY_M                  : return Key_m;
        case GLFW_KEY_N                  : return Key_n;
        case GLFW_KEY_O                  : return Key_o;
        case GLFW_KEY_P                  : return Key_p;
        case GLFW_KEY_Q                  : return Key_q;
        case GLFW_KEY_R                  : return Key_r;
        case GLFW_KEY_S                  : return Key_s;
        case GLFW_KEY_T                  : return Key_t;
        case GLFW_KEY_U                  : return Key_u;
        case GLFW_KEY_V                  : return Key_v;
        case GLFW_KEY_W                  : return Key_w;
        case GLFW_KEY_X                  : return Key_x;
        case GLFW_KEY_Y                  : return Key_y;
        case GLFW_KEY_Z                  : return Key_z;
        case GLFW_KEY_LEFT_BRACKET       : return Key_left_bracket;
        case GLFW_KEY_BACKSLASH          : return Key_backslash;
        case GLFW_KEY_RIGHT_BRACKET      : return Key_right_bracket;
        case GLFW_KEY_GRAVE_ACCENT       : return Key_grave_accent;
        case GLFW_KEY_WORLD_1            : return Key_world_1;
        case GLFW_KEY_WORLD_2            : return Key_world_2;
        case GLFW_KEY_ESCAPE             : return Key_escape;
        case GLFW_KEY_ENTER              : return Key_enter;
        case GLFW_KEY_TAB                : return Key_tab;
        case GLFW_KEY_BACKSPACE          : return Key_backspace;
        case GLFW_KEY_INSERT             : return Key_insert;
        case GLFW_KEY_DELETE             : return Key_delete;
        case GLFW_KEY_RIGHT              : return Key_right;
        case GLFW_KEY_LEFT               : return Key_left;
        case GLFW_KEY_DOWN               : return Key_down;
        case GLFW_KEY_UP                 : return Key_up;
        case GLFW_KEY_PAGE_UP            : return Key_page_up;
        case GLFW_KEY_PAGE_DOWN          : return Key_page_down;
        case GLFW_KEY_HOME               : return Key_home;
        case GLFW_KEY_END                : return Key_end;
        case GLFW_KEY_CAPS_LOCK          : return Key_caps_lock;
        case GLFW_KEY_SCROLL_LOCK        : return Key_scroll_lock;
        case GLFW_KEY_NUM_LOCK           : return Key_num_lock;
        case GLFW_KEY_PRINT_SCREEN       : return Key_print_screen;
        case GLFW_KEY_PAUSE              : return Key_pause;
        case GLFW_KEY_F1                 : return Key_f1;
        case GLFW_KEY_F2                 : return Key_f2;
        case GLFW_KEY_F3                 : return Key_f3;
        case GLFW_KEY_F4                 : return Key_f4;
        case GLFW_KEY_F5                 : return Key_f5;
        case GLFW_KEY_F6                 : return Key_f6;
        case GLFW_KEY_F7                 : return Key_f7;
        case GLFW_KEY_F8                 : return Key_f8;
        case GLFW_KEY_F9                 : return Key_f9;
        case GLFW_KEY_F10                : return Key_f10;
        case GLFW_KEY_F11                : return Key_f11;
        case GLFW_KEY_F12                : return Key_f12;
        case GLFW_KEY_F13                : return Key_f13;
        case GLFW_KEY_F14                : return Key_f14;
        case GLFW_KEY_F15                : return Key_f15;
        case GLFW_KEY_F16                : return Key_f16;
        case GLFW_KEY_F17                : return Key_f17;
        case GLFW_KEY_F18                : return Key_f18;
        case GLFW_KEY_F19                : return Key_f19;
        case GLFW_KEY_F20                : return Key_f20;
        case GLFW_KEY_F21                : return Key_f21;
        case GLFW_KEY_F22                : return Key_f22;
        case GLFW_KEY_F23                : return Key_f23;
        case GLFW_KEY_F24                : return Key_f24;
        case GLFW_KEY_F25                : return Key_f25;
        case GLFW_KEY_KP_0               : return Key_kp_0;
        case GLFW_KEY_KP_1               : return Key_kp_1;
        case GLFW_KEY_KP_2               : return Key_kp_2;
        case GLFW_KEY_KP_3               : return Key_kp_3;
        case GLFW_KEY_KP_4               : return Key_kp_4;
        case GLFW_KEY_KP_5               : return Key_kp_5;
        case GLFW_KEY_KP_6               : return Key_kp_6;
        case GLFW_KEY_KP_7               : return Key_kp_7;
        case GLFW_KEY_KP_8               : return Key_kp_8;
        case GLFW_KEY_KP_9               : return Key_kp_9;
        case GLFW_KEY_KP_DECIMAL         : return Key_kp_decimal;
        case GLFW_KEY_KP_DIVIDE          : return Key_kp_divide;
        case GLFW_KEY_KP_MULTIPLY        : return Key_kp_multiply;
        case GLFW_KEY_KP_SUBTRACT        : return Key_kp_subtract;
        case GLFW_KEY_KP_ADD             : return Key_kp_add;
        case GLFW_KEY_KP_ENTER           : return Key_kp_enter;
        case GLFW_KEY_KP_EQUAL           : return Key_kp_equal;
        case GLFW_KEY_LEFT_SHIFT         : return Key_left_shift;
        case GLFW_KEY_LEFT_CONTROL       : return Key_left_control;
        case GLFW_KEY_LEFT_ALT           : return Key_left_alt;
        case GLFW_KEY_LEFT_SUPER         : return Key_left_super;
        case GLFW_KEY_RIGHT_SHIFT        : return Key_right_shift;
        case GLFW_KEY_RIGHT_CONTROL      : return Key_right_control;
        case GLFW_KEY_RIGHT_ALT          : return Key_right_alt;
        case GLFW_KEY_RIGHT_SUPER        : return Key_right_super;
        case GLFW_KEY_MENU               : return Key_menu;
        case GLFW_KEY_UNKNOWN            : return Key_unknown;
        default:
            return Key_unknown;
    }
}

auto glfw_modifiers_to_erhe(const int glfw_modifiers) -> Key_modifier_mask
{
    uint32_t mask = 0;
    // TODO GLFW_MOD_CAPS_LOCK
    // TODO GLFW_MOD_NUM_LOCK
    if (glfw_modifiers & GLFW_MOD_CONTROL) mask |= Key_modifier_bit_ctrl;
    if (glfw_modifiers & GLFW_MOD_SHIFT)   mask |= Key_modifier_bit_shift;
    if (glfw_modifiers & GLFW_MOD_SUPER)   mask |= Key_modifier_bit_super;
    if (glfw_modifiers & GLFW_MOD_ALT)     mask |= Key_modifier_bit_menu;
    return mask;
}

auto glfw_mouse_button_to_erhe(const int glfw_mouse_button) -> Mouse_button
{
    switch (glfw_mouse_button)
    {
        case GLFW_MOUSE_BUTTON_LEFT:   return Mouse_button_left;
        case GLFW_MOUSE_BUTTON_MIDDLE: return Mouse_button_middle;
        case GLFW_MOUSE_BUTTON_RIGHT:  return Mouse_button_right;
        default:
            // TODO
            return Mouse_button_left;
    }
}

// erhe uses click count from mango:
//  0     = release
//  1     = press
//  wheel = signed int with direction and amount
auto glfw_mouse_button_action_to_erhe(const int glfw_mouse_button_action) -> int
{
    switch (glfw_mouse_button_action)
    {
        case GLFW_PRESS:   return 1;
        case GLFW_RELEASE: return 0;
        default: return 0; // TODO
    }
}

auto get_event_handler(GLFWwindow* glfw_window) -> Event_handler*
{
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr)
    {
        return &window->get_root_view();
    }
    return nullptr;
}

void key_event_callback(
    GLFWwindow* glfw_window,
    const int   key,
    const int   scancode,
    const int   action,
    const int   glfw_modifiers
)
{
    static_cast<void>(scancode);

    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        switch (action)
        {
            case GLFW_PRESS:
            case GLFW_RELEASE:
            {
                event_handler->on_key(
                    glfw_key_to_erhe(key),
                    glfw_modifiers_to_erhe(glfw_modifiers),
                    (action == GLFW_PRESS)
                );
                break;
            }

            case GLFW_REPEAT:
            default:
                break;
        }
    }
}

void char_event_callback(
    GLFWwindow*        glfw_window,
    const unsigned int codepoint
)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_char(codepoint);
    }
}

void mouse_position_event_callback(GLFWwindow* glfw_window, double x, double y)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_mouse_move(x, y);
    }
}

void mouse_button_event_callback(GLFWwindow* glfw_window, const int button, const int action, const int mods)
{
    static_cast<void>(mods);

    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_mouse_click(
            glfw_mouse_button_to_erhe(button),
            glfw_mouse_button_action_to_erhe(action)
        );
    }
}

void mouse_wheel_event_callback(GLFWwindow* glfw_window, const double x, const double y)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_mouse_wheel(x, y);
    }
}

void window_resize_event_callback(GLFWwindow* glfw_window, const int width, const int height)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_resize(width, height);
    }
}

void window_refresh_callback(GLFWwindow* glfw_window)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_refresh();
    }
}

void window_close_event_callback(GLFWwindow* glfw_window)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_close();
    }
}

void window_focus_event_callback(GLFWwindow* glfw_window, int focused)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_focus(focused);
    }
}

void window_cursor_enter_callback(GLFWwindow* glfw_window, int entered)
{
    auto* const event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_cursor_enter(entered);
    }
}

} // namespace

auto Context_window::get_glfw_window() const -> GLFWwindow*
{
    return m_glfw_window;
}

int Context_window::s_window_count{0};

Context_window::Context_window(const Window_configuration& configuration)
    : m_root_view{this}
{
    const bool ok = open(configuration);

    ERHE_VERIFY(ok);
}

Context_window::Context_window(Context_window* share)
    : m_root_view{this}
{
    Expects(share != nullptr);

    const bool ok = open(
        {
            .fullscreen        = false,
            .width             = 64,
            .height            = 64,
            .msaa_sample_count = 0,
            .title             = "erhe share context",
            .share             = share
        }
    );

    ERHE_VERIFY(ok);
}

// Currently this is not thread safe.
// For now, only call this from main thread.
auto Context_window::open(
    const Window_configuration& configuration
) -> bool
{
    ERHE_PROFILE_FUNCTION

    if (s_window_count == 0)
    {
        if (glfwInit() != GLFW_TRUE)
        {
            fputs("Failed to initialize GLFW\n", stderr);
            return false;
        }

        sleep_initialize();
    }

    const bool primary = (configuration.share == nullptr);

    glfwWindowHint(GLFW_CLIENT_API,       GLFW_OPENGL_API);
    glfwWindowHint(GLFW_RED_BITS,         8);
    glfwWindowHint(GLFW_GREEN_BITS,       8);
    glfwWindowHint(GLFW_BLUE_BITS,        8);
    glfwWindowHint(GLFW_ALPHA_BITS,       8);
    //glfwWindowHint(GLFW_DEPTH_BITS,     24);
    glfwWindowHint(GLFW_SRGB_CAPABLE,     GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
    glfwWindowHint(GLFW_CENTER_CURSOR,    GLFW_TRUE); // Fullscreen only
    if (configuration.msaa_sample_count > 0)
    {
        glfwWindowHint(GLFW_SAMPLES, configuration.msaa_sample_count);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
#if !defined(NDEBUG)
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT,  GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_NO_ERROR,      GLFW_FALSE);
#else
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT,  GLFW_FALSE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_NO_ERROR,      GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE,               primary ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, configuration.framebuffer_transparency ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* const share_window = !primary
        ? reinterpret_cast<GLFWwindow*>(configuration.share->get_glfw_window())
        : nullptr;

    GLFWmonitor* monitor = configuration.fullscreen ? glfwGetPrimaryMonitor() : nullptr;

    if (
        configuration.fullscreen &&
        (monitor != nullptr)
    )
    {
        ERHE_PROFILE_SCOPE("window (fullscreen)");

        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        m_glfw_window = glfwCreateWindow(
            mode->width,
            mode->height,
            configuration.title,
            monitor,
            share_window
        );
    }
    else
    {
        ERHE_PROFILE_SCOPE("window");

        m_glfw_window = glfwCreateWindow(
            configuration.width,
            configuration.height,
            configuration.title,
            monitor,
            share_window
        );
    }

    if (m_glfw_window == nullptr)
    {
        printf("Failed to open GLFW window for GL 4.6\n");
        if (s_window_count == 0)
        {
            glfwTerminate();
        }
        return false;
    }

    s_window_count++;
    m_is_event_loop_running = false;
    m_is_mouse_captured = false;

    auto* const window = static_cast<GLFWwindow*>(m_glfw_window);
    glfwSetWindowUserPointer    (window, this);
    glfwSetWindowSizeCallback   (window, window_resize_event_callback);
    glfwSetWindowRefreshCallback(window, window_refresh_callback);
    glfwSetKeyCallback          (window, key_event_callback);
    glfwSetCharCallback         (window, char_event_callback);
    glfwSetCursorPosCallback    (window, mouse_position_event_callback);
    glfwSetMouseButtonCallback  (window, mouse_button_event_callback);
    glfwSetScrollCallback       (window, mouse_wheel_event_callback);
    glfwSetWindowCloseCallback  (window, window_close_event_callback);
    glfwSetWindowFocusCallback  (window, window_focus_event_callback);
    glfwSetCursorEnterCallback  (window, window_cursor_enter_callback);

    if (primary)
    {
        GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
        m_mouse_cursor[Mouse_cursor_Arrow     ] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        m_mouse_cursor[Mouse_cursor_TextInput ] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
        m_mouse_cursor[Mouse_cursor_ResizeNS  ] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
        m_mouse_cursor[Mouse_cursor_ResizeEW  ] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
        m_mouse_cursor[Mouse_cursor_Hand      ] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
        m_mouse_cursor[Mouse_cursor_Crosshair ] = glfwCreateStandardCursor(GLFW_CROSSHAIR_CURSOR);
#if GLFW_HAS_NEW_CURSORS
        m_mouse_cursor[Mouse_cursor_ResizeAll ] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
        m_mouse_cursor[Mouse_cursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
        m_mouse_cursor[Mouse_cursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
        m_mouse_cursor[Mouse_cursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
        m_mouse_cursor[Mouse_cursor_ResizeAll ] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        m_mouse_cursor[Mouse_cursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        m_mouse_cursor[Mouse_cursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        m_mouse_cursor[Mouse_cursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
#endif
        glfwSetErrorCallback(prev_error_callback);

        glfwSetInputMode(window, GLFW_CURSOR,               GLFW_CURSOR_NORMAL);
        glfwSetInputMode(window, GLFW_STICKY_KEYS,          GL_FALSE);
        glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GL_FALSE);

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        glfwShowWindow(window);
        glfwMakeContextCurrent(window);
        log_window->info("Setting swap interval to {}", configuration.swap_interval);
        glfwSwapInterval(configuration.swap_interval);
        if (s_window_count == 1)
        {
            get_extensions();
        }
    }
    else
    {
        for (Mouse_cursor cursor_n = 0; cursor_n < Mouse_cursor_COUNT; cursor_n++)
        {
            m_mouse_cursor[cursor_n] = nullptr;
        }
    }

    m_configuration = configuration;

    return true;
}

Context_window::~Context_window() noexcept
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        for (Mouse_cursor cursor_n = 0; cursor_n < Mouse_cursor_COUNT; cursor_n++)
        {
            glfwDestroyCursor(m_mouse_cursor[cursor_n]);
        }

        glfwDestroyWindow(window);
        --s_window_count;
        if (s_window_count == 0)
        {
            glfwTerminate();
        }
    }
}

void Context_window::break_event_loop()
{
    m_is_event_loop_running = false;
}

void Context_window::poll_events()
{
    ERHE_PROFILE_FUNCTION

    if (m_configuration.sleep_time > 0.0f)
    {
        ERHE_PROFILE_SCOPE("sleep")
        sleep_for(std::chrono::duration<float, std::milli>(m_configuration.sleep_time * 1000.0f));
    }
    if (m_configuration.wait_time > 0.0f)
    {
        ERHE_PROFILE_SCOPE("wait")
        glfwWaitEventsTimeout(m_configuration.wait_time);
    }
    else
    {
        ERHE_PROFILE_SCOPE("poll")

        glfwPollEvents();
    }
}

void Context_window::enter_event_loop()
{
    m_is_event_loop_running = true;
    for (;;)
    {
        if (!m_is_event_loop_running)
        {
            break;
        }

        poll_events();

        if (!m_is_event_loop_running)
        {
            break;
        }

        m_root_view.on_idle();
    }
}

void Context_window::get_cursor_position(double& xpos, double& ypos)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwGetCursorPos(window, &xpos, &ypos);
    }
}

void Context_window::set_visible(const bool visible)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        if (m_is_window_visible != visible)
        {
            m_is_window_visible = visible;
            if (m_is_window_visible)
            {
                glfwShowWindow(window);
            }
            else
            {
                glfwHideWindow(window);
            }
        }
    }
}

void Context_window::set_cursor(const Mouse_cursor cursor)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window == nullptr)
    {
        return;
    }
    if (m_current_mouse_cursor == cursor)
    {
        return;
    }

    glfwSetCursor(
        window,
        m_mouse_cursor[cursor] != nullptr
            ? m_mouse_cursor[cursor]
            : m_mouse_cursor[Mouse_cursor_Arrow]
    );
    m_current_mouse_cursor = cursor;
    if (!m_is_mouse_captured)
    {
        glfwSetInputMode(
            window, GLFW_CURSOR,
            m_is_mouse_captured
                ? GLFW_CURSOR_DISABLED
                : (m_current_mouse_cursor != Mouse_cursor_None)
                    ? GLFW_CURSOR_NORMAL
                    : GLFW_CURSOR_HIDDEN
        );
    }

}

void Context_window::capture_mouse(const bool capture)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        if (m_is_mouse_captured != capture)
        {
            m_is_mouse_captured = capture;
            glfwSetInputMode(
                window,
                GLFW_CURSOR,
                m_is_mouse_captured
                    ? GLFW_CURSOR_DISABLED
                    : (m_current_mouse_cursor != Mouse_cursor_None)
                        ? GLFW_CURSOR_NORMAL
                        : GLFW_CURSOR_HIDDEN
            );
        }
    }
}

auto Context_window::is_mouse_captured() const -> bool
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        const int mode = glfwGetInputMode(window, GLFW_CURSOR);
        if (mode == GLFW_CURSOR_DISABLED)
        {
            return true;
        }
        if (mode == GLFW_CURSOR_NORMAL)
        {
            return false;
        }
        ERHE_FATAL("unexpected GLFW_CURSOR_MODE");
    }
    return false;
}

auto Context_window::get_width() const -> int
{
    int width {0};
    int height{0};
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwGetWindowSize(window, &width, &height);
    }
    return width;
}

auto Context_window::get_height() const -> int
{
    int width {0};
    int height{0};
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwGetWindowSize(window, &width, &height);
    }
    return height;
}

auto Context_window::get_root_view() -> Root_view&
{
    return m_root_view;
}

void Context_window::get_extensions()
{
    gl::dynamic_load_init(glfwGetProcAddress);
}

void Context_window::make_current() const
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwMakeContextCurrent(window);
    }
}

void Context_window::clear_current() const
{
    glfwMakeContextCurrent(nullptr);
}

void Context_window::swap_buffers() const
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwSwapBuffers(window);
    }
}

auto Context_window::get_device_pointer() const -> void*
{
#if defined(_WIN32)
    return glfwGetWGLContext(m_glfw_window);
#else
    return nullptr; // TODO
#endif
}

auto Context_window::get_window_handle() const -> void*
{
#if defined(_WIN32)
    return glfwGetWin32Window(m_glfw_window);
#else
    return nullptr; // TODO
#endif
}

} // namespace erhe::toolkit

#endif // defined(ERHE_WINDOW_LIBRARY_GLFW)
