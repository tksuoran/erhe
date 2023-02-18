#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/window.hpp"

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


Root_view::Root_view(Context_window* window)
    : m_window{window}
{
}

void Root_view::set_view(View* view)
{
    view->on_resize(m_window->get_width(), m_window->get_height());
    m_view = view;
}

void Root_view::reset_view(View* view)
{
    view->on_resize(m_window->get_width(), m_window->get_height());
    m_view = view;
    m_last_view = nullptr;
}

void Root_view::on_idle()
{
    if (m_last_view != m_view) {
        if (m_last_view) {
            m_last_view->on_exit();
        }

        m_view->on_enter();
        m_last_view = m_view;
    }

    if (m_view != nullptr) {
        m_view->update();
    }
}

void Root_view::on_close()
{
    if (m_view != nullptr) {
        //m_view->on_exit();
        m_view->on_close();
        //m_view = nullptr;
    }

    //m_last_view = nullptr;
    //
    //if (m_window != nullptr) {
    //    m_window->break_event_loop();
    //}
}

void Root_view::on_focus(int focused)
{
    if (m_view != nullptr) {
        m_view->on_focus(focused);
    }
}

void Root_view::on_cursor_enter(int entered)
{
    if (m_view != nullptr) {
        m_view->on_cursor_enter(entered);
    }
}

void Root_view::on_refresh()
{
    if (m_view != nullptr) {
        m_view->on_refresh();
    }
}

void Root_view::on_resize(const int width, const int height)
{
    if (m_view != nullptr) {
        m_view->on_resize(width, height);
    }
}

void Root_view::on_key(const Keycode code, const Key_modifier_mask mask, const bool pressed)
{
    if (m_view != nullptr) {
        m_view->on_key(code, mask, pressed);
    }
}

void Root_view::on_char(const unsigned int codepoint)
{
    if (m_view != nullptr) {
        m_view->on_char(codepoint);
    }
}

void Root_view::on_mouse_move(const float x, const float y)
{
    if (m_view != nullptr) {
        m_view->on_mouse_move(x, y);
    }
}

void Root_view::on_mouse_button(const Mouse_button button, const bool pressed)
{
    if (m_view != nullptr) {
        m_view->on_mouse_button(button, pressed);
    }
}

void Root_view::on_mouse_wheel(const float x, const float y)
{
    if (m_view != nullptr) {
        m_view->on_mouse_wheel(x, y);
    }
}


} // namespace erhe::toolkit
