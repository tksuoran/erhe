#include "erhe/toolkit/window_event_handler.hpp"
#include "erhe/toolkit/toolkit_log.hpp"
#include "erhe/toolkit/window.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::toolkit
{

auto c_str(const Keycode code) -> const char*
{
    switch (code) {
        case Key_unknown      : return "unknown";
        case Key_space        : return "space";
        case Key_apostrophe   : return "apostrophe";
        case Key_comma        : return "comma";
        case Key_minus        : return "minus";
        case Key_period       : return "period";
        case Key_slash        : return "slash";
        case Key_0            : return "0";
        case Key_1            : return "1";
        case Key_2            : return "2";
        case Key_3            : return "3";
        case Key_4            : return "4";
        case Key_5            : return "5";
        case Key_6            : return "6";
        case Key_7            : return "7";
        case Key_8            : return "8";
        case Key_9            : return "9";
        case Key_semicolon    : return "semicolon";
        case Key_equal        : return "equal";
        case Key_a            : return "a";
        case Key_b            : return "b";
        case Key_c            : return "c";
        case Key_d            : return "d";
        case Key_e            : return "e";
        case Key_f            : return "f";
        case Key_g            : return "g";
        case Key_h            : return "h";
        case Key_i            : return "i";
        case Key_j            : return "j";
        case Key_k            : return "k";
        case Key_l            : return "l";
        case Key_m            : return "m";
        case Key_n            : return "n";
        case Key_o            : return "o";
        case Key_p            : return "p";
        case Key_q            : return "q";
        case Key_r            : return "r";
        case Key_s            : return "s";
        case Key_t            : return "t";
        case Key_u            : return "u";
        case Key_v            : return "v";
        case Key_w            : return "w";
        case Key_x            : return "x";
        case Key_y            : return "y";
        case Key_z            : return "z";
        case Key_left_bracket : return "left bracket";
        case Key_backslash    : return "backslash";
        case Key_right_bracket: return "right bracket";
        case Key_grave_accent : return "grave accent";
        case Key_world_1      : return "world 1";
        case Key_world_2      : return "world 2";
        case Key_escape       : return "escape";
        case Key_enter        : return "enter";
        case Key_tab          : return "tab";
        case Key_backspace    : return "backspace";
        case Key_insert       : return "insert";
        case Key_delete       : return "delete";
        case Key_right        : return "right";
        case Key_left         : return "left";
        case Key_down         : return "down";
        case Key_up           : return "up";
        case Key_page_up      : return "page up";
        case Key_page_down    : return "page down";
        case Key_home         : return "home";
        case Key_end          : return "end";
        case Key_caps_lock    : return "caps lock";
        case Key_scroll_lock  : return "scroll lock";
        case Key_num_lock     : return "num lock";
        case Key_print_screen : return "print screen";
        case Key_pause        : return "pause";
        case Key_f1           : return "f1";
        case Key_f2           : return "f2";
        case Key_f3           : return "f3";
        case Key_f4           : return "f4";
        case Key_f5           : return "f5";
        case Key_f6           : return "f6";
        case Key_f7           : return "f7";
        case Key_f8           : return "f8";
        case Key_f9           : return "f9";
        case Key_f10          : return "f10";
        case Key_f11          : return "f11";
        case Key_f12          : return "f12";
        case Key_f13          : return "f13";
        case Key_f14          : return "f14";
        case Key_f15          : return "f15";
        case Key_f16          : return "f16";
        case Key_f17          : return "f17";
        case Key_f18          : return "f18";
        case Key_f19          : return "f19";
        case Key_f20          : return "f20";
        case Key_f21          : return "f21";
        case Key_f22          : return "f22";
        case Key_f23          : return "f23";
        case Key_f24          : return "f24";
        case Key_f25          : return "f25";
        case Key_kp_0         : return "keypad 0";
        case Key_kp_1         : return "keypad 1";
        case Key_kp_2         : return "keypad 2";
        case Key_kp_3         : return "keypad 3";
        case Key_kp_4         : return "keypad 4";
        case Key_kp_5         : return "keypad 5";
        case Key_kp_6         : return "keypad 6";
        case Key_kp_7         : return "keypad 7";
        case Key_kp_8         : return "keypad 8";
        case Key_kp_9         : return "keypad 9";
        case Key_kp_decimal   : return "keypad decimal";
        case Key_kp_divide    : return "keypad divide";
        case Key_kp_multiply  : return "keypad multiply";
        case Key_kp_subtract  : return "keypad subtract";
        case Key_kp_add       : return "keypad add";
        case Key_kp_enter     : return "keypad enter";
        case Key_kp_equal     : return "keypad equal";
        case Key_left_shift   : return "left shift";
        case Key_left_control : return "left control";
        case Key_left_alt     : return "left alt";
        case Key_left_super   : return "left super";
        case Key_right_shift  : return "right shift";
        case Key_right_control: return "right control";
        case Key_right_alt    : return "right alt";
        case Key_right_super  : return "right super";
        case Key_menu         : return "menu";
        default: return "?";
    }
};

auto c_str(const Mouse_button button) -> const char*
{
    switch (button) {
        case Mouse_button_left  : return "left";
        case Mouse_button_right : return "right";
        case Mouse_button_middle: return "middle";
        case Mouse_button_wheel : return "wheel";
        case Mouse_button_x1    : return "x1";
        case Mouse_button_x2    : return "x2";
        default: return "?";
    };
}


Root_window_event_handler::Root_window_event_handler(Context_window* window)
    : m_window{window}
{
}

void Root_window_event_handler::sort_handlers()
{
    std::sort(
        m_handlers.begin(),
        m_handlers.end(),
        [](const Window_event_handler* lhs, const Window_event_handler* rhs) {
            return lhs->get_priority() > rhs->get_priority();
        }
    );
}

void Root_window_event_handler::attach(Window_event_handler* window_event_handler, int priority)
{
    ERHE_VERIFY(window_event_handler != nullptr);
    window_event_handler->set_priority(priority);
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_handlers.push_back(window_event_handler);
        sort_handlers();
    }
    window_event_handler->on_resize(m_window->get_width(), m_window->get_height());
}

void Root_window_event_handler::detach(Window_event_handler* window_event_handler)
{
    ERHE_VERIFY(window_event_handler != nullptr);
    std::lock_guard<std::mutex> lock{m_mutex};
    const auto i = std::remove(m_handlers.begin(), m_handlers.end(), window_event_handler);
    if (i == m_handlers.end()) {
        log_window->error("Window_event_handler was not attached");
    } else {
        m_handlers.erase(i, m_handlers.end());
    }
}

auto Root_window_event_handler::on_idle() -> bool 
{
    for (auto* handler : m_handlers) {
        if (handler->on_idle()) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_close() -> bool
{
    for (auto* handler : m_handlers) {
        if (handler->on_close()) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_focus(const int focused) -> bool
{
    for (auto* handler : m_handlers) {
        if (handler->on_focus(focused)) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_cursor_enter(const int entered) -> bool
{
    for (auto* handler : m_handlers) {
        if (handler->on_cursor_enter(entered)) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_refresh() -> bool
{
    for (auto* handler : m_handlers) {
        if (handler->on_refresh()) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_resize(const int width, const int height) -> bool
{
    m_width  = width;
    m_height = height;
    for (auto* handler : m_handlers) {
        if (handler->on_resize(width, height)) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_key(
    const Keycode           code,
    const Key_modifier_mask mask,
    const bool              pressed
) -> bool
{
    for (auto* handler : m_handlers) {
        if (handler->on_key(code, mask, pressed)) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_char(const unsigned int codepoint) -> bool
{
    for (auto* handler : m_handlers) {
        if (handler->on_char(codepoint)) {
            return true;
        }
    }
    return false;
}

void Root_window_event_handler::set_active_mouse_handler(Window_event_handler* handler)
{
    if (m_active_mouse_handler == handler) {
        return;
    }
    log_window_event->trace(
        "active mouse event handler set to '{}'",
        (handler != nullptr) ? handler->get_name() : ""
    );

    m_active_mouse_handler = handler;
}

auto Root_window_event_handler::on_mouse_move(const float x, const float y) -> bool
{
    if (m_active_mouse_handler != nullptr) {
        if (m_active_mouse_handler->has_active_mouse()) {
            if (m_active_mouse_handler->on_mouse_move(x, y)) {
                return true;
            }
        } else {
            set_active_mouse_handler(nullptr);
        }
    }

    for (auto* handler : m_handlers) {
        const bool consumed = handler->on_mouse_move(x, y);
        if (m_active_mouse_handler == nullptr) {
            if (handler->has_active_mouse()) {
                set_active_mouse_handler(handler);
            }
        }
        if (consumed) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_mouse_button(
    const Mouse_button button,
    const bool         pressed
) -> bool
{
    if (m_active_mouse_handler != nullptr) {
        if (m_active_mouse_handler->has_active_mouse()) {
            const bool handled = m_active_mouse_handler->on_mouse_button(button, pressed);
            if (handled) {
                return true;
            }
        } else {
            set_active_mouse_handler(nullptr);
        }
    }

    for (auto* handler : m_handlers) {
        const bool consumed = handler->on_mouse_button(button, pressed);
        if (m_active_mouse_handler == nullptr) {
            if (handler->has_active_mouse()) {
                set_active_mouse_handler(handler);
            }
        }
        if (consumed) {
            return true;
        }
    }
    return false;
}

auto Root_window_event_handler::on_mouse_wheel(
    const float x,
    const float y
) -> bool
{
    if (m_active_mouse_handler != nullptr) {
        if (m_active_mouse_handler->has_active_mouse()) {
            if (m_active_mouse_handler->on_mouse_wheel(x, y)) {
                return true;
            }
        } else {
            set_active_mouse_handler(nullptr);
        }
    }

    for (auto* handler : m_handlers) {
        const bool consumed = handler->on_mouse_wheel(x, y);
        if (m_active_mouse_handler == nullptr) {
            if (handler->has_active_mouse()) {
                set_active_mouse_handler(handler);
            }
        }
        if (consumed) {
            return true;
        }
    }
    return false;
}


} // namespace erhe::toolkit
