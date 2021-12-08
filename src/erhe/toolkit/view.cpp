#include "erhe/toolkit/view.hpp"
#include "erhe/toolkit/window.hpp"

namespace erhe::toolkit
{

auto c_str(const Keycode code) -> const char*
{
    switch (code)
    {
        case Keycode::Key_unknown      : return "unknown";
        case Keycode::Key_space        : return "space";
        case Keycode::Key_apostrophe   : return "apostrophe";
        case Keycode::Key_comma        : return "comma";
        case Keycode::Key_minus        : return "minus";
        case Keycode::Key_period       : return "period";
        case Keycode::Key_slash        : return "slash";
        case Keycode::Key_0            : return "0";
        case Keycode::Key_1            : return "1";
        case Keycode::Key_2            : return "2";
        case Keycode::Key_3            : return "3";
        case Keycode::Key_4            : return "4";
        case Keycode::Key_5            : return "5";
        case Keycode::Key_6            : return "6";
        case Keycode::Key_7            : return "7";
        case Keycode::Key_8            : return "8";
        case Keycode::Key_9            : return "9";
        case Keycode::Key_semicolon    : return "semicolon";
        case Keycode::Key_equal        : return "equal";
        case Keycode::Key_a            : return "a";
        case Keycode::Key_b            : return "b";
        case Keycode::Key_c            : return "c";
        case Keycode::Key_d            : return "d";
        case Keycode::Key_e            : return "e";
        case Keycode::Key_f            : return "f";
        case Keycode::Key_g            : return "g";
        case Keycode::Key_h            : return "h";
        case Keycode::Key_i            : return "i";
        case Keycode::Key_j            : return "j";
        case Keycode::Key_k            : return "k";
        case Keycode::Key_l            : return "l";
        case Keycode::Key_m            : return "m";
        case Keycode::Key_n            : return "n";
        case Keycode::Key_o            : return "o";
        case Keycode::Key_p            : return "p";
        case Keycode::Key_q            : return "q";
        case Keycode::Key_r            : return "r";
        case Keycode::Key_s            : return "s";
        case Keycode::Key_t            : return "t";
        case Keycode::Key_u            : return "u";
        case Keycode::Key_v            : return "v";
        case Keycode::Key_w            : return "w";
        case Keycode::Key_x            : return "x";
        case Keycode::Key_y            : return "y";
        case Keycode::Key_z            : return "z";
        case Keycode::Key_left_bracket : return "left bracket";
        case Keycode::Key_backslash    : return "backslash";
        case Keycode::Key_right_bracket: return "right bracket";
        case Keycode::Key_grave_accent : return "grave accent";
        case Keycode::Key_world_1      : return "world 1";
        case Keycode::Key_world_2      : return "world 2";
        case Keycode::Key_escape       : return "escape";
        case Keycode::Key_enter        : return "enter";
        case Keycode::Key_tab          : return "tab";
        case Keycode::Key_backspace    : return "backspace";
        case Keycode::Key_insert       : return "insert";
        case Keycode::Key_delete       : return "delete";
        case Keycode::Key_right        : return "right";
        case Keycode::Key_left         : return "left";
        case Keycode::Key_down         : return "down";
        case Keycode::Key_up           : return "up";
        case Keycode::Key_page_up      : return "page up";
        case Keycode::Key_page_down    : return "page down";
        case Keycode::Key_home         : return "home";
        case Keycode::Key_end          : return "end";
        case Keycode::Key_caps_lock    : return "caps lock";
        case Keycode::Key_scroll_lock  : return "scroll lock";
        case Keycode::Key_num_lock     : return "num lock";
        case Keycode::Key_print_screen : return "print screen";
        case Keycode::Key_pause        : return "pause";
        case Keycode::Key_f1           : return "f1";
        case Keycode::Key_f2           : return "f2";
        case Keycode::Key_f3           : return "f3";
        case Keycode::Key_f4           : return "f4";
        case Keycode::Key_f5           : return "f5";
        case Keycode::Key_f6           : return "f6";
        case Keycode::Key_f7           : return "f7";
        case Keycode::Key_f8           : return "f8";
        case Keycode::Key_f9           : return "f9";
        case Keycode::Key_f10          : return "f10";
        case Keycode::Key_f11          : return "f11";
        case Keycode::Key_f12          : return "f12";
        case Keycode::Key_f13          : return "f13";
        case Keycode::Key_f14          : return "f14";
        case Keycode::Key_f15          : return "f15";
        case Keycode::Key_f16          : return "f16";
        case Keycode::Key_f17          : return "f17";
        case Keycode::Key_f18          : return "f18";
        case Keycode::Key_f19          : return "f19";
        case Keycode::Key_f20          : return "f20";
        case Keycode::Key_f21          : return "f21";
        case Keycode::Key_f22          : return "f22";
        case Keycode::Key_f23          : return "f23";
        case Keycode::Key_f24          : return "f24";
        case Keycode::Key_f25          : return "f25";
        case Keycode::Key_kp_0         : return "keypad 0";
        case Keycode::Key_kp_1         : return "keypad 1";
        case Keycode::Key_kp_2         : return "keypad 2";
        case Keycode::Key_kp_3         : return "keypad 3";
        case Keycode::Key_kp_4         : return "keypad 4";
        case Keycode::Key_kp_5         : return "keypad 5";
        case Keycode::Key_kp_6         : return "keypad 6";
        case Keycode::Key_kp_7         : return "keypad 7";
        case Keycode::Key_kp_8         : return "keypad 8";
        case Keycode::Key_kp_9         : return "keypad 9";
        case Keycode::Key_kp_decimal   : return "keypad decimal";
        case Keycode::Key_kp_divide    : return "keypad divide";
        case Keycode::Key_kp_multiply  : return "keypad multiply";
        case Keycode::Key_kp_subtract  : return "keypad subtract";
        case Keycode::Key_kp_add       : return "keypad add";
        case Keycode::Key_kp_enter     : return "keypad enter";
        case Keycode::Key_kp_equal     : return "keypad equal";
        case Keycode::Key_left_shift   : return "left shift";
        case Keycode::Key_left_control : return "left control";
        case Keycode::Key_left_alt     : return "left alt";
        case Keycode::Key_left_super   : return "left super";
        case Keycode::Key_right_shift  : return "right shift";
        case Keycode::Key_right_control: return "right control";
        case Keycode::Key_right_alt    : return "right alt";
        case Keycode::Key_right_super  : return "right super";
        case Keycode::Key_menu         : return "menu";    
        default: return "?";
    }
};

auto c_str(const Mouse_button button) -> const char*
{
    switch (button)
    {
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
    if (m_last_view != m_view)
    {
        if (m_last_view)
        {
            m_last_view->on_exit();
        }

        m_view->on_enter();
        m_last_view = m_view;
    }

    if (m_view != nullptr)
    {
        m_view->update();
    }
}

void Root_view::on_close()
{
    if (m_view != nullptr)
    {
        m_view->on_exit();
        m_view = nullptr;
    }

    m_last_view = nullptr;

    if (m_window != nullptr)
    {
        m_window->break_event_loop();
    }
}

void Root_view::on_resize(const int width, const int height)
{
    if (m_view != nullptr)
    {
        m_view->on_resize(width, height);
    }
}

void Root_view::on_key_press(const Keycode code, const Key_modifier_mask mask)
{
    if (m_view != nullptr)
    {
        m_view->on_key_press(code, mask);
    }
}

void Root_view::on_key_release(const Keycode code, const Key_modifier_mask mask)
{
    if (m_view != nullptr)
    {
        m_view->on_key_release(code, mask);
    }
}

void Root_view::on_mouse_move(const double x, const double y)
{
    if (m_view != nullptr)
    {
        m_view->on_mouse_move(x, y);
    }
}

void Root_view::on_mouse_click(const Mouse_button button, const int count)
{
    if (m_view != nullptr)
    {
        m_view->on_mouse_click(button, count);
    }
}

} // namespace erhe::toolkit
