#include "erhe_window/glfw_window.hpp"
#include "erhe_gl/dynamic_load.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_time/sleep.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/printf.h>
#include <GLFW/glfw3.h>

#if defined(_WIN32)
#   define GLFW_EXPOSE_NATIVE_WIN32 1
#   define GLFW_EXPOSE_NATIVE_WGL 1
#   include <GLFW/glfw3native.h>
#   include <GL/wglext.h>
#endif

#include <cstdlib>
#include <ctime>
#include <stdexcept>
#include <thread>

namespace gl {

using glproc = void (*)();

auto get_proc_address(const char* procname) -> glproc
{
    auto proc = glfwGetProcAddress(procname);
    return proc;
}

} // namespace gl

namespace erhe::window {

namespace {

auto glfw_key_to_erhe(const int glfw_key) -> Keycode
{
    switch (glfw_key) {
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
        default:                           return Key_unknown;
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
    switch (glfw_mouse_button) {
        case GLFW_MOUSE_BUTTON_LEFT:   return Mouse_button_left;
        case GLFW_MOUSE_BUTTON_MIDDLE: return Mouse_button_middle;
        case GLFW_MOUSE_BUTTON_RIGHT:  return Mouse_button_right;
        case GLFW_MOUSE_BUTTON_4:      return Mouse_button_x1;
        case GLFW_MOUSE_BUTTON_5:      return Mouse_button_x2;
        default:
            // TODO
            return Mouse_button_left;
    }
}

// erhe uses click count from mango:
//  0     = release
//  1     = press
//  wheel = signed int with direction and amount
auto glfw_mouse_button_action_to_erhe(const int glfw_mouse_button_action) -> bool
{
    switch (glfw_mouse_button_action) {
        case GLFW_PRESS:   return true;
        case GLFW_RELEASE: return false;
        default: return false; // TODO
    }
}

[[nodiscard]] auto glfw_key_to_modifier(int key) -> int
{
    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL) {
        return GLFW_MOD_CONTROL;
    }
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
        return GLFW_MOD_SHIFT;
    }
    if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT) {
        return GLFW_MOD_ALT;
    }
    if (key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER) {
        return GLFW_MOD_SUPER;
    }
    return 0;
}

void key_event_callback(GLFWwindow* glfw_window, const int key, const int scancode, const int action, const int glfw_modifiers)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_key_event(timestamp_ns, key, scancode, action, glfw_modifiers);
    }
}

void char_event_callback(GLFWwindow* glfw_window, const unsigned int codepoint)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_char_event(timestamp_ns, codepoint);
    }
}

void mouse_position_event_callback(GLFWwindow* glfw_window, double x, double y)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_mouse_move(timestamp_ns, x, y);
    }
}

void mouse_button_event_callback(GLFWwindow* glfw_window, const int button, const int action, const int mods)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_mouse_button_event(timestamp_ns, button, action, mods);
    }
}

void mouse_wheel_event_callback(GLFWwindow* glfw_window, const double x, const double y)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_mouse_wheel_event(timestamp_ns, x, y);
    }
}

void window_resize_event_callback(GLFWwindow* glfw_window, const int width, const int height)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_window_resize_event(timestamp_ns, width, height);
    }
}

void window_refresh_callback(GLFWwindow* glfw_window)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_window_refresh_event(timestamp_ns);
    }
}

void window_close_event_callback(GLFWwindow* glfw_window)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_window_close_event(timestamp_ns);
    }
}

void window_focus_event_callback(GLFWwindow* glfw_window, int focused)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_window_focus_event(timestamp_ns, focused);
    }
}

void window_cursor_enter_callback(GLFWwindow* glfw_window, int entered)
{
    const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto* const window = reinterpret_cast<Context_window*>(glfwGetWindowUserPointer(glfw_window));
    if (window != nullptr) {
        window->handle_cursor_enter_event(timestamp_ns, entered);
    }
}

} // namespace

auto Context_window::get_glfw_window() const -> GLFWwindow*
{
    return m_glfw_window;
}

int Context_window::s_window_count{0};

Context_window::Context_window(const Window_configuration& configuration)
{
    ERHE_PROFILE_FUNCTION();

    const bool ok = open(configuration);
    ERHE_VERIFY(ok);
}

Context_window::Context_window(Context_window* share)
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(share != nullptr);

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

[[nodiscard]] auto get_monitor(const int x, const int y) -> GLFWmonitor*
{
    int monitor_count{0};
    GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
    for (int i = 0; i < monitor_count; ++i) {
        int monitor_xpos  {0};
        int monitor_ypos  {0};
        int monitor_width {0};
        int monitor_height{0};
        glfwGetMonitorWorkarea(monitors[i], &monitor_xpos, &monitor_ypos, &monitor_width, &monitor_height);
        if ((x >= monitor_xpos) && (y >= monitor_ypos) && (x < monitor_xpos + monitor_width) && (y < monitor_ypos + monitor_height)) {
            return monitors[i];
        }
    }
    return glfwGetPrimaryMonitor();
}

// Currently this is not thread safe.
// For now, only call this from main thread.
auto Context_window::open(const Window_configuration& configuration) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (s_window_count == 0) {
        if (glfwInit() != GLFW_TRUE) {
            fputs("Failed to initialize GLFW\n", stderr);
            return false;
        }
    }

    const bool primary = (configuration.share == nullptr);

    // Scanning joysticks appears to be slow, so do it in worker thread
    if (primary && configuration.enable_joystick) {
        m_joystick_scan_task = std::thread{
            [this]() {
                ERHE_PROFILE_SCOPE("Scan joysticks");
                for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
                    ERHE_PROFILE_SCOPE("Joystick");
                    int present = glfwJoystickPresent(jid);
                    if (present) {
                        const char* name = glfwGetJoystickName(jid);
                        const char* guid = glfwGetJoystickGUID(jid);
                        int axis_count = 0;
                        const float* axes = glfwGetJoystickAxes(jid, &axis_count);
                        static_cast<void>(axes);
                        int button_count;
                        const unsigned char* buttons = glfwGetJoystickButtons(jid, &button_count);
                        static_cast<void>(buttons);
                        log_window->info(
                            "Joystick {}: name = {} GUID = {} axis_count = {} button_count = {}",
                            jid,
                            (name != nullptr) ? name : "",
                            (guid != nullptr) ? guid : "",
                            axis_count,
                            button_count
                        );
                    }
                }
                m_joystick_scan_done.store(true);
            }
        };
    };

    glfwWindowHint(GLFW_CLIENT_API,     GLFW_OPENGL_API);
    glfwWindowHint(GLFW_RED_BITS,       8);
    glfwWindowHint(GLFW_GREEN_BITS,     8);
    glfwWindowHint(GLFW_BLUE_BITS,      8);
    glfwWindowHint(GLFW_ALPHA_BITS,     8);
    glfwWindowHint(GLFW_DEPTH_BITS,     configuration.use_depth   ? 24 : 0);
    glfwWindowHint(GLFW_STENCIL_BITS,   configuration.use_stencil ?  8 : 0);

    glfwWindowHint(GLFW_SRGB_CAPABLE,     GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
    glfwWindowHint(GLFW_CENTER_CURSOR,    GLFW_TRUE); // Fullscreen only
    if (configuration.msaa_sample_count > 0) {
        glfwWindowHint(GLFW_SAMPLES, configuration.msaa_sample_count);
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, configuration.gl_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, configuration.gl_minor);
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

    bool fullscreen = configuration.fullscreen;
/// YOLO #if defined(_WIN32)
/// YOLO     if (IsDebuggerPresent() != 0) {
/// YOLO         fullscreen = false;
/// YOLO     }
/// YOLO #endif

    GLFWmonitor* monitor = fullscreen ? glfwGetPrimaryMonitor() : nullptr;

    if (fullscreen && (monitor != nullptr)) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        ERHE_PROFILE_SCOPE("glfwCreateWindow");
        m_glfw_window = glfwCreateWindow(mode->width, mode->height, configuration.title.data(), monitor, share_window);
    } else {
        ERHE_PROFILE_SCOPE("glfwCreateWindow");
        m_glfw_window = glfwCreateWindow(configuration.width, configuration.height, configuration.title.data(), monitor, share_window);
    }

    if (m_glfw_window == nullptr) {
        log_window->error("Failed to open GLFW window for GL {}.{}.", configuration.gl_major, configuration.gl_minor);
        if (s_window_count == 0) {
            glfwTerminate();
        }
        return false;
    }

    { // Center window
        int window_xpos  {0};
        int window_ypos  {0};
        int window_width {0};
        int window_height{0};
        glfwGetWindowPos(m_glfw_window, &window_xpos, &window_ypos);
        glfwGetWindowSize(m_glfw_window, &window_width, &window_height);

        monitor = get_monitor(window_xpos + window_width / 2, window_ypos + window_height / 2);
        int monitor_xpos  {0};
        int monitor_ypos  {0};
        int monitor_width {0};
        int monitor_height{0};
        glfwGetMonitorWorkarea(monitor, &monitor_xpos, &monitor_ypos, &monitor_width, &monitor_height);
        glfwSetWindowPos(
            m_glfw_window,
            monitor_xpos + (monitor_width / 2) - (window_width / 2),
            monitor_ypos + (monitor_height / 2) - (window_height / 2)
        );
    }

    s_window_count++;
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

    m_configuration = configuration;
    if (primary) {

        if (glfwRawMouseMotionSupported()) {
            log_window->info("Enabling GLFW_RAW_MOUSE_MOTION");
            m_use_raw_mouse = true;
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }

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

        if (m_configuration.show) {
            glfwShowWindow(window);
        }
        glfwMakeContextCurrent(window);

        bool is_swap_control_tear_supported = 
            (glfwExtensionSupported("WGL_EXT_swap_control_tear") == GLFW_TRUE) ||
            (glfwExtensionSupported("GLX_EXT_swap_control_tear") == GLFW_TRUE);
        if (is_swap_control_tear_supported) {
            log_window->info("EXT_swap_control_tear is supported");
        } else {
            log_window->info("EXT_swap_control_tear is not supported");
            m_configuration.swap_interval = std::min(0, m_configuration.swap_interval);
        }
        log_window->info("Setting swap interval to {}", m_configuration.swap_interval);
        glfwSwapInterval(configuration.swap_interval);
        if (s_window_count == 1) {
            get_extensions();
        }
    } else {
        for (Mouse_cursor cursor_n = 0; cursor_n < Mouse_cursor_COUNT; cursor_n++) {
            m_mouse_cursor[cursor_n] = nullptr;
        }
    }

    glfwGetCursorPos(window, &m_last_mouse_x, &m_last_mouse_y);

    return true;
}

Context_window::~Context_window() noexcept
{
    if (m_joystick_scan_task.joinable()) {
        m_joystick_scan_task.join();
    }

    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
        for (Mouse_cursor cursor_n = 0; cursor_n < Mouse_cursor_COUNT; cursor_n++) {
            glfwDestroyCursor(m_mouse_cursor[cursor_n]);
        }

        glfwDestroyWindow(window);
        --s_window_count;
        if (s_window_count == 0) {
            glfwTerminate();
        }
    }
}

void Context_window::poll_events(float wait_time)
{
    ERHE_PROFILE_FUNCTION();

    if (m_is_mouse_captured) {
        glfwSetCursorPos(m_glfw_window, m_mouse_capture_xpos, m_mouse_capture_ypos);
    }

    if (wait_time > 0.0f) {
        ERHE_PROFILE_SCOPE("wait");
        glfwWaitEventsTimeout(wait_time);
    } else {
        ERHE_PROFILE_SCOPE("poll");
        glfwPollEvents();
    }

    if (m_joystick_scan_done.load()) {
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            int present = glfwJoystickPresent(jid);
            if (present) {
                int axis_count = 0;
                int button_count = 0;
                const float* axis_values = glfwGetJoystickAxes(jid, &axis_count);
                const unsigned char* button_values = glfwGetJoystickButtons(jid, &button_count);
                const int64_t timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
                if (axis_count > m_controller_axis_values.size()) {
                    m_controller_axis_values.resize(axis_count);
                }
                for (int i = 0; i < axis_count; ++i) {
                    const float value = axis_values[i];
                    // TODO This was somewhat unexpected: At least space navigator
                    //      reports *cumulative* values for each axis, instead of
                    //      the current physical state of the controller.
                    const float delta = value - m_controller_axis_values[i];
                    if (delta != 0.0f) {
                        m_controller_axis_values[i] = value;
                        m_input_events[m_input_event_queue_write].push_back(
                            Input_event{
                                .type = Input_event_type::controller_axis_event,
                                .timestamp_ns = timestamp_ns,
                                .u = {
                                    .controller_axis_event = {
                                        .controller    = jid,
                                        .axis          = i,
                                        .value         = delta,
                                        .modifier_mask = get_modifier_mask()
                                    }
                                }
                            }
                        );
                    }
                }
                if (button_count > m_controller_button_values.size()) {
                    m_controller_button_values.resize(button_count);
                }
                for (int i = 0; i < button_count; ++i) {
                    const bool value = button_values[i] != 0;
                    if (value != m_controller_button_values[i]) {
                        m_controller_button_values[i] = value;
                        m_input_events[m_input_event_queue_write].push_back(
                            Input_event{
                                .type = Input_event_type::controller_button_event,
                                .timestamp_ns = timestamp_ns,
                                .u = {
                                    .controller_button_event = {
                                        .controller    = jid,
                                        .button        = i,
                                        .value         = value,
                                        .modifier_mask = get_modifier_mask()
                                    }
                                }
                            }
                        );
                    }
                }
            }
        }
    }

    if (m_input_event_synthesizer_callback) {
        m_input_event_synthesizer_callback(*this);
    }

    // Swap input event buffers
    int old_read_buffer = 1 - m_input_event_queue_write;
    // for (const Input_event& input_event : m_input_events[old_read_buffer]) {
    //     if (input_event.handled) {
    //         continue;
    //     }
    //     if (input_event.type == Input_event_type::window_focus_event) {
    //         log_window_event->trace("Not handled: {}", input_event.describe());
    //     }
    // }
    m_input_events[old_read_buffer].clear();
    m_input_event_queue_write = old_read_buffer;
}

void Context_window::inject_input_event(const Input_event& event)
{
    m_input_events[m_input_event_queue_write].push_back(event);
}

void Context_window::set_input_event_synthesizer_callback(std::function<void(Context_window& context_window)> callback)
{
    m_input_event_synthesizer_callback = callback;
}

void Context_window::get_cursor_position(float& xpos, float& ypos)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
        double xpos_double{0.0};
        double ypos_double{0.0};
        glfwGetCursorPos(window, &xpos_double, &ypos_double);
        xpos = static_cast<float>(xpos_double);
        ypos = static_cast<float>(ypos_double);
    }
}

void Context_window::get_cursor_relative_hold_position(float& xpos, float& ypos)
{
    xpos = static_cast<float>(m_mouse_capture_xpos);
    ypos = static_cast<float>(m_mouse_capture_ypos);
}

void Context_window::set_visible(const bool visible)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
        if (m_is_window_visible != visible) {
            m_is_window_visible = visible;
            if (m_is_window_visible) {
                glfwShowWindow(window);
            } else {
                glfwHideWindow(window);
            }
        }
    }
}

void Context_window::set_cursor(const Mouse_cursor cursor)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window == nullptr) {
        return;
    }
    if (m_current_mouse_cursor == cursor) {
        return;
    }

    glfwSetCursor(
        window,
        m_mouse_cursor[cursor] != nullptr
            ? m_mouse_cursor[cursor]
            : m_mouse_cursor[Mouse_cursor_Arrow]
    );
    m_current_mouse_cursor = cursor;
    if (!m_is_mouse_captured) {
        glfwSetInputMode(
            window, GLFW_CURSOR,
            (m_current_mouse_cursor != Mouse_cursor_None)
                ? GLFW_CURSOR_NORMAL
                : GLFW_CURSOR_HIDDEN
        );
    }
}

void Context_window::set_cursor_relative_hold(const bool capture)
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
        if (capture) {
            glfwGetCursorPos(window, &m_mouse_capture_xpos, &m_mouse_capture_ypos);
            m_mouse_virtual_xpos = m_mouse_capture_xpos;
            m_mouse_virtual_ypos = m_mouse_capture_ypos;
        }
        if (m_is_mouse_captured != capture) {
            m_is_mouse_captured = capture;
            const int hidden_value = m_use_raw_mouse ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_HIDDEN;
            glfwSetInputMode(
                window,
                GLFW_CURSOR,
                m_is_mouse_captured
                    ? hidden_value
                    : (m_current_mouse_cursor != Mouse_cursor_None)
                        ? GLFW_CURSOR_NORMAL
                        : hidden_value
            );
            if (m_use_raw_mouse) {
                glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, m_is_mouse_captured ? GLFW_TRUE : GLFW_FALSE);
            }
        }
    }
}

auto Context_window::get_modifier_mask() const -> Key_modifier_mask
{
    return glfw_modifiers_to_erhe(m_glfw_key_modifiers);
}

void Context_window::handle_key_event(int64_t timestamp_ns, int key, int scancode, int action, int glfw_modifiers)
{
    static_cast<void>(scancode);

    m_glfw_key_modifiers = glfw_modifiers;

    switch (action) {
        case GLFW_PRESS:
        case GLFW_RELEASE: {
            m_input_events[m_input_event_queue_write].push_back(
                Input_event{
                    .type = Input_event_type::key_event,
                    .timestamp_ns = timestamp_ns,
                    .u = {
                        .key_event = {
                            .keycode       = glfw_key_to_erhe(key),
                            .modifier_mask = glfw_modifiers_to_erhe(glfw_modifiers),
                            .pressed       = (action == GLFW_PRESS)
                        }
                    }
                }
            );
            break;
        }

        case GLFW_REPEAT:
        default:
            break;
    }
}

void Context_window::handle_window_resize_event(int64_t timestamp_ns, int width, int height)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_resize_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .window_resize_event = {
                    .width  = width,
                    .height = height
                }
            }
        }
    );
}

void Context_window::handle_window_refresh_event(int64_t timestamp_ns)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_refresh_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .window_refresh_event = {}
            }
        }
    );
}

void Context_window::handle_window_close_event(int64_t timestamp_ns)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_close_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .window_close_event = {}
            }
        }
    );
}

void Context_window::handle_window_focus_event(int64_t timestamp_ns, int focused)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_focus_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .window_focus_event = {
                    .focused = (focused != 0)
                }
            }
        }
    );
    log_window_event->trace(m_input_events[m_input_event_queue_write].back().describe());
}

void Context_window::handle_cursor_enter_event(int64_t timestamp_ns, int entered)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::cursor_enter_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .cursor_enter_event = {
                    .entered = entered
                }
            }
        }
    );
    log_window_event->trace(m_input_events[m_input_event_queue_write].back().describe());
}

void Context_window::handle_char_event(int64_t timestamp_ns, unsigned int codepoint)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::char_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .char_event = {
                    .codepoint = codepoint
                }
            }
        }
    );
}

void Context_window::handle_mouse_button_event(int64_t timestamp_ns, int button, int action, int glfw_modifiers)
{
    m_glfw_key_modifiers = glfw_modifiers;

    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::mouse_button_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .mouse_button_event = {
                    .button        = glfw_mouse_button_to_erhe(button),
                    .pressed       = glfw_mouse_button_action_to_erhe(action),
                    .modifier_mask = glfw_modifiers_to_erhe(glfw_modifiers)
                }
            }
        }
    );
}

void Context_window::handle_mouse_wheel_event(int64_t timestamp_ns, double x, double y)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::mouse_wheel_event,
            .timestamp_ns = timestamp_ns,
            .u = {
                .mouse_wheel_event = {
                    .x             = static_cast<float>(x),
                    .y             = static_cast<float>(y),
                    .modifier_mask = get_modifier_mask()
                }
            }
        }
    );
}

void Context_window::handle_mouse_move(int64_t timestamp_ns, double x, double y)
{
    if (m_is_mouse_captured) {
        double dx = x - m_mouse_capture_xpos;
        double dy = y - m_mouse_capture_ypos;
        m_input_events[m_input_event_queue_write].push_back(
            Input_event{
                .type = Input_event_type::mouse_move_event,
                .timestamp_ns = timestamp_ns,
                .u = {
                    .mouse_move_event = {
                        .x             = static_cast<float>(m_mouse_capture_xpos),
                        .y             = static_cast<float>(m_mouse_capture_ypos),
                        .dx            = static_cast<float>(dx),
                        .dy            = static_cast<float>(dy),
                        .modifier_mask = get_modifier_mask()
                    }
                }
            }
        );
    } else {
        double dx = x - m_last_mouse_x;
        double dy = y - m_last_mouse_y;
        m_last_mouse_x = x;
        m_last_mouse_y = y;
        m_input_events[m_input_event_queue_write].push_back(
            Input_event{
                .type = Input_event_type::mouse_move_event,
                .timestamp_ns = timestamp_ns,
                .u = {
                    .mouse_move_event = {
                        .x             = static_cast<float>(x),
                        .y             = static_cast<float>(y),
                        .dx            = static_cast<float>(dx),
                        .dy            = static_cast<float>(dy),
                        .modifier_mask = get_modifier_mask()
                    }
                }
            }
        );
    }
}

auto Context_window::get_cursor_relative_hold() const -> bool
{
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
        const int mode = glfwGetInputMode(window, GLFW_CURSOR);
        if (mode == GLFW_CURSOR_CAPTURED) {
            return true;
        }
        if (mode == GLFW_CURSOR_NORMAL) {
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
    if (window != nullptr) {
        glfwGetWindowSize(window, &width, &height);
    }
    return width;
}

auto Context_window::get_height() const -> int
{
    int width {0};
    int height{0};
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
        glfwGetWindowSize(window, &width, &height);
    }
    return height;
}

auto Context_window::get_input_events() -> std::vector<Input_event>&
{
    return m_input_events[1 - m_input_event_queue_write];
}

void Context_window::get_extensions()
{
    ERHE_PROFILE_FUNCTION();
    gl::dynamic_load_init(glfwGetProcAddress);

#if defined(_WIN32)
    m_NV_delay_before_swap = glfwGetProcAddress("wglDelayBeforeSwapNV");
#else // TODO
#endif
}

void Context_window::make_current() const
{
    auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
        const auto* const already_current_context = glfwGetCurrentContext();
        if (already_current_context != nullptr) {
            log_window->error("context is already current");
        }
        glfwMakeContextCurrent(window);
    }
}

void Context_window::clear_current() const
{
    glfwMakeContextCurrent(nullptr);
}

auto Context_window::delay_before_swap(float /*seconds*/) const -> bool
{
#if defined(_WIN32)
    if (m_NV_delay_before_swap != nullptr) {
        auto* window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
        //HGLRC glrc = glfwGetWGLContext(window);
        HDC dc = GetDC(glfwGetWin32Window(window));

        PFNWGLDELAYBEFORESWAPNVPROC p_WGL_NV_delay_before_swap = (PFNWGLDELAYBEFORESWAPNVPROC)(m_NV_delay_before_swap);
        BOOL return_value = p_WGL_NV_delay_before_swap(dc, seconds);
        return return_value == TRUE;
    } else {
        return false;
    }
#else
    // TODO
    return false;
#endif
}

void Context_window::swap_buffers() const
{
    ERHE_PROFILE_FUNCTION();
    auto* const window = reinterpret_cast<GLFWwindow*>(m_glfw_window);
    if (window != nullptr) {
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

#if defined(_WIN32)
auto Context_window::get_hwnd() const -> HWND
{
    return glfwGetWin32Window(m_glfw_window);
}
auto Context_window::get_hglrc() const -> HGLRC
{
    return glfwGetWGLContext(m_glfw_window);
}
#endif

auto Context_window::get_window_handle() const -> void*
{
#if defined(_WIN32)
    return glfwGetWin32Window(m_glfw_window);
#else
    return nullptr; // TODO
#endif
}

auto Context_window::get_scale_factor() const -> float
{
    // TODO Use glfwSetWindowContentScaleCallback()

    int window_xpos  {0};
    int window_ypos  {0};
    int window_width {0};
    int window_height{0};
    glfwGetWindowPos(m_glfw_window, &window_xpos, &window_ypos);
    glfwGetWindowSize(m_glfw_window, &window_width, &window_height);

    GLFWmonitor* monitor = get_monitor(window_xpos + window_width / 2, window_ypos + window_height / 2);
    float x_scale = 1.0f;
    float y_scale = 1.0f;
    glfwGetMonitorContentScale(monitor, &x_scale, &y_scale);
    return (x_scale + y_scale) / 2.0f;
}

void Context_window::set_text_input_area(int x, int y, int w, int h)
{
    static_cast<void>(x);
    static_cast<void>(y);
    static_cast<void>(w);
    static_cast<void>(h);
}

void Context_window::start_text_input()
{
}

void Context_window::stop_text_input()
{
}

}
