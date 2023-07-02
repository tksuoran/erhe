#pragma once

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::toolkit
{

class Context_window;

#pragma region Keycode
using Keycode = signed int;
constexpr Keycode Key_unknown       =  -1;
constexpr Keycode Key_space         =  32;
constexpr Keycode Key_apostrophe    =  39;
constexpr Keycode Key_comma         =  44;
constexpr Keycode Key_minus         =  45;
constexpr Keycode Key_period        =  46;
constexpr Keycode Key_slash         =  47;
constexpr Keycode Key_0             =  48;
constexpr Keycode Key_1             =  49;
constexpr Keycode Key_2             =  50;
constexpr Keycode Key_3             =  51;
constexpr Keycode Key_4             =  52;
constexpr Keycode Key_5             =  53;
constexpr Keycode Key_6             =  54;
constexpr Keycode Key_7             =  55;
constexpr Keycode Key_8             =  56;
constexpr Keycode Key_9             =  57;
constexpr Keycode Key_semicolon     =  59;
constexpr Keycode Key_equal         =  61;
constexpr Keycode Key_a             =  65;
constexpr Keycode Key_b             =  66;
constexpr Keycode Key_c             =  67;
constexpr Keycode Key_d             =  68;
constexpr Keycode Key_e             =  69;
constexpr Keycode Key_f             =  70;
constexpr Keycode Key_g             =  71;
constexpr Keycode Key_h             =  72;
constexpr Keycode Key_i             =  73;
constexpr Keycode Key_j             =  74;
constexpr Keycode Key_k             =  75;
constexpr Keycode Key_l             =  76;
constexpr Keycode Key_m             =  77;
constexpr Keycode Key_n             =  78;
constexpr Keycode Key_o             =  79;
constexpr Keycode Key_p             =  80;
constexpr Keycode Key_q             =  81;
constexpr Keycode Key_r             =  82;
constexpr Keycode Key_s             =  83;
constexpr Keycode Key_t             =  84;
constexpr Keycode Key_u             =  85;
constexpr Keycode Key_v             =  86;
constexpr Keycode Key_w             =  87;
constexpr Keycode Key_x             =  88;
constexpr Keycode Key_y             =  89;
constexpr Keycode Key_z             =  90;
constexpr Keycode Key_left_bracket  =  91;
constexpr Keycode Key_backslash     =  92;
constexpr Keycode Key_right_bracket =  93;
constexpr Keycode Key_grave_accent  =  96;
constexpr Keycode Key_world_1       = 161;
constexpr Keycode Key_world_2       = 162;
constexpr Keycode Key_escape        = 256;
constexpr Keycode Key_enter         = 257;
constexpr Keycode Key_tab           = 258;
constexpr Keycode Key_backspace     = 259;
constexpr Keycode Key_insert        = 260;
constexpr Keycode Key_delete        = 261;
constexpr Keycode Key_right         = 262;
constexpr Keycode Key_left          = 263;
constexpr Keycode Key_down          = 264;
constexpr Keycode Key_up            = 265;
constexpr Keycode Key_page_up       = 266;
constexpr Keycode Key_page_down     = 267;
constexpr Keycode Key_home          = 268;
constexpr Keycode Key_end           = 269;
constexpr Keycode Key_caps_lock     = 280;
constexpr Keycode Key_scroll_lock   = 281;
constexpr Keycode Key_num_lock      = 282;
constexpr Keycode Key_print_screen  = 283;
constexpr Keycode Key_pause         = 284;
constexpr Keycode Key_f1            = 290;
constexpr Keycode Key_f2            = 291;
constexpr Keycode Key_f3            = 292;
constexpr Keycode Key_f4            = 293;
constexpr Keycode Key_f5            = 294;
constexpr Keycode Key_f6            = 295;
constexpr Keycode Key_f7            = 296;
constexpr Keycode Key_f8            = 297;
constexpr Keycode Key_f9            = 298;
constexpr Keycode Key_f10           = 299;
constexpr Keycode Key_f11           = 300;
constexpr Keycode Key_f12           = 301;
constexpr Keycode Key_f13           = 302;
constexpr Keycode Key_f14           = 303;
constexpr Keycode Key_f15           = 304;
constexpr Keycode Key_f16           = 305;
constexpr Keycode Key_f17           = 306;
constexpr Keycode Key_f18           = 307;
constexpr Keycode Key_f19           = 308;
constexpr Keycode Key_f20           = 309;
constexpr Keycode Key_f21           = 310;
constexpr Keycode Key_f22           = 311;
constexpr Keycode Key_f23           = 312;
constexpr Keycode Key_f24           = 313;
constexpr Keycode Key_f25           = 314;
constexpr Keycode Key_kp_0          = 320;
constexpr Keycode Key_kp_1          = 321;
constexpr Keycode Key_kp_2          = 322;
constexpr Keycode Key_kp_3          = 323;
constexpr Keycode Key_kp_4          = 324;
constexpr Keycode Key_kp_5          = 325;
constexpr Keycode Key_kp_6          = 326;
constexpr Keycode Key_kp_7          = 327;
constexpr Keycode Key_kp_8          = 328;
constexpr Keycode Key_kp_9          = 329;
constexpr Keycode Key_kp_decimal    = 330;
constexpr Keycode Key_kp_divide     = 331;
constexpr Keycode Key_kp_multiply   = 332;
constexpr Keycode Key_kp_subtract   = 333;
constexpr Keycode Key_kp_add        = 334;
constexpr Keycode Key_kp_enter      = 335;
constexpr Keycode Key_kp_equal      = 336;
constexpr Keycode Key_left_shift    = 340;
constexpr Keycode Key_left_control  = 341;
constexpr Keycode Key_left_alt      = 342;
constexpr Keycode Key_left_super    = 343;
constexpr Keycode Key_right_shift   = 344;
constexpr Keycode Key_right_control = 345;
constexpr Keycode Key_right_alt     = 346;
constexpr Keycode Key_right_super   = 347;
constexpr Keycode Key_menu          = 348;
constexpr Keycode Key_last          = Key_menu;

extern auto c_str(const Keycode code) -> const char*;

using Key_modifier_mask = uint32_t;
constexpr Key_modifier_mask Key_modifier_bit_ctrl  = 0x0001u;
constexpr Key_modifier_mask Key_modifier_bit_shift = 0x0002u;
constexpr Key_modifier_mask Key_modifier_bit_super = 0x0004u;
constexpr Key_modifier_mask Key_modifier_bit_menu  = 0x0008u;

using Mouse_button = uint32_t;
constexpr Mouse_button Mouse_button_left   = 0;
constexpr Mouse_button Mouse_button_right  = 1;
constexpr Mouse_button Mouse_button_middle = 2;
constexpr Mouse_button Mouse_button_wheel  = 3;
constexpr Mouse_button Mouse_button_x1     = 4;
constexpr Mouse_button Mouse_button_x2     = 5;
constexpr Mouse_button Mouse_button_count  = 6;
constexpr Mouse_button Mouse_button_none   = 0xffffffff;

extern auto c_str(Mouse_button button) -> const char*;
#pragma endregion Keycode

class Window_event_handler;
class Root_window_event_handler;

// Window_event_handler can process input events from window
class Window_event_handler
{
public:
    virtual ~Window_event_handler() noexcept = default;

    void set_priority(int priority) { m_priority = priority; }
    [[nodiscard]] auto get_priority() const -> int { return m_priority; }

    virtual auto on_idle() -> bool { return false; }

    virtual auto on_close() -> bool { return false; }

    virtual auto on_focus(const int focused) -> bool
    {
        static_cast<void>(focused);
        return false;
    }

    virtual auto on_cursor_enter(const int entered) -> bool
    {
        static_cast<void>(entered);
        return false;
    }

    virtual auto on_refresh() -> bool
    {
        return false;
    }

    virtual auto on_resize(const int width, const int height) -> bool
    {
        static_cast<void>(width);
        static_cast<void>(height);
        return false;
    }

    virtual auto on_key(const Keycode code, const Key_modifier_mask modifier_mask, const bool pressed) -> bool
    {
        static_cast<void>(code);
        static_cast<void>(modifier_mask);
        static_cast<void>(pressed);
        return false;
    }

    virtual auto on_char(const unsigned int c) -> bool
    {
        static_cast<void>(c);
        return false;
    }

    virtual auto on_mouse_move(const float x, const float y) -> bool
    {
        static_cast<void>(x);
        static_cast<void>(y);
        return false;
    }

    virtual auto on_mouse_button(const Mouse_button button, const bool pressed) -> bool
    {
        static_cast<void>(button);
        static_cast<void>(pressed);
        return false;
    }

    virtual auto on_mouse_wheel(const float x, const float y) -> bool
    {
        static_cast<void>(x);
        static_cast<void>(y);
        return false;
    }

private:
    int m_priority{0};
};

class Root_window_event_handler
    : public Window_event_handler
{
public:
    explicit Root_window_event_handler(Context_window* window);

    void attach(Window_event_handler* handler, int priority);
    void detach(Window_event_handler* handler);

    auto on_focus       (int focused) -> bool                                        override;
    auto on_cursor_enter(int entered) -> bool                                        override;
    auto on_refresh     () -> bool                                                   override;
    auto on_idle        () -> bool                                                   override;
    auto on_close       () -> bool                                                   override;
    auto on_resize      (int width, int height) -> bool                              override;
    auto on_key         (Keycode code, Key_modifier_mask mask, bool pressed) -> bool override;
    auto on_char        (unsigned int codepoint) -> bool                             override;
    auto on_mouse_move  (float x, float y) -> bool                                   override;
    auto on_mouse_button(Mouse_button button, bool pressed) -> bool                  override;
    auto on_mouse_wheel (float x, float y) -> bool                                   override;

private:
    void sort_handlers();

    Context_window*                    m_window{nullptr};
    int                                m_width {0};
    int                                m_height{0};
    std::mutex                         m_mutex;
    std::vector<Window_event_handler*> m_handlers;
};

} // namespace erhe::toolkit
