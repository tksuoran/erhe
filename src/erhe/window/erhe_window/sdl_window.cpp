#include "erhe_window/sdl_window.hpp"
#include "erhe_gl/dynamic_load.hpp"
#include "erhe_window/window_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_time/sleep.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/printf.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#if defined(_WIN32)
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
    auto proc = SDL_GL_GetProcAddress(procname);
    return proc;
}

} // namespace gl

namespace erhe::window {

namespace {

auto sdl_key_to_erhe(const SDL_Keycode sdl_key) -> Keycode
{
    switch (sdl_key) {
        case SDLK_SPACE              : return Key_space;
        case SDLK_APOSTROPHE         : return Key_apostrophe;
        case SDLK_COMMA              : return Key_comma;
        case SDLK_MINUS              : return Key_minus;
        case SDLK_PERIOD             : return Key_period;
        case SDLK_SLASH              : return Key_slash;
        case SDLK_0                  : return Key_0;
        case SDLK_1                  : return Key_1;
        case SDLK_2                  : return Key_2;
        case SDLK_3                  : return Key_3;
        case SDLK_4                  : return Key_4;
        case SDLK_5                  : return Key_5;
        case SDLK_6                  : return Key_6;
        case SDLK_7                  : return Key_7;
        case SDLK_8                  : return Key_8;
        case SDLK_9                  : return Key_9;
        case SDLK_SEMICOLON          : return Key_semicolon;
        case SDLK_EQUALS             : return Key_equal;
        case SDLK_A                  : return Key_a;
        case SDLK_B                  : return Key_b;
        case SDLK_C                  : return Key_c;
        case SDLK_D                  : return Key_d;
        case SDLK_E                  : return Key_e;
        case SDLK_F                  : return Key_f;
        case SDLK_G                  : return Key_g;
        case SDLK_H                  : return Key_h;
        case SDLK_I                  : return Key_i;
        case SDLK_J                  : return Key_j;
        case SDLK_K                  : return Key_k;
        case SDLK_L                  : return Key_l;
        case SDLK_M                  : return Key_m;
        case SDLK_N                  : return Key_n;
        case SDLK_O                  : return Key_o;
        case SDLK_P                  : return Key_p;
        case SDLK_Q                  : return Key_q;
        case SDLK_R                  : return Key_r;
        case SDLK_S                  : return Key_s;
        case SDLK_T                  : return Key_t;
        case SDLK_U                  : return Key_u;
        case SDLK_V                  : return Key_v;
        case SDLK_W                  : return Key_w;
        case SDLK_X                  : return Key_x;
        case SDLK_Y                  : return Key_y;
        case SDLK_Z                  : return Key_z;
        case SDLK_LEFTBRACKET        : return Key_left_bracket;
        case SDLK_BACKSLASH          : return Key_backslash;
        case SDLK_RIGHTBRACKET       : return Key_right_bracket;
        case SDLK_GRAVE              : return Key_grave_accent;
        //case SDLK_WORLD_1            : return Key_world_1;
        //case SDLK_WORLD_2            : return Key_world_2;
        case SDLK_ESCAPE             : return Key_escape;
        case SDLK_RETURN             : return Key_enter;
        case SDLK_TAB                : return Key_tab;
        case SDLK_BACKSPACE          : return Key_backspace;
        case SDLK_INSERT             : return Key_insert;
        case SDLK_DELETE             : return Key_delete;
        case SDLK_RIGHT              : return Key_right;
        case SDLK_LEFT               : return Key_left;
        case SDLK_DOWN               : return Key_down;
        case SDLK_UP                 : return Key_up;
        case SDLK_PAGEUP             : return Key_page_up;
        case SDLK_PAGEDOWN           : return Key_page_down;
        case SDLK_HOME               : return Key_home;
        case SDLK_END                : return Key_end;
        case SDLK_CAPSLOCK           : return Key_caps_lock;
        case SDLK_SCROLLLOCK         : return Key_scroll_lock;
        case SDLK_NUMLOCKCLEAR       : return Key_num_lock;
        case SDLK_PRINTSCREEN        : return Key_print_screen;
        case SDLK_PAUSE              : return Key_pause;
        case SDLK_F1                 : return Key_f1;
        case SDLK_F2                 : return Key_f2;
        case SDLK_F3                 : return Key_f3;
        case SDLK_F4                 : return Key_f4;
        case SDLK_F5                 : return Key_f5;
        case SDLK_F6                 : return Key_f6;
        case SDLK_F7                 : return Key_f7;
        case SDLK_F8                 : return Key_f8;
        case SDLK_F9                 : return Key_f9;
        case SDLK_F10                : return Key_f10;
        case SDLK_F11                : return Key_f11;
        case SDLK_F12                : return Key_f12;
        case SDLK_F13                : return Key_f13;
        case SDLK_F14                : return Key_f14;
        case SDLK_F15                : return Key_f15;
        case SDLK_F16                : return Key_f16;
        case SDLK_F17                : return Key_f17;
        case SDLK_F18                : return Key_f18;
        case SDLK_F19                : return Key_f19;
        case SDLK_F20                : return Key_f20;
        case SDLK_F21                : return Key_f21;
        case SDLK_F22                : return Key_f22;
        case SDLK_F23                : return Key_f23;
        case SDLK_F24                : return Key_f24;
        //case SDLK_F25                : return Key_f25;
        case SDLK_KP_0               : return Key_kp_0;
        case SDLK_KP_1               : return Key_kp_1;
        case SDLK_KP_2               : return Key_kp_2;
        case SDLK_KP_3               : return Key_kp_3;
        case SDLK_KP_4               : return Key_kp_4;
        case SDLK_KP_5               : return Key_kp_5;
        case SDLK_KP_6               : return Key_kp_6;
        case SDLK_KP_7               : return Key_kp_7;
        case SDLK_KP_8               : return Key_kp_8;
        case SDLK_KP_9               : return Key_kp_9;
        case SDLK_KP_DECIMAL         : return Key_kp_decimal;
        case SDLK_KP_DIVIDE          : return Key_kp_divide;
        case SDLK_KP_MULTIPLY        : return Key_kp_multiply;
        case SDLK_KP_MINUS           : return Key_kp_subtract;
        case SDLK_KP_PLUS            : return Key_kp_add;
        case SDLK_KP_ENTER           : return Key_kp_enter;
        case SDLK_KP_EQUALS          : return Key_kp_equal;
        case SDLK_LSHIFT             : return Key_left_shift;
        case SDLK_LCTRL              : return Key_left_control;
        case SDLK_LALT               : return Key_left_alt;
        case SDLK_LGUI               : return Key_left_super;
        case SDLK_RSHIFT             : return Key_right_shift;
        case SDLK_RCTRL              : return Key_right_control;
        case SDLK_RALT               : return Key_right_alt;
        case SDLK_RGUI               : return Key_right_super;
        case SDLK_MENU               : return Key_menu;
        case SDLK_UNKNOWN            : return Key_unknown;
        default:                       return Key_unknown;
    }
}

auto sdl_modifiers_to_erhe(const unsigned int sdl_modifiers) -> Key_modifier_mask
{
    uint32_t mask = 0;
    // TODO GLFW_MOD_CAPS_LOCK
    // TODO GLFW_MOD_NUM_LOCK
    if (sdl_modifiers & SDL_KMOD_CTRL ) mask |= Key_modifier_bit_ctrl;
    if (sdl_modifiers & SDL_KMOD_SHIFT) mask |= Key_modifier_bit_shift;
    if (sdl_modifiers & SDL_KMOD_GUI  ) mask |= Key_modifier_bit_super;
    if (sdl_modifiers & SDL_KMOD_ALT  ) mask |= Key_modifier_bit_menu;
    return mask;
}

auto sdl_mouse_button_to_erhe(const int sdl_mouse_button) -> Mouse_button
{
    switch (sdl_mouse_button) {
        case SDL_BUTTON_LEFT:   return Mouse_button_left;
        case SDL_BUTTON_MIDDLE: return Mouse_button_middle;
        case SDL_BUTTON_RIGHT:  return Mouse_button_right;
        case SDL_BUTTON_X1:     return Mouse_button_x1;
        case SDL_BUTTON_X2:     return Mouse_button_x2;
        default: {
            // TODO
            return Mouse_button_left;
        }
    }
}

[[nodiscard]] auto sdl_key_to_modifier(int key) -> int
{
    if (key == SDLK_LCTRL || key == SDLK_RCTRL) {
        return SDL_KMOD_CTRL;
    }
    if (key == SDLK_LSHIFT || key == SDLK_RSHIFT) {
        return SDL_KMOD_SHIFT;
    }
    if (key == SDLK_LALT || key == SDLK_RALT) {
        return SDL_KMOD_ALT;
    }
    if (key == SDLK_LGUI || key == SDLK_RGUI) {
        return SDL_KMOD_GUI;
    }
    return 0;
}


} // namespace

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


// Currently this is not thread safe.
// For now, only call this from main thread.
auto Context_window::open(const Window_configuration& configuration) -> bool
{
    ERHE_PROFILE_FUNCTION();

    if (s_window_count == 0) {
        SDL_InitFlags init_flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
        if (configuration.enable_joystick) {
            init_flags |= SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD;
        }

        bool sdl_init_ok = SDL_Init(init_flags);
        if (!sdl_init_ok) {
            fputs("Failed to initialize SDL\n", stderr);
            return false;
        }
    }

    const bool primary = (configuration.share == nullptr);

    // Scanning joysticks can be slow, so do it in worker thread
    if (primary && configuration.enable_joystick) {
        m_joystick_scan_task = std::thread{
            [this]() {
                ERHE_PROFILE_SCOPE("Scan joysticks");
                int joystick_count{0};
                SDL_JoystickID* joysticks = SDL_GetJoysticks(&joystick_count);
                if ((joystick_count > 0) && (joysticks != nullptr)) {
                    for (int i = 0; i < joystick_count; ++i) {
                        ERHE_PROFILE_SCOPE("Joystick");
                        SDL_JoystickID id = joysticks[i];
                        if (id == 0) {
                            break;
                        }
                        SDL_Joystick* joystick = SDL_OpenJoystick(id); // TODO close?
                        if (joystick == nullptr) {
                            continue;
                        }
                        const char* name         = SDL_GetJoystickName(joystick);
                        Uint16      vendor       = SDL_GetJoystickVendor(joystick);
                        Uint16      product      = SDL_GetJoystickProduct(joystick);
                        int         axis_count   = SDL_GetNumJoystickAxes(joystick);
                        int         button_count = SDL_GetNumJoystickButtons(joystick);

                        if (m_joystick_info.size() <= id) {
                            m_joystick_info.resize(id + 8);
                        }
                        log_window->info(
                            "Joystick id = {} name = {} vendor = {:04x} product = {:04x} axis_count = {} button_count = {}",
                            id,
                            (name != nullptr) ? name : "",
                            vendor,
                            product,
                            axis_count,
                            button_count
                        );
                    }
                }
                m_joystick_scan_done.store(true);
            }
        };
    };

    SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (configuration.fullscreen) { 
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (configuration.framebuffer_transparency) {
        window_flags |= SDL_WINDOW_TRANSPARENT;
    }
    if (!primary) {
        window_flags |= SDL_WINDOW_HIDDEN;
        window_flags |= SDL_WINDOW_NOT_FOCUSABLE;
    }

    SDL_Window* sdl_window = SDL_CreateWindow(configuration.title.c_str(), configuration.width, configuration.height, window_flags);
    m_sdl_window = sdl_window;

    if (m_sdl_window == nullptr) {
        log_window->error("Failed to open SDL window for GL {}.{}.", configuration.gl_major, configuration.gl_minor);
        if (s_window_count == 0) {
            SDL_Quit();
        }
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,       8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,     8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,      8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,     8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,     configuration.use_depth   ? 24 : 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,   configuration.use_stencil ?  8 : 0);

    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);
    if (configuration.msaa_sample_count > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, configuration.msaa_sample_count);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, configuration.gl_major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, configuration.gl_minor);
#if !defined(NDEBUG)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,         SDL_GL_CONTEXT_DEBUG_FLAG | SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,         0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
#endif

    if (configuration.share != nullptr) {
        configuration.share->make_current();
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    } else {
        SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    }

    SDL_GLContext sdl_context = SDL_GL_CreateContext(sdl_window);

    m_sdl_gl_context = sdl_context;
    if (primary) {
        SDL_GL_MakeCurrent(sdl_window, sdl_context);
        get_extensions();
        SDL_ShowWindow(sdl_window);
    }

    s_window_count++;
    m_is_mouse_relative_hold_enabled = false;
    m_configuration = configuration;
    SDL_GetMouseState(&m_last_mouse_x, &m_last_mouse_y);
    return true;
}

Context_window::~Context_window() noexcept
{
    if (m_joystick_scan_task.joinable()) {
        m_joystick_scan_task.join();
    }

    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_DestroyWindow(window);
        --s_window_count;
        if (s_window_count == 0) {
            SDL_Quit();
        }
    }
}

void Context_window::poll_events(float wait_time)
{
    ERHE_PROFILE_FUNCTION();

    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window == nullptr) {
        return;
    }

    if (m_is_mouse_relative_hold_enabled) {
        SDL_WarpMouseInWindow(window, m_mouse_relative_hold_xpos, m_mouse_relative_hold_ypos);
    }
    static_cast<void>(wait_time);

    //if (wait_time > 0.0f) {
    // ERHE_PROFILE_SCOPE("wait");
    // TODO SDL_WaitEventTimeoutNS()
    //double  wait_time_ns_ = static_cast<double>(wait_time) * 1'000'000'000;
    //int64_t wait_time_ns  = static_cast<int64_t>(wait_time_ns_);
    SDL_Event poll_event{};
    while (SDL_PollEvent(&poll_event)) {
        const int64_t timestamp = static_cast<int64_t>(poll_event.common.timestamp);
        switch (poll_event.type) {
            case SDL_EVENT_MOUSE_MOTION: {
                //// log_window_event->info("SDL_EVENT_MOUSE_MOTION x = {}, y = {}", poll_event.motion.x, poll_event.motion.y);
                handle_mouse_move(timestamp, poll_event.motion.x, poll_event.motion.y, poll_event.motion.xrel, poll_event.motion.yrel);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                //// log_window_event->info("SDL_MouseButtonEvent button = {}, down = {}", poll_event.button.button, poll_event.button.down);
                handle_mouse_button_event(timestamp, poll_event.button.button, poll_event.button.down);
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                //// log_window_event->info("SDL_EVENT_MOUSE_WHEEL x = {}, y = {}", poll_event.wheel.x, poll_event.wheel.y);
                handle_mouse_wheel_event(timestamp, poll_event.wheel.x, poll_event.wheel.y);
                break;
            }
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                //// log_window_event->info("SDL_KeyboardEvent key = {}, down = {}, mod = {}", poll_event.key.key, poll_event.key.down, poll_event.key.mod);
                handle_key_event(timestamp, poll_event.key.key, poll_event.key.scancode, poll_event.key.down, poll_event.key.mod);
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                //// log_window_event->info("SDL_EVENT_TEXT_INPUT text = {}", poll_event.text.text);
                handle_text_event(timestamp, poll_event.text.text);
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST: {
                const bool gained = poll_event.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
                //// log_window_event->info("{}", gained ? "SDL_EVENT_WINDOW_FOCUS_GAINED" : "SDL_EVENT_WINDOW_FOCUS_LOST");
                handle_window_focus_event(timestamp, gained);
                break;
            }
            case SDL_EVENT_WINDOW_MOUSE_ENTER:
            case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
                const bool entered = poll_event.type == SDL_EVENT_WINDOW_MOUSE_ENTER;
                //// log_window_event->info("{}", entered ? "SDL_EVENT_WINDOW_MOUSE_ENTER" : "SDL_EVENT_WINDOW_MOUSE_LEAVE");
                handle_cursor_enter_event(timestamp, entered);
                break;
            }
            case SDL_EVENT_WINDOW_EXPOSED: {
                //// log_window_event->info("SDL_EVENT_WINDOW_EXPOSED");
                handle_window_refresh_event(timestamp);
                break;
            }
            case SDL_EVENT_WINDOW_RESIZED: {
                //// log_window_event->info("SDL_EVENT_WINDOW_RESIZED {} x {}", poll_event.window.data1, poll_event.window.data2);
                handle_window_resize_event(timestamp, poll_event.window.data1, poll_event.window.data2);
                break;
            }
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                //// log_window_event->info("SDL_EVENT_WINDOW_CLOSE_REQUESTED");
                handle_window_close_event(timestamp);
                break;
            }
            case SDL_EVENT_JOYSTICK_AXIS_MOTION: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_AXIS_MOTION axis = {} value = {}", poll_event.jaxis.axis, poll_event.jaxis.value);
                handle_controller_axis_event(timestamp, poll_event.jaxis.which, poll_event.jaxis.axis, poll_event.jaxis.value);
                break;
            };
            case SDL_EVENT_JOYSTICK_BALL_MOTION: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_BALL_MOTION");
                break;
            };
            case SDL_EVENT_JOYSTICK_HAT_MOTION: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_HAT_MOTION");
                break;
            };
            case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
            case SDL_EVENT_JOYSTICK_BUTTON_UP: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_BUTTON button = {} down = {}", poll_event.jbutton.button, poll_event.jbutton.down);
                handle_controller_button_event(timestamp, poll_event.jbutton.which, poll_event.jbutton.button, poll_event.jbutton.down);
                break;
            };
            case SDL_EVENT_JOYSTICK_ADDED: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_ADDED device = {}", poll_event.jdevice.which);
                break;
            };
            case SDL_EVENT_JOYSTICK_REMOVED: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_REMOVED");
                break;
            };
            case SDL_EVENT_JOYSTICK_BATTERY_UPDATED: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_BATTERY_UPDATED power_level = {}", poll_event.jbattery.percent);
                break;
            };
            case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE: {
                //// log_window_event->info("SDL_EVENT_JOYSTICK_UPDATE_COMPLETE");
                break;
            };
            default: {
                break;
            }
        }
    }


    //    glfwWaitEventsTimeout(wait_time);
    //} else {
    //    ERHE_PROFILE_SCOPE("poll");
    //    glfwPollEvents();
    //}

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
    SDL_GetMouseState(&xpos, &ypos);
}

void Context_window::get_cursor_relative_hold_position(float& xpos, float& ypos)
{
    xpos = static_cast<float>(m_mouse_relative_hold_xpos);
    ypos = static_cast<float>(m_mouse_relative_hold_ypos);
}

void Context_window::set_visible(const bool visible)
{
    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        if (m_is_window_visible != visible) {
            m_is_window_visible = visible;
            if (m_is_window_visible) {
                SDL_ShowWindow(window);
            } else {
                SDL_HideWindow(window);
            }
        }
    }
}

void Context_window::set_cursor(const Mouse_cursor cursor)
{
    static_cast<void>(cursor);
}

void Context_window::set_cursor_relative_hold(const bool relative_hold_enabled)
{    
    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) 
    {
        if (m_is_mouse_relative_hold_enabled != relative_hold_enabled) {
            SDL_SetWindowRelativeMouseMode(window, relative_hold_enabled);
            if (relative_hold_enabled) {
                SDL_GetMouseState(&m_mouse_relative_hold_xpos, &m_mouse_relative_hold_ypos);
                m_mouse_virtual_xpos = m_mouse_relative_hold_xpos;
                m_mouse_virtual_ypos = m_mouse_relative_hold_ypos;
            }
            m_is_mouse_relative_hold_enabled = relative_hold_enabled;
        }
    }
}

void Context_window::set_text_input_area(int x, int y, int w, int h)
{
    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_Rect r;
        r.x = x;
        r.y = y;
        r.w = w;
        r.h = h;
        log_window_event->info("SDL_SetTextInputArea({}, {}, {}, {})", x, y, w, h);
        SDL_SetTextInputArea(window, &r, 0);
    }
}

void Context_window::start_text_input()
{
    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        log_window_event->info("SDL_StartTextInput()");
        SDL_StartTextInput(window);
    }
}

void Context_window::stop_text_input()
{
    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        log_window_event->info("SDL_StopTextInput()");
        SDL_StopTextInput(window);
    }
}

auto Context_window::get_modifier_mask() const -> Key_modifier_mask
{
    return sdl_modifiers_to_erhe(m_key_modifiers);
}

void Context_window::handle_key_event(int64_t timestamp, int key, int scancode, bool pressed, int modifiers)
{
    static_cast<void>(scancode);

    m_key_modifiers = modifiers;

    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::key_event,
            .timestamp_ns = timestamp,
            .u = {
                .key_event = {
                    .keycode       = sdl_key_to_erhe(key),
                    .modifier_mask = sdl_modifiers_to_erhe(modifiers),
                    .pressed       = pressed
                }
            }
        }
    );
}

void Context_window::handle_text_event(int64_t timestamp, const char* utf8_text)
{
    Input_event text_event{
        .type = Input_event_type::text_event,
        .timestamp_ns = timestamp
    };
    memset(text_event.u.text_event.utf8_text, 0, 32);
    ERHE_VERIFY(strlen(utf8_text) < 32);
    strncpy(text_event.u.text_event.utf8_text, utf8_text, 31);
    m_input_events[m_input_event_queue_write].push_back(text_event);
}

void Context_window::handle_window_resize_event(int64_t timestamp, int width, int height)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_resize_event,
            .timestamp_ns = timestamp,
            .u = {
                .window_resize_event = {
                    .width  = width,
                    .height = height
                }
            }
        }
    );
}

void Context_window::handle_window_refresh_event(int64_t timestamp)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_refresh_event,
            .timestamp_ns = timestamp,
            .u = {
                .window_refresh_event = {}
            }
        }
    );
}

void Context_window::handle_window_close_event(int64_t timestamp)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_close_event,
            .timestamp_ns = timestamp,
            .u = {
                .window_close_event = {}
            }
        }
    );
}

void Context_window::handle_window_focus_event(int64_t timestamp, bool focused)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::window_focus_event,
            .timestamp_ns = timestamp,
            .u = {
                .window_focus_event = {
                    .focused = (focused != 0)
                }
            }
        }
    );
    log_window_event->trace(m_input_events[m_input_event_queue_write].back().describe());
}

void Context_window::handle_cursor_enter_event(int64_t timestamp, bool entered)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::cursor_enter_event,
            .timestamp_ns = timestamp,
            .u = {
                .cursor_enter_event = {
                    .entered = entered
                }
            }
        }
    );
    log_window_event->trace(m_input_events[m_input_event_queue_write].back().describe());
}

void Context_window::handle_mouse_button_event(int64_t timestamp, int button, bool pressed)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::mouse_button_event,
            .timestamp_ns = timestamp,
            .u = {
                .mouse_button_event = {
                    .button        = sdl_mouse_button_to_erhe(button),
                    .pressed       = pressed,
                    .modifier_mask = sdl_modifiers_to_erhe(m_key_modifiers)
                }
            }
        }
    );
}

void Context_window::handle_mouse_wheel_event(int64_t timestamp, float x, float y)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::mouse_wheel_event,
            .timestamp_ns = timestamp,
            .u = {
                .mouse_wheel_event = {
                    .x             = x,
                    .y             = y,
                    .modifier_mask = get_modifier_mask()
                }
            }
        }
    );
}

void Context_window::handle_mouse_move(int64_t timestamp, float x, float y, float dx, float dy)
{
    if (m_is_mouse_relative_hold_enabled) {
        m_input_events[m_input_event_queue_write].push_back(
            Input_event{
                .type = Input_event_type::mouse_move_event,
                .timestamp_ns = timestamp,
                .u = {
                    .mouse_move_event = {
                        .x             = m_mouse_relative_hold_xpos,
                        .y             = m_mouse_relative_hold_ypos,
                        .dx            = dx,
                        .dy            = dy,
                        .modifier_mask = get_modifier_mask()
                    }
                }
            }
        );
    } else {
        m_last_mouse_x = x;
        m_last_mouse_y = y;
        m_input_events[m_input_event_queue_write].push_back(
            Input_event{
                .type = Input_event_type::mouse_move_event,
                .timestamp_ns = timestamp,
                .u = {
                    .mouse_move_event = {
                        .x             = x,
                        .y             = y,
                        .dx            = dx,
                        .dy            = dy,
                        .modifier_mask = get_modifier_mask()
                    }
                }
            }
        );
    }
}

void Context_window::handle_controller_axis_event(int64_t timestamp, int device, int axis, int value_)
{
    float normalized_value = static_cast<float>(value_) / 32767.0f;
    if (normalized_value < -1.0f) {
        normalized_value = -1.0f;
    }
    if (normalized_value > 1.0f) {
        normalized_value = 1.0f;
    }
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::controller_axis_event,
            .timestamp_ns = timestamp,
            .u = {
                .controller_axis_event = {
                    .controller    = device,
                    .axis          = axis,
                    .value         = normalized_value,
                    .modifier_mask = get_modifier_mask()
                }
            }
        }
    );
}

void Context_window::handle_controller_button_event(int64_t timestamp, int device, int button, bool pressed)
{
    m_input_events[m_input_event_queue_write].push_back(
        Input_event{
            .type = Input_event_type::controller_button_event,
            .timestamp_ns = timestamp,
            .u = {
                .controller_button_event = {
                    .controller    = device,
                    .button        = button,
                    .value         = pressed,
                    .modifier_mask = get_modifier_mask()
                }
            }
        }
    );
}

auto Context_window::get_cursor_relative_hold() const -> bool
{
    return m_is_mouse_relative_hold_enabled;
}

auto Context_window::get_width() const -> int
{
    int width {0};
    int height{0};
    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_GetWindowSizeInPixels(window, &width, &height);
    }
    return width;
}

auto Context_window::get_height() const -> int
{
    int width {0};
    int height{0};
    auto* const window = reinterpret_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_GetWindowSizeInPixels(window, &width, &height);
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
    gl::dynamic_load_init(SDL_GL_GetProcAddress);

#if defined(_WIN32)
    m_NV_delay_before_swap = SDL_GL_GetProcAddress("wglDelayBeforeSwapNV");
#else // TODO
#endif
}

void Context_window::make_current() const
{
    auto* window = static_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_GLContext sdl_context = static_cast<SDL_GLContext>(m_sdl_gl_context);
        SDL_GL_MakeCurrent(window, sdl_context);
    }
}

void Context_window::clear_current() const
{
    auto* window = static_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_GL_MakeCurrent(window, nullptr);
    }
}

auto Context_window::delay_before_swap(float seconds) const -> bool
{
#if defined(_WIN32)
    if (m_NV_delay_before_swap != nullptr) {
        auto* window = static_cast<SDL_Window*>(m_sdl_window);
        if (window != nullptr) {
            SDL_PropertiesID properties = SDL_GetWindowProperties(window);
            void* untyped_hdc = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HDC_POINTER, nullptr);
            HDC dc = static_cast<HDC>(untyped_hdc);
            PFNWGLDELAYBEFORESWAPNVPROC p_WGL_NV_delay_before_swap = (PFNWGLDELAYBEFORESWAPNVPROC)(m_NV_delay_before_swap);
            BOOL return_value = p_WGL_NV_delay_before_swap(dc, seconds);
            return return_value == TRUE;
        }
        return false;
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
    auto* window = static_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_GL_SwapWindow(window);
    }
}

auto Context_window::get_device_pointer() const -> void*
{
#if defined(_WIN32)
    return wglGetCurrentContext();
#else
    ERHE_FATAL("TODO");
    return nullptr; // TODO
#endif
}

auto Context_window::get_window_handle() const -> void*
{
#if defined(_WIN32)
    auto* window = static_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        void* untyped_hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        HWND hwnd = static_cast<HWND>(untyped_hwnd);
        return hwnd;
    } else {
        return nullptr;
    }
#else
    return nullptr; // TODO
#endif
}

#if defined(_WIN32)
auto Context_window::get_hwnd() const -> HWND
{
    auto* window = static_cast<SDL_Window*>(m_sdl_window);
    if (window != nullptr) {
        SDL_PropertiesID properties = SDL_GetWindowProperties(window);
        void* untyped_hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        HWND hwnd = static_cast<HWND>(untyped_hwnd);
        return hwnd;
    } else {
        return nullptr;
    }
}
auto Context_window::get_hglrc() const -> HGLRC
{
    return wglGetCurrentContext();
}
#endif

auto Context_window::get_scale_factor() const -> float
{
    return 1.0f; // TODO
}

}
