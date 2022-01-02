#pragma once

#include <memory>

namespace erhe::toolkit
{

class Context_window;

enum class Keycode : signed int
{
    Key_unknown       =  -1,
    Key_space         =  32,
    Key_apostrophe    =  39, // '
    Key_comma         =  44, // ,
    Key_minus         =  45, // -
    Key_period        =  46, // .
    Key_slash         =  47, // /
    Key_0             =  48,
    Key_1             =  49,
    Key_2             =  50,
    Key_3             =  51,
    Key_4             =  52,
    Key_5             =  53,
    Key_6             =  54,
    Key_7             =  55,
    Key_8             =  56,
    Key_9             =  57,
    Key_semicolon     =  59, // ;
    Key_equal         =  61, // =
    Key_a             =  65,
    Key_b             =  66,
    Key_c             =  67,
    Key_d             =  68,
    Key_e             =  69,
    Key_f             =  70,
    Key_g             =  71,
    Key_h             =  72,
    Key_i             =  73,
    Key_j             =  74,
    Key_k             =  75,
    Key_l             =  76,
    Key_m             =  77,
    Key_n             =  78,
    Key_o             =  79,
    Key_p             =  80,
    Key_q             =  81,
    Key_r             =  82,
    Key_s             =  83,
    Key_t             =  84,
    Key_u             =  85,
    Key_v             =  86,
    Key_w             =  87,
    Key_x             =  88,
    Key_y             =  89,
    Key_z             =  90,
    Key_left_bracket  =  91, // [
    Key_backslash     =  92, // \    .
    Key_right_bracket =  93, // ]
    Key_grave_accent  =  96, // `
    Key_world_1       = 161, // non-US #1
    Key_world_2       = 162, // non-US #2
    Key_escape        = 256,
    Key_enter         = 257,
    Key_tab           = 258,
    Key_backspace     = 259,
    Key_insert        = 260,
    Key_delete        = 261,
    Key_right         = 262,
    Key_left          = 263,
    Key_down          = 264,
    Key_up            = 265,
    Key_page_up       = 266,
    Key_page_down     = 267,
    Key_home          = 268,
    Key_end           = 269,
    Key_caps_lock     = 280,
    Key_scroll_lock   = 281,
    Key_num_lock      = 282,
    Key_print_screen  = 283,
    Key_pause         = 284,
    Key_f1            = 290,
    Key_f2            = 291,
    Key_f3            = 292,
    Key_f4            = 293,
    Key_f5            = 294,
    Key_f6            = 295,
    Key_f7            = 296,
    Key_f8            = 297,
    Key_f9            = 298,
    Key_f10           = 299,
    Key_f11           = 300,
    Key_f12           = 301,
    Key_f13           = 302,
    Key_f14           = 303,
    Key_f15           = 304,
    Key_f16           = 305,
    Key_f17           = 306,
    Key_f18           = 307,
    Key_f19           = 308,
    Key_f20           = 309,
    Key_f21           = 310,
    Key_f22           = 311,
    Key_f23           = 312,
    Key_f24           = 313,
    Key_f25           = 314,
    Key_kp_0          = 320,
    Key_kp_1          = 321,
    Key_kp_2          = 322,
    Key_kp_3          = 323,
    Key_kp_4          = 324,
    Key_kp_5          = 325,
    Key_kp_6          = 326,
    Key_kp_7          = 327,
    Key_kp_8          = 328,
    Key_kp_9          = 329,
    Key_kp_decimal    = 330,
    Key_kp_divide     = 331,
    Key_kp_multiply   = 332,
    Key_kp_subtract   = 333,
    Key_kp_add        = 334,
    Key_kp_enter      = 335,
    Key_kp_equal      = 336,
    Key_left_shift    = 340,
    Key_left_control  = 341,
    Key_left_alt      = 342,
    Key_left_super    = 343,
    Key_right_shift   = 344,
    Key_right_control = 345,
    Key_right_alt     = 346,
    Key_right_super   = 347,
    Key_menu          = 348,
    Key_last          = Key_menu
};

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

extern auto c_str(const Mouse_button button) -> const char*;

class Event_handler;
class View;
class Root_view;

// Event_handler can process input events from WSI
class Event_handler
{
public:
    virtual ~Event_handler() = default;

    virtual void on_idle()
    {
    }

    virtual void on_close()
    {
    }

    virtual void on_refresh()
    {
    }

    virtual void on_resize(int width, int height)
    {
        static_cast<void>(width);
        static_cast<void>(height);
    }

    virtual void on_key_press(Keycode code, Key_modifier_mask modifier_mask)
    {
        static_cast<void>(code);
        static_cast<void>(modifier_mask);
    }

    virtual void on_key_release(Keycode code, Key_modifier_mask modifier_mask)
    {
        static_cast<void>(code);
        static_cast<void>(modifier_mask);
    }

    virtual void on_mouse_move(double x, double y)
    {
        static_cast<void>(x);
        static_cast<void>(y);
    }

    virtual void on_mouse_click(Mouse_button button, int count)
    {
        static_cast<void>(button);
        static_cast<void>(count);
    }
};

// View is the currently active EventHandler.
// Only one View can be currently active.
// When a View is made active, the on_exit() is called on the previously active View (if any),
// and on_enter is called for the new View.
class View
    : public Event_handler
{
public:
    ~View() override = default;

    virtual void on_enter() {}
    virtual void on_exit () {}
    virtual void update  () {}

    void on_resize(int width, int height) override
    {
        m_width  = width;
        m_height = height;
    }

    [[nodiscard]] auto width() -> int
    {
        return m_width;
    }

    [[nodiscard]] auto height() -> int
    {
        return m_height;
    }

private:
    int m_width {0};
    int m_height{0};
};

// All events go first to Root_view, which routes
// them to the currently active View.
// Root_view manages the currently active view, and
// performs View transition by calling on_exit() and
// on_enter() when active View is changed.
class Root_view
    : public Event_handler
{
public:
    explicit Root_view(Context_window* window);

    void set_view      (View* view);
    void reset_view    (View* view);
    void on_refresh    () override;
    void on_idle       () override;
    void on_close      () override;
    void on_resize     (const int width, const int height) override;
    void on_key_press  (const Keycode code, const Key_modifier_mask mask) override;
    void on_key_release(const Keycode code, const Key_modifier_mask mask) override;
    void on_mouse_move (const double x, const double y) override;
    void on_mouse_click(const Mouse_button button, const int count) override;

private:
    Context_window* m_window   {nullptr};
    View*           m_view     {nullptr};
    View*           m_last_view{nullptr};
};

} // namespace erhe::toolkit
