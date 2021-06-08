#if defined(ERHE_WINDOW_TOOLKIT_GLFW)

#include "erhe/toolkit/glfw_window.hpp"
#include "erhe/gl/dynamic_load.hpp"
#include "erhe/toolkit/verify.hpp"

#include <fmt/printf.h>
#include <GLFW/glfw3.h>
#include <gsl/assert>

#include <cstdlib>
#include <stdexcept>
#include <ctime>

namespace gl
{

using glproc = void (*)();

auto get_proc_address(const char* procname)
-> glproc
{
    auto proc = glfwGetProcAddress(procname);
    return proc;
}

} // namespace gl

namespace erhe::toolkit
{

namespace
{

Keycode glfw_key_to_erhe(int glfw_key)
{
    switch (glfw_key)
    {
        case GLFW_KEY_SPACE              : return Keycode::Key_space;
        case GLFW_KEY_APOSTROPHE         : return Keycode::Key_apostrophe;
        case GLFW_KEY_COMMA              : return Keycode::Key_comma;
        case GLFW_KEY_MINUS              : return Keycode::Key_minus;
        case GLFW_KEY_PERIOD             : return Keycode::Key_period;
        case GLFW_KEY_SLASH              : return Keycode::Key_slash;
        case GLFW_KEY_0                  : return Keycode::Key_0;
        case GLFW_KEY_1                  : return Keycode::Key_1;
        case GLFW_KEY_2                  : return Keycode::Key_2;
        case GLFW_KEY_3                  : return Keycode::Key_3;
        case GLFW_KEY_4                  : return Keycode::Key_4;
        case GLFW_KEY_5                  : return Keycode::Key_5;
        case GLFW_KEY_6                  : return Keycode::Key_6;
        case GLFW_KEY_7                  : return Keycode::Key_7;
        case GLFW_KEY_8                  : return Keycode::Key_8;
        case GLFW_KEY_9                  : return Keycode::Key_9;
        case GLFW_KEY_SEMICOLON          : return Keycode::Key_semicolon;
        case GLFW_KEY_EQUAL              : return Keycode::Key_equal;
        case GLFW_KEY_A                  : return Keycode::Key_a;
        case GLFW_KEY_B                  : return Keycode::Key_b;
        case GLFW_KEY_C                  : return Keycode::Key_c;
        case GLFW_KEY_D                  : return Keycode::Key_d;
        case GLFW_KEY_E                  : return Keycode::Key_e;
        case GLFW_KEY_F                  : return Keycode::Key_f;
        case GLFW_KEY_G                  : return Keycode::Key_g;
        case GLFW_KEY_H                  : return Keycode::Key_h;
        case GLFW_KEY_I                  : return Keycode::Key_i;
        case GLFW_KEY_J                  : return Keycode::Key_j;
        case GLFW_KEY_K                  : return Keycode::Key_k;
        case GLFW_KEY_L                  : return Keycode::Key_l;
        case GLFW_KEY_M                  : return Keycode::Key_m;
        case GLFW_KEY_N                  : return Keycode::Key_n;
        case GLFW_KEY_O                  : return Keycode::Key_o;
        case GLFW_KEY_P                  : return Keycode::Key_p;
        case GLFW_KEY_Q                  : return Keycode::Key_q;
        case GLFW_KEY_R                  : return Keycode::Key_r;
        case GLFW_KEY_S                  : return Keycode::Key_s;
        case GLFW_KEY_T                  : return Keycode::Key_t;
        case GLFW_KEY_U                  : return Keycode::Key_u;
        case GLFW_KEY_V                  : return Keycode::Key_v;
        case GLFW_KEY_W                  : return Keycode::Key_w;
        case GLFW_KEY_X                  : return Keycode::Key_x;
        case GLFW_KEY_Y                  : return Keycode::Key_y;
        case GLFW_KEY_Z                  : return Keycode::Key_z;
        case GLFW_KEY_LEFT_BRACKET       : return Keycode::Key_left_bracket;
        case GLFW_KEY_BACKSLASH          : return Keycode::Key_backslash;
        case GLFW_KEY_RIGHT_BRACKET      : return Keycode::Key_right_bracket;
        case GLFW_KEY_GRAVE_ACCENT       : return Keycode::Key_grave_accent;
        case GLFW_KEY_WORLD_1            : return Keycode::Key_world_1;
        case GLFW_KEY_WORLD_2            : return Keycode::Key_world_2;
        case GLFW_KEY_ESCAPE             : return Keycode::Key_escape;
        case GLFW_KEY_ENTER              : return Keycode::Key_enter;
        case GLFW_KEY_TAB                : return Keycode::Key_tab;
        case GLFW_KEY_BACKSPACE          : return Keycode::Key_backspace;
        case GLFW_KEY_INSERT             : return Keycode::Key_insert;
        case GLFW_KEY_DELETE             : return Keycode::Key_delete;
        case GLFW_KEY_RIGHT              : return Keycode::Key_right;
        case GLFW_KEY_LEFT               : return Keycode::Key_left;
        case GLFW_KEY_DOWN               : return Keycode::Key_down;
        case GLFW_KEY_UP                 : return Keycode::Key_up;
        case GLFW_KEY_PAGE_UP            : return Keycode::Key_page_up;
        case GLFW_KEY_PAGE_DOWN          : return Keycode::Key_page_down;
        case GLFW_KEY_HOME               : return Keycode::Key_home;
        case GLFW_KEY_END                : return Keycode::Key_end;
        case GLFW_KEY_CAPS_LOCK          : return Keycode::Key_caps_lock;
        case GLFW_KEY_SCROLL_LOCK        : return Keycode::Key_scroll_lock;
        case GLFW_KEY_NUM_LOCK           : return Keycode::Key_num_lock;
        case GLFW_KEY_PRINT_SCREEN       : return Keycode::Key_print_screen;
        case GLFW_KEY_PAUSE              : return Keycode::Key_pause;
        case GLFW_KEY_F1                 : return Keycode::Key_f1;
        case GLFW_KEY_F2                 : return Keycode::Key_f2;
        case GLFW_KEY_F3                 : return Keycode::Key_f3;
        case GLFW_KEY_F4                 : return Keycode::Key_f4;
        case GLFW_KEY_F5                 : return Keycode::Key_f5;
        case GLFW_KEY_F6                 : return Keycode::Key_f6;
        case GLFW_KEY_F7                 : return Keycode::Key_f7;
        case GLFW_KEY_F8                 : return Keycode::Key_f8;
        case GLFW_KEY_F9                 : return Keycode::Key_f9;
        case GLFW_KEY_F10                : return Keycode::Key_f10;
        case GLFW_KEY_F11                : return Keycode::Key_f11;
        case GLFW_KEY_F12                : return Keycode::Key_f12;
        case GLFW_KEY_F13                : return Keycode::Key_f13;
        case GLFW_KEY_F14                : return Keycode::Key_f14;
        case GLFW_KEY_F15                : return Keycode::Key_f15;
        case GLFW_KEY_F16                : return Keycode::Key_f16;
        case GLFW_KEY_F17                : return Keycode::Key_f17;
        case GLFW_KEY_F18                : return Keycode::Key_f18;
        case GLFW_KEY_F19                : return Keycode::Key_f19;
        case GLFW_KEY_F20                : return Keycode::Key_f20;
        case GLFW_KEY_F21                : return Keycode::Key_f21;
        case GLFW_KEY_F22                : return Keycode::Key_f22;
        case GLFW_KEY_F23                : return Keycode::Key_f23;
        case GLFW_KEY_F24                : return Keycode::Key_f24;
        case GLFW_KEY_F25                : return Keycode::Key_f25;
        case GLFW_KEY_KP_0               : return Keycode::Key_kp_0;
        case GLFW_KEY_KP_1               : return Keycode::Key_kp_1;
        case GLFW_KEY_KP_2               : return Keycode::Key_kp_2;
        case GLFW_KEY_KP_3               : return Keycode::Key_kp_3;
        case GLFW_KEY_KP_4               : return Keycode::Key_kp_4;
        case GLFW_KEY_KP_5               : return Keycode::Key_kp_5;
        case GLFW_KEY_KP_6               : return Keycode::Key_kp_6;
        case GLFW_KEY_KP_7               : return Keycode::Key_kp_7;
        case GLFW_KEY_KP_8               : return Keycode::Key_kp_8;
        case GLFW_KEY_KP_9               : return Keycode::Key_kp_9;
        case GLFW_KEY_KP_DECIMAL         : return Keycode::Key_kp_decimal;
        case GLFW_KEY_KP_DIVIDE          : return Keycode::Key_kp_divide;
        case GLFW_KEY_KP_MULTIPLY        : return Keycode::Key_kp_multiply;
        case GLFW_KEY_KP_SUBTRACT        : return Keycode::Key_kp_subtract;
        case GLFW_KEY_KP_ADD             : return Keycode::Key_kp_add;
        case GLFW_KEY_KP_ENTER           : return Keycode::Key_kp_enter;
        case GLFW_KEY_KP_EQUAL           : return Keycode::Key_kp_equal;
        case GLFW_KEY_LEFT_SHIFT         : return Keycode::Key_left_shift;
        case GLFW_KEY_LEFT_CONTROL       : return Keycode::Key_left_control;
        case GLFW_KEY_LEFT_ALT           : return Keycode::Key_left_alt;
        case GLFW_KEY_LEFT_SUPER         : return Keycode::Key_left_super;
        case GLFW_KEY_RIGHT_SHIFT        : return Keycode::Key_right_shift;
        case GLFW_KEY_RIGHT_CONTROL      : return Keycode::Key_right_control;
        case GLFW_KEY_RIGHT_ALT          : return Keycode::Key_right_alt;
        case GLFW_KEY_RIGHT_SUPER        : return Keycode::Key_right_super;
        case GLFW_KEY_MENU               : return Keycode::Key_menu;
        case GLFW_KEY_UNKNOWN            : return Keycode::Key_unknown;
        default:
            return Keycode::Key_unknown;
    }
}

Key_modifier_mask glfw_modifiers_to_erhe(int glfw_modifiers)
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

Mouse_button glfw_mouse_button_to_erhe(int glfw_mouse_button)
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
int glfw_mouse_button_action_to_erhe(int glfw_mouse_button_action)
{
    switch (glfw_mouse_button_action)
    {
        case GLFW_PRESS:   return 1;
        case GLFW_RELEASE: return 0;
        default: return 0; // TODO
    }
}

Event_handler* get_event_handler(GLFWwindow* glfw_window)
{
    auto* window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr)
    {
        return &window->get_root_view();
    }
    return nullptr;
}

void key_event_callback(GLFWwindow* glfw_window, int key, int scancode, int action, int glfe_modifiers)
{
    static_cast<void>(scancode);
    auto* event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        Keycode keycode = glfw_key_to_erhe(key);
        if (action == GLFW_PRESS)
        {
            event_handler->on_key_press(keycode, glfw_modifiers_to_erhe(glfe_modifiers));
        }
        else if (action == GLFW_RELEASE)
        {
            event_handler->on_key_release(keycode, glfw_modifiers_to_erhe(glfe_modifiers));
        }
    }
}

void mouse_position_event_callback(GLFWwindow* glfw_window, double x, double y)
{
    auto* event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_mouse_move(x, y);
    }
}

void mouse_button_event_callback(GLFWwindow* glfw_window, int button, int action, int mods)
{
    static_cast<void>(mods);
    auto* event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_mouse_click(glfw_mouse_button_to_erhe(button), glfw_mouse_button_action_to_erhe(action));
    }
}

void mouse_wheel_event_callback(GLFWwindow* glfw_window, double x, double y)
{
    auto* event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_mouse_click(Mouse_button_wheel, static_cast<int>(x + y)); // TODO mouse wheel API
    }
}

void window_resize_event_callback(GLFWwindow* glfw_window, int width, int height)
{
    auto* event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_resize(width, height);
    }
}

void window_close_event_callback(GLFWwindow* glfw_window)
{
    auto* event_handler = get_event_handler(glfw_window);
    if (event_handler)
    {
        event_handler->on_close();
    }
}

} // namespace

auto Context_window::get_glfw_window() const
-> GLFWwindow*
{
    return m_glfw_window;
}

int Context_window::s_window_count{0};

const char* month_name[] = { "January", "February", "March", "April", "May", "June",
                             "July", "August", "September", "October", "November", "December" };

Context_window::Context_window(int width, int height)
    : m_root_view{this}
{
    time_t now = time(0);
    tm* l = localtime(&now);
    std::string title = fmt::format("erhe by Timo Suoranta {} {}. {}",
                                     month_name[l->tm_mon],
                                     l->tm_mday,
                                     1900 + l->tm_year);
    bool ok = open(width, height, title, 4, 6, nullptr);

    VERIFY(ok);
}

Context_window::Context_window(Context_window* share)
    : m_root_view{this}
{
    Expects(share != nullptr);

    bool ok = open(64, 64, "erhe share context", share->get_opengl_major_version(), share->get_opengl_minor_version(), share);

    VERIFY(ok);
}

// Currently this is not thread safe.
// For now, only call this from main thread.
auto Context_window::open(int                width,
                          int                height,
                          const std::string& title,
                          int                opengl_major_version,
                          int                opengl_minor_version,
                          Context_window*    share)
-> bool
{
    if (s_window_count == 0)
    {
        if (glfwInit() != GLFW_TRUE)
        {
            fputs("Failed to initialize GLFW\n", stderr);
            return false;
        }
    }

    bool primary = (share == nullptr);

    glfwWindowHint(GLFW_CLIENT_API,            GLFW_OPENGL_API);
    glfwWindowHint(GLFW_RED_BITS,               8);
    glfwWindowHint(GLFW_GREEN_BITS,             8);
    glfwWindowHint(GLFW_BLUE_BITS,              8);
    //glfwWindowHint(GLFW_DEPTH_BITS,            24);
    glfwWindowHint(GLFW_SRGB_CAPABLE,          GLFW_TRUE);
    //glfwWindowHint(GLFW_SAMPLES,               16);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, opengl_major_version);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, opengl_minor_version);
    glfwWindowHint(GLFW_OPENGL_PROFILE,        GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE,               primary ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* share_window = !primary ? reinterpret_cast<GLFWwindow*>(share->get_glfw_window()) : nullptr;
    GLFWmonitor* monitor{nullptr};
    m_glfw_window = glfwCreateWindow(width, height, title.c_str(), monitor, share_window);
    if (m_glfw_window == nullptr)
    {
        fmt::printf("Failed to open GLFW window for GL {}, {}\n", opengl_major_version, opengl_minor_version);
        if (s_window_count == 0)
        {
            glfwTerminate();
        }
        return false;
    }

    s_window_count++;
    m_opengl_major_version = opengl_major_version;
    m_opengl_minor_version = opengl_minor_version;
    m_is_event_loop_running = false;
    m_is_mouse_captured = false;

    auto* window = static_cast<GLFWwindow*>(m_glfw_window);
    glfwSetWindowUserPointer  (window, this);
    glfwSetWindowSizeCallback (window, window_resize_event_callback);
    glfwSetKeyCallback        (window, key_event_callback);
    glfwSetCursorPosCallback  (window, mouse_position_event_callback);
    glfwSetMouseButtonCallback(window, mouse_button_event_callback);
    glfwSetScrollCallback     (window, mouse_wheel_event_callback);
    glfwSetWindowCloseCallback(window, window_close_event_callback);

    if (primary)
    {
        glfwSetInputMode(window, GLFW_CURSOR,               GLFW_CURSOR_NORMAL);
        glfwSetInputMode(window, GLFW_STICKY_KEYS,          GL_FALSE);
        glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GL_FALSE);
        glfwSwapInterval(0);

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        glfwShowWindow(window);
        glfwMakeContextCurrent(window);
        if (s_window_count == 1)
        {
            get_extensions();
        }
    }

    return true;
}

Context_window::~Context_window()
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
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

void Context_window::enter_event_loop()
{
    m_is_event_loop_running = true;
    for (;;)
    {
        if (!m_is_event_loop_running)
        {
            break;
        }

        glfwPollEvents();

        if (!m_is_event_loop_running)
        {
            break;
        }

        m_root_view.on_idle();
    }
}

void Context_window::get_cursor_position(double& xpos, double& ypos)
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwGetCursorPos(window, &xpos, &ypos);
    }
}

void Context_window::set_visible(bool visible)
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
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

void Context_window::show_ursor(bool show)
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        if (m_is_window_visible != show)
        {
            m_is_window_visible = show;
            if (!m_is_mouse_captured)
            {
                glfwSetInputMode(window, GLFW_CURSOR, m_is_mouse_captured ? GLFW_CURSOR_DISABLED
                                                                          : m_is_window_visible ? GLFW_CURSOR_NORMAL
                                                                                                : GLFW_CURSOR_HIDDEN);
            }
        }
    }
}

void Context_window::capture_mouse(bool capture)
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        if (m_is_mouse_captured != capture)
        {
            m_is_mouse_captured = capture;
                glfwSetInputMode(window, GLFW_CURSOR, m_is_mouse_captured ? GLFW_CURSOR_DISABLED
                                                                          : m_is_window_visible ? GLFW_CURSOR_NORMAL
                                                                                                : GLFW_CURSOR_HIDDEN);
        }
    }
}

auto Context_window::is_mouse_captured() const
-> bool
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        int mode = glfwGetInputMode(window, GLFW_CURSOR);
        if (mode == GLFW_CURSOR_DISABLED)
        {
            return true;
        }
        if (mode == GLFW_CURSOR_NORMAL)
        {
            return false;
        }
        FATAL("unexpected GLFW_CURSOR_MODE");
    }
    return false;
}

auto Context_window::get_width() const
-> int
{
    int width{0};
    int height{0};
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwGetWindowSize(window, &width, &height);
    }
    return width;
}

auto Context_window::get_height() const
-> int
{
    int width{0};
    int height{0};
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwGetWindowSize(window, &width, &height);
    }
    return height;
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
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr)
    {
        glfwSwapBuffers(window);
    }
}

} // namespace erhe::toolkit

#endif // defined(ERHE_WINDOW_TOOLKIT_GLFW)
