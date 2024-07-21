// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_log.hpp"

#include <imgui/imgui_internal.h>

namespace erhe::imgui {

namespace {

auto from_erhe(const erhe::window::Keycode keycode) -> ImGuiKey
{
    switch (keycode) {
        case erhe::window::Key_tab          : return ImGuiKey_Tab           ;
        case erhe::window::Key_left         : return ImGuiKey_LeftArrow     ;
        case erhe::window::Key_right        : return ImGuiKey_RightArrow    ;
        case erhe::window::Key_up           : return ImGuiKey_UpArrow       ;
        case erhe::window::Key_down         : return ImGuiKey_DownArrow     ;
        case erhe::window::Key_page_up      : return ImGuiKey_PageUp        ;
        case erhe::window::Key_page_down    : return ImGuiKey_PageDown      ;
        case erhe::window::Key_home         : return ImGuiKey_Home          ;
        case erhe::window::Key_end          : return ImGuiKey_End           ;
        case erhe::window::Key_insert       : return ImGuiKey_Insert        ;
        case erhe::window::Key_delete       : return ImGuiKey_Delete        ;
        case erhe::window::Key_backspace    : return ImGuiKey_Backspace     ;
        case erhe::window::Key_space        : return ImGuiKey_Space         ;
        case erhe::window::Key_enter        : return ImGuiKey_Enter         ;
        case erhe::window::Key_escape       : return ImGuiKey_Escape        ;
        case erhe::window::Key_left_control : return ImGuiKey_LeftCtrl      ;
        case erhe::window::Key_left_shift   : return ImGuiKey_LeftShift     ;
        case erhe::window::Key_left_alt     : return ImGuiKey_LeftAlt       ;
        case erhe::window::Key_left_super   : return ImGuiKey_LeftSuper     ;
        case erhe::window::Key_right_control: return ImGuiKey_RightCtrl     ;
        case erhe::window::Key_right_shift  : return ImGuiKey_RightShift    ;
        case erhe::window::Key_right_alt    : return ImGuiKey_RightAlt      ;
        case erhe::window::Key_right_super  : return ImGuiKey_RightSuper    ;
        case erhe::window::Key_menu         : return ImGuiKey_Menu          ;
        case erhe::window::Key_0            : return ImGuiKey_0             ;
        case erhe::window::Key_1            : return ImGuiKey_1             ;
        case erhe::window::Key_2            : return ImGuiKey_2             ;
        case erhe::window::Key_3            : return ImGuiKey_3             ;
        case erhe::window::Key_4            : return ImGuiKey_4             ;
        case erhe::window::Key_5            : return ImGuiKey_5             ;
        case erhe::window::Key_6            : return ImGuiKey_6             ;
        case erhe::window::Key_7            : return ImGuiKey_7             ;
        case erhe::window::Key_8            : return ImGuiKey_8             ;
        case erhe::window::Key_9            : return ImGuiKey_9             ;
        case erhe::window::Key_a            : return ImGuiKey_A             ;
        case erhe::window::Key_b            : return ImGuiKey_B             ;
        case erhe::window::Key_c            : return ImGuiKey_C             ;
        case erhe::window::Key_d            : return ImGuiKey_D             ;
        case erhe::window::Key_e            : return ImGuiKey_E             ;
        case erhe::window::Key_f            : return ImGuiKey_F             ;
        case erhe::window::Key_g            : return ImGuiKey_G             ;
        case erhe::window::Key_h            : return ImGuiKey_H             ;
        case erhe::window::Key_i            : return ImGuiKey_I             ;
        case erhe::window::Key_j            : return ImGuiKey_J             ;
        case erhe::window::Key_k            : return ImGuiKey_K             ;
        case erhe::window::Key_l            : return ImGuiKey_L             ;
        case erhe::window::Key_m            : return ImGuiKey_M             ;
        case erhe::window::Key_n            : return ImGuiKey_N             ;
        case erhe::window::Key_o            : return ImGuiKey_O             ;
        case erhe::window::Key_p            : return ImGuiKey_P             ;
        case erhe::window::Key_q            : return ImGuiKey_Q             ;
        case erhe::window::Key_r            : return ImGuiKey_R             ;
        case erhe::window::Key_s            : return ImGuiKey_S             ;
        case erhe::window::Key_t            : return ImGuiKey_T             ;
        case erhe::window::Key_u            : return ImGuiKey_U             ;
        case erhe::window::Key_v            : return ImGuiKey_V             ;
        case erhe::window::Key_w            : return ImGuiKey_W             ;
        case erhe::window::Key_x            : return ImGuiKey_X             ;
        case erhe::window::Key_y            : return ImGuiKey_Y             ;
        case erhe::window::Key_z            : return ImGuiKey_Z             ;
        case erhe::window::Key_f1           : return ImGuiKey_F1            ;
        case erhe::window::Key_f2           : return ImGuiKey_F2            ;
        case erhe::window::Key_f3           : return ImGuiKey_F3            ;
        case erhe::window::Key_f4           : return ImGuiKey_F4            ;
        case erhe::window::Key_f5           : return ImGuiKey_F5            ;
        case erhe::window::Key_f6           : return ImGuiKey_F6            ;
        case erhe::window::Key_f7           : return ImGuiKey_F7            ;
        case erhe::window::Key_f8           : return ImGuiKey_F8            ;
        case erhe::window::Key_f9           : return ImGuiKey_F9            ;
        case erhe::window::Key_f10          : return ImGuiKey_F10           ;
        case erhe::window::Key_f11          : return ImGuiKey_F11           ;
        case erhe::window::Key_f12          : return ImGuiKey_F12           ;
        case erhe::window::Key_apostrophe   : return ImGuiKey_Apostrophe    ;
        case erhe::window::Key_comma        : return ImGuiKey_Comma         ;
        case erhe::window::Key_minus        : return ImGuiKey_Minus         ; // -
        case erhe::window::Key_period       : return ImGuiKey_Period        ; // .
        case erhe::window::Key_slash        : return ImGuiKey_Slash         ; // /
        case erhe::window::Key_semicolon    : return ImGuiKey_Semicolon     ; // ;
        case erhe::window::Key_equal        : return ImGuiKey_Equal         ; // =
        case erhe::window::Key_left_bracket : return ImGuiKey_LeftBracket   ; // [
        case erhe::window::Key_backslash    : return ImGuiKey_Backslash     ; // \ (this text inhibit multiline comment caused by backslash)
        case erhe::window::Key_right_bracket: return ImGuiKey_RightBracket  ; // ]
        case erhe::window::Key_grave_accent : return ImGuiKey_GraveAccent   ; // `
        case erhe::window::Key_caps_lock    : return ImGuiKey_CapsLock      ;
        case erhe::window::Key_scroll_lock  : return ImGuiKey_ScrollLock    ;
        case erhe::window::Key_num_lock     : return ImGuiKey_NumLock       ;
        case erhe::window::Key_print_screen : return ImGuiKey_PrintScreen   ;
        case erhe::window::Key_pause        : return ImGuiKey_Pause         ;
        case erhe::window::Key_kp_0         : return ImGuiKey_Keypad0       ;
        case erhe::window::Key_kp_1         : return ImGuiKey_Keypad1       ;
        case erhe::window::Key_kp_2         : return ImGuiKey_Keypad2       ;
        case erhe::window::Key_kp_3         : return ImGuiKey_Keypad3       ;
        case erhe::window::Key_kp_4         : return ImGuiKey_Keypad4       ;
        case erhe::window::Key_kp_5         : return ImGuiKey_Keypad5       ;
        case erhe::window::Key_kp_6         : return ImGuiKey_Keypad6       ;
        case erhe::window::Key_kp_7         : return ImGuiKey_Keypad7       ;
        case erhe::window::Key_kp_8         : return ImGuiKey_Keypad8       ;
        case erhe::window::Key_kp_9         : return ImGuiKey_Keypad9       ;
        case erhe::window::Key_kp_decimal   : return ImGuiKey_KeypadDecimal ;
        case erhe::window::Key_kp_divide    : return ImGuiKey_KeypadDivide  ;
        case erhe::window::Key_kp_multiply  : return ImGuiKey_KeypadMultiply;
        case erhe::window::Key_kp_subtract  : return ImGuiKey_KeypadSubtract;
        case erhe::window::Key_kp_add       : return ImGuiKey_KeypadAdd     ;
        case erhe::window::Key_kp_enter     : return ImGuiKey_KeypadEnter   ;
        case erhe::window::Key_kp_equal     : return ImGuiKey_KeypadEqual   ;
        default                             : return ImGuiKey_None          ;
    }
}

auto from_erhe(const erhe::window::Mouse_button button) -> int
{
    switch (button) {
        case erhe::window::Mouse_button_left:   return ImGuiMouseButton_Left;
        case erhe::window::Mouse_button_right:  return ImGuiMouseButton_Right;
        case erhe::window::Mouse_button_middle: return ImGuiMouseButton_Middle;
        case erhe::window::Mouse_button_x1:     return 3;
        case erhe::window::Mouse_button_x2:     return 4;
        default: return -1;
    }
}

void update_key_modifiers(ImGuiIO& io, const uint32_t modifier_mask)
{
    io.AddKeyEvent(ImGuiMod_Ctrl,  (modifier_mask & erhe::window::Key_modifier_bit_ctrl ) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifier_mask & erhe::window::Key_modifier_bit_shift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt,   (modifier_mask & erhe::window::Key_modifier_bit_menu ) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (modifier_mask & erhe::window::Key_modifier_bit_super) != 0);
}

}

Imgui_host::Imgui_host(
    erhe::rendergraph::Rendergraph& rendergraph,
    Imgui_renderer&                 imgui_renderer,
    const std::string_view          name,
    const bool                      imgui_ini,
    ImFontAtlas*                    font_atlas
)
    : Rendergraph_node{rendergraph, fmt::format("Imgui_host {}", name)}
    , m_imgui_ini_path{imgui_ini ? fmt::format("imgui_{}.ini", name) : ""}
    , m_imgui_renderer{imgui_renderer}
{
    IMGUI_CHECKVERSION();
    m_imgui_context = ImGui::CreateContext(font_atlas);

    ImGuiIO& io = m_imgui_context->IO;
    io.IniFilename = imgui_ini ? m_imgui_ini_path.c_str() : nullptr;

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Imgui_window_scene_view a dependency for Imgui_host, forcing
    // correct rendering order (Imgui_window_scene_view must be rendered before
    // Imgui_host).
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_input(erhe::rendergraph::Routing::None, "window", erhe::rendergraph::Rendergraph_node_key::window);

    imgui_renderer.register_imgui_host(this);
}

Imgui_host::~Imgui_host()
{
    m_imgui_renderer.unregister_imgui_host(this);

    ImGui::DestroyContext(m_imgui_context);
    m_imgui_context = nullptr;
}

auto Imgui_host::get_imgui_renderer() -> Imgui_renderer&
{
    return m_imgui_renderer;
}

auto Imgui_host::get_imgui_context() -> ImGuiContext*
{
    return m_imgui_context;
}

auto Imgui_host::name() const -> const std::string&
{
    return m_name;
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
auto Imgui_host::want_capture_keyboard() const -> bool
{
    ImGuiIO& io = m_imgui_context->IO;
    return io.WantCaptureKeyboard && !m_request_keyboard;
}

auto Imgui_host::want_capture_mouse() const -> bool
{
    ImGuiIO& io = m_imgui_context->IO;
    return io.WantCaptureMouse && !m_request_mouse;
}

auto Imgui_host::has_cursor() const -> bool
{
    return m_has_cursor;
}

auto Imgui_host::imgui_context() const -> ImGuiContext*
{
    return m_imgui_context;
}

void Imgui_host::set_begin_callback(const std::function<void(Imgui_host& viewport)>& callback)
{
    m_begin_callback = callback;
}

auto Imgui_host::get_scale_value() const -> float
{
    return 1.0f;
}

void Imgui_host::update_input_request(const bool request_keyboard, const bool request_mouse)
{
    m_request_keyboard = request_keyboard;
    m_request_mouse    = request_mouse;
}

auto Imgui_host::get_mouse_position() const -> glm::vec2
{
    ImGuiIO& io = m_imgui_context->IO;
    return glm::vec2{io.MousePos.x, io.MousePos.y};
}

#pragma region Events

// Note: All handlers here return false, in order to
//       let other, later event handlers to process
//       these events as well.
auto Imgui_host::on_cursor_enter_event(const erhe::window::Cursor_enter_event& cursor_enter_event) -> bool
{
    SPDLOG_LOGGER_TRACE(log_input_events, "on_cursor_enter_event({})", cursor_enter_event.entered);
    m_has_cursor = cursor_enter_event.entered != 0;
    ImGuiIO& io = m_imgui_context->IO;
    if (!m_has_cursor) {
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
    return true;
}

auto Imgui_host::on_window_focus_event(const erhe::window::Window_focus_event& window_focus_event) -> bool
{
    SPDLOG_LOGGER_TRACE(log_input_events, "Imgui_host::on_window_focus_event({})", window_focus_event.focused);
    ImGuiIO& io = m_imgui_context->IO;
    io.AddFocusEvent(window_focus_event.focused);
    return true;
}

auto Imgui_host::on_mouse_move_event(const erhe::window::Mouse_move_event& mouse_move_event) -> bool
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddMousePosEvent(mouse_move_event.x, mouse_move_event.y);
    return false;
}

auto Imgui_host::on_mouse_button_event(const erhe::window::Mouse_button_event& mouse_button_event) -> bool
{
    int imgui_mouse_button = from_erhe(mouse_button_event.button);
    if (imgui_mouse_button < 0) {
        return false;
    }

    ImGuiIO& io = m_imgui_context->IO;
    io.AddMouseButtonEvent(imgui_mouse_button, mouse_button_event.pressed);
    return false;
}

auto Imgui_host::on_mouse_wheel_event(const erhe::window::Mouse_wheel_event& mouse_wheel_event) -> bool
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddMouseWheelEvent(mouse_wheel_event.x, mouse_wheel_event.y);
    return false;
}

auto Imgui_host::on_key_event(const erhe::window::Key_event& event) -> bool
{
    using erhe::window::Keycode;

    ImGuiIO& io = m_imgui_context->IO;
    update_key_modifiers(io, event.modifier_mask);
    io.AddKeyEvent(from_erhe(event.keycode), event.pressed);
    return false;
}

auto Imgui_host::on_char_event(const erhe::window::Char_event& char_event) -> bool
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddInputCharacter(char_event.codepoint);
    return false;
}

#pragma endregion Events

#else
auto Imgui_host::want_capture_mouse() const -> bool
{
    return false;
}

void Imgui_host::on_focus(int) {}
void Imgui_host::on_cursor_enter(int) {}
void Imgui_host::on_mouse_button(uint32_t, bool) {}
void Imgui_host::on_mouse_wheel(float, float) {}
void Imgui_host::make_imgui_context_current() {}
void Imgui_host::make_imgui_context_uncurrent() {}
void Imgui_host::on_key(signed int, uint32_t, bool) {}
void Imgui_host::on_char(const unsigned int) {}

#endif

} // namespace erhe::imgui
