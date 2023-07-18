// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/imgui/imgui_viewport.hpp"
#include "erhe/imgui/imgui_windows.hpp"
#include "erhe/imgui/scoped_imgui_context.hpp"
#include "erhe/imgui/imgui_log.hpp"
#include "erhe/toolkit/window.hpp"

#include <imgui/imgui_internal.h>

#include <algorithm>

namespace erhe::imgui {

namespace {

auto from_erhe(const erhe::toolkit::Keycode keycode) -> ImGuiKey
{
    switch (keycode) {
        case erhe::toolkit::Key_tab          : return ImGuiKey_Tab           ;
        case erhe::toolkit::Key_left         : return ImGuiKey_LeftArrow     ;
        case erhe::toolkit::Key_right        : return ImGuiKey_RightArrow    ;
        case erhe::toolkit::Key_up           : return ImGuiKey_UpArrow       ;
        case erhe::toolkit::Key_down         : return ImGuiKey_DownArrow     ;
        case erhe::toolkit::Key_page_up      : return ImGuiKey_PageUp        ;
        case erhe::toolkit::Key_page_down    : return ImGuiKey_PageDown      ;
        case erhe::toolkit::Key_home         : return ImGuiKey_Home          ;
        case erhe::toolkit::Key_end          : return ImGuiKey_End           ;
        case erhe::toolkit::Key_insert       : return ImGuiKey_Insert        ;
        case erhe::toolkit::Key_delete       : return ImGuiKey_Delete        ;
        case erhe::toolkit::Key_backspace    : return ImGuiKey_Backspace     ;
        case erhe::toolkit::Key_space        : return ImGuiKey_Space         ;
        case erhe::toolkit::Key_enter        : return ImGuiKey_Enter         ;
        case erhe::toolkit::Key_escape       : return ImGuiKey_Escape        ;
        case erhe::toolkit::Key_left_control : return ImGuiKey_LeftCtrl      ;
        case erhe::toolkit::Key_left_shift   : return ImGuiKey_LeftShift     ;
        case erhe::toolkit::Key_left_alt     : return ImGuiKey_LeftAlt       ;
        case erhe::toolkit::Key_left_super   : return ImGuiKey_LeftSuper     ;
        case erhe::toolkit::Key_right_control: return ImGuiKey_RightCtrl     ;
        case erhe::toolkit::Key_right_shift  : return ImGuiKey_RightShift    ;
        case erhe::toolkit::Key_right_alt    : return ImGuiKey_RightAlt      ;
        case erhe::toolkit::Key_right_super  : return ImGuiKey_RightSuper    ;
        case erhe::toolkit::Key_menu         : return ImGuiKey_Menu          ;
        case erhe::toolkit::Key_0            : return ImGuiKey_0             ;
        case erhe::toolkit::Key_1            : return ImGuiKey_1             ;
        case erhe::toolkit::Key_2            : return ImGuiKey_2             ;
        case erhe::toolkit::Key_3            : return ImGuiKey_3             ;
        case erhe::toolkit::Key_4            : return ImGuiKey_4             ;
        case erhe::toolkit::Key_5            : return ImGuiKey_5             ;
        case erhe::toolkit::Key_6            : return ImGuiKey_6             ;
        case erhe::toolkit::Key_7            : return ImGuiKey_7             ;
        case erhe::toolkit::Key_8            : return ImGuiKey_8             ;
        case erhe::toolkit::Key_9            : return ImGuiKey_9             ;
        case erhe::toolkit::Key_a            : return ImGuiKey_A             ;
        case erhe::toolkit::Key_b            : return ImGuiKey_B             ;
        case erhe::toolkit::Key_c            : return ImGuiKey_C             ;
        case erhe::toolkit::Key_d            : return ImGuiKey_D             ;
        case erhe::toolkit::Key_e            : return ImGuiKey_E             ;
        case erhe::toolkit::Key_f            : return ImGuiKey_F             ;
        case erhe::toolkit::Key_g            : return ImGuiKey_G             ;
        case erhe::toolkit::Key_h            : return ImGuiKey_H             ;
        case erhe::toolkit::Key_i            : return ImGuiKey_I             ;
        case erhe::toolkit::Key_j            : return ImGuiKey_J             ;
        case erhe::toolkit::Key_k            : return ImGuiKey_K             ;
        case erhe::toolkit::Key_l            : return ImGuiKey_L             ;
        case erhe::toolkit::Key_m            : return ImGuiKey_M             ;
        case erhe::toolkit::Key_n            : return ImGuiKey_N             ;
        case erhe::toolkit::Key_o            : return ImGuiKey_O             ;
        case erhe::toolkit::Key_p            : return ImGuiKey_P             ;
        case erhe::toolkit::Key_q            : return ImGuiKey_Q             ;
        case erhe::toolkit::Key_r            : return ImGuiKey_R             ;
        case erhe::toolkit::Key_s            : return ImGuiKey_S             ;
        case erhe::toolkit::Key_t            : return ImGuiKey_T             ;
        case erhe::toolkit::Key_u            : return ImGuiKey_U             ;
        case erhe::toolkit::Key_v            : return ImGuiKey_V             ;
        case erhe::toolkit::Key_w            : return ImGuiKey_W             ;
        case erhe::toolkit::Key_x            : return ImGuiKey_X             ;
        case erhe::toolkit::Key_y            : return ImGuiKey_Y             ;
        case erhe::toolkit::Key_z            : return ImGuiKey_Z             ;
        case erhe::toolkit::Key_f1           : return ImGuiKey_F1            ;
        case erhe::toolkit::Key_f2           : return ImGuiKey_F2            ;
        case erhe::toolkit::Key_f3           : return ImGuiKey_F3            ;
        case erhe::toolkit::Key_f4           : return ImGuiKey_F4            ;
        case erhe::toolkit::Key_f5           : return ImGuiKey_F5            ;
        case erhe::toolkit::Key_f6           : return ImGuiKey_F6            ;
        case erhe::toolkit::Key_f7           : return ImGuiKey_F7            ;
        case erhe::toolkit::Key_f8           : return ImGuiKey_F8            ;
        case erhe::toolkit::Key_f9           : return ImGuiKey_F9            ;
        case erhe::toolkit::Key_f10          : return ImGuiKey_F10           ;
        case erhe::toolkit::Key_f11          : return ImGuiKey_F11           ;
        case erhe::toolkit::Key_f12          : return ImGuiKey_F12           ;
        case erhe::toolkit::Key_apostrophe   : return ImGuiKey_Apostrophe    ;
        case erhe::toolkit::Key_comma        : return ImGuiKey_Comma         ;
        case erhe::toolkit::Key_minus        : return ImGuiKey_Minus         ; // -
        case erhe::toolkit::Key_period       : return ImGuiKey_Period        ; // .
        case erhe::toolkit::Key_slash        : return ImGuiKey_Slash         ; // /
        case erhe::toolkit::Key_semicolon    : return ImGuiKey_Semicolon     ; // ;
        case erhe::toolkit::Key_equal        : return ImGuiKey_Equal         ; // =
        case erhe::toolkit::Key_left_bracket : return ImGuiKey_LeftBracket   ; // [
        case erhe::toolkit::Key_backslash    : return ImGuiKey_Backslash     ; // \ (this text inhibit multiline comment caused by backslash)
        case erhe::toolkit::Key_right_bracket: return ImGuiKey_RightBracket  ; // ]
        case erhe::toolkit::Key_grave_accent : return ImGuiKey_GraveAccent   ; // `
        case erhe::toolkit::Key_caps_lock    : return ImGuiKey_CapsLock      ;
        case erhe::toolkit::Key_scroll_lock  : return ImGuiKey_ScrollLock    ;
        case erhe::toolkit::Key_num_lock     : return ImGuiKey_NumLock       ;
        case erhe::toolkit::Key_print_screen : return ImGuiKey_PrintScreen   ;
        case erhe::toolkit::Key_pause        : return ImGuiKey_Pause         ;
        case erhe::toolkit::Key_kp_0         : return ImGuiKey_Keypad0       ;
        case erhe::toolkit::Key_kp_1         : return ImGuiKey_Keypad1       ;
        case erhe::toolkit::Key_kp_2         : return ImGuiKey_Keypad2       ;
        case erhe::toolkit::Key_kp_3         : return ImGuiKey_Keypad3       ;
        case erhe::toolkit::Key_kp_4         : return ImGuiKey_Keypad4       ;
        case erhe::toolkit::Key_kp_5         : return ImGuiKey_Keypad5       ;
        case erhe::toolkit::Key_kp_6         : return ImGuiKey_Keypad6       ;
        case erhe::toolkit::Key_kp_7         : return ImGuiKey_Keypad7       ;
        case erhe::toolkit::Key_kp_8         : return ImGuiKey_Keypad8       ;
        case erhe::toolkit::Key_kp_9         : return ImGuiKey_Keypad9       ;
        case erhe::toolkit::Key_kp_decimal   : return ImGuiKey_KeypadDecimal ;
        case erhe::toolkit::Key_kp_divide    : return ImGuiKey_KeypadDivide  ;
        case erhe::toolkit::Key_kp_multiply  : return ImGuiKey_KeypadMultiply;
        case erhe::toolkit::Key_kp_subtract  : return ImGuiKey_KeypadSubtract;
        case erhe::toolkit::Key_kp_add       : return ImGuiKey_KeypadAdd     ;
        case erhe::toolkit::Key_kp_enter     : return ImGuiKey_KeypadEnter   ;
        case erhe::toolkit::Key_kp_equal     : return ImGuiKey_KeypadEqual   ;
        default                              : return ImGuiKey_None          ;
    }
}

void update_key_modifiers(ImGuiIO& io, const uint32_t modifier_mask)
{
    io.AddKeyEvent(ImGuiMod_Ctrl,  (modifier_mask & erhe::toolkit::Key_modifier_bit_ctrl ) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifier_mask & erhe::toolkit::Key_modifier_bit_shift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt,   (modifier_mask & erhe::toolkit::Key_modifier_bit_menu ) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (modifier_mask & erhe::toolkit::Key_modifier_bit_super) != 0);
}

}

Imgui_viewport::Imgui_viewport(
    erhe::rendergraph::Rendergraph& rendergraph,
    Imgui_windows&                  imgui_windows,
    const std::string_view          name,
    const bool                      imgui_ini,
    ImFontAtlas*                    font_atlas
)
    : Rendergraph_node{rendergraph, fmt::format("Viewport {}", name)}
    , m_imgui_ini_path{imgui_ini ? fmt::format("imgui_{}.ini", name) : ""}
    , m_imgui_windows {imgui_windows}
{
    IMGUI_CHECKVERSION();
    m_imgui_context = ImGui::CreateContext(font_atlas);

    ImGuiIO& io = m_imgui_context->IO;
    io.IniFilename = imgui_ini ? m_imgui_ini_path.c_str() : nullptr;

    // "window" is slot / pseudo-resource which allows use rendergraph connection
    // to make Imgui_viewport_window a dependency for Imgui_viewport, forcing
    // correct rendering order (Imgui_viewport_window must be rendered before
    // Imgui_viewport).
    //
    // TODO Imgui_renderer should carry dependencies using Rendergraph.
    register_input(
        erhe::rendergraph::Resource_routing::None,
        "window",
        erhe::rendergraph::Rendergraph_node_key::window
    );

    imgui_windows.register_imgui_viewport(this);
}

Imgui_viewport::~Imgui_viewport()
{
    m_imgui_windows.unregister_imgui_viewport(this);

    ImGui::DestroyContext(m_imgui_context);
    m_imgui_context = nullptr;
}

[[nodiscard]] auto Imgui_viewport::get_imgui_windows() -> Imgui_windows&
{
    return m_imgui_windows;
}

[[nodiscard]] auto Imgui_viewport::name() const -> const std::string&
{
    return m_name;
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
auto Imgui_viewport::want_capture_keyboard() const -> bool
{
    ImGuiIO& io = m_imgui_context->IO;
    return io.WantCaptureKeyboard && !m_request_keyboard;
}

auto Imgui_viewport::want_capture_mouse() const -> bool
{
    ImGuiIO& io = m_imgui_context->IO;
    return io.WantCaptureMouse && !m_request_mouse;
}

[[nodiscard]] auto Imgui_viewport::has_cursor() const -> bool
{
    return m_has_cursor;
}

[[nodiscard]] auto Imgui_viewport::imgui_context() const -> ImGuiContext*
{
    return m_imgui_context;
}

[[nodiscard]] auto Imgui_viewport::get_scale_value() const -> float
{
    return 1.0f;
}

void Imgui_viewport::update_input_request(
    const bool request_keyboard,
    const bool request_mouse
)
{
    m_request_keyboard = request_keyboard;
    m_request_mouse    = request_mouse;
}

auto Imgui_viewport::get_mouse_position() const -> glm::vec2
{
    ImGuiIO& io = m_imgui_context->IO;
    return glm::vec2{io.MousePos.x, io.MousePos.y};
}

void Imgui_viewport::builtin_imgui_window_menu()
{
    if (ImGui::BeginMenu("ImGui"))
    {
        ImGui::MenuItem("Demo",             "", &m_imgui_builtin_windows.demo);
        ImGui::MenuItem("Style Editor",     "", &m_imgui_builtin_windows.style_editor);
        ImGui::MenuItem("Metrics/Debugger", "", &m_imgui_builtin_windows.metrics);
        ImGui::MenuItem("Stack Tool",       "", &m_imgui_builtin_windows.stack_tool);
        ImGui::EndMenu();
    }
}

void Imgui_viewport::menu()
{
    if (ImGui::BeginMainMenuBar()) {
        m_imgui_windows.window_menu(this);
        ImGui::EndMainMenuBar();
    }
    if (m_imgui_builtin_windows.demo) {
        ImGui::ShowDemoWindow(&m_imgui_builtin_windows.demo);
    }

    if (m_imgui_builtin_windows.style_editor) {
        ImGui::Begin("Dear ImGui Style Editor", &m_imgui_builtin_windows.style_editor);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }

    if (m_imgui_builtin_windows.metrics) {
        ImGui::ShowMetricsWindow(&m_imgui_builtin_windows.metrics);
    }

    if (m_imgui_builtin_windows.stack_tool) {
        ImGui::ShowStackToolWindow(&m_imgui_builtin_windows.stack_tool);
    }
}

#pragma region Events
void Imgui_viewport::on_focus(const int focused)
{
    std::lock_guard<std::mutex> lock{m_event_mutex};
    m_events.push_back(
        Imgui_event{
            .type = Imgui_event_type::focus_event,
            .u = {
                .focus_event = {
                    .focused = focused
                }
            }
        }
    );
}

void Imgui_viewport::on_event(const Focus_event& focus_event)
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddFocusEvent(focus_event.focused != 0);
}

void Imgui_viewport::on_cursor_enter(const int entered)
{
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "imgui viewport {} on_cursor_enter(entered = {})",
        m_name,
        entered
    );

    std::lock_guard<std::mutex> lock{m_event_mutex};
    m_events.push_back(
        Imgui_event{
            .type = Imgui_event_type::cursor_enter_event,
            .u = {
                .cursor_enter_event = {
                    .entered = entered
                }
            }
        }
    );
}

void Imgui_viewport::on_event(const Cursor_enter_event& cursor_enter_event)
{
    m_has_cursor = cursor_enter_event.entered != 0;
    ImGuiIO& io = m_imgui_context->IO;
    io.AddFocusEvent(m_has_cursor);
}

void Imgui_viewport::on_mouse_move(
    const float x,
    const float y
)
{
    std::lock_guard<std::mutex> lock{m_event_mutex};
    m_events.push_back(
        Imgui_event{
            .type = Imgui_event_type::mouse_move_event,
            .u = {
                .mouse_move_event = {
                    .x = x,
                    .y = y
                }
            }
        }
    );
}

void Imgui_viewport::on_event(const Mouse_move_event& mouse_move_event)
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddMousePosEvent(mouse_move_event.x, mouse_move_event.y);
}

void Imgui_viewport::on_mouse_button(
    const uint32_t button,
    const bool     pressed
)
{
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "imgui viewport {} on_mouse_button(button = {}, action = {})",
        m_name,
        button,
        pressed ? "pressed" : "released"
    );

    std::lock_guard<std::mutex> lock{m_event_mutex};
    m_events.push_back(
        Imgui_event{
            .type = Imgui_event_type::mouse_button_event,
            .u = {
                .mouse_button_event = {
                    .button  = button,
                    .pressed = pressed
                }
            }
        }
    );
}

void Imgui_viewport::on_event(const Mouse_button_event& mouse_button_event)
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddMouseButtonEvent(mouse_button_event.button, mouse_button_event.pressed);
}

void Imgui_viewport::on_mouse_wheel(
    const float x,
    const float y
)
{
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "imgui viewport {} on_mouse_wheel(x = {}, y = {})",
        m_name,
        x,
        y
    );

    std::lock_guard<std::mutex> lock{m_event_mutex};
    m_events.push_back(
        Imgui_event{
            .type = Imgui_event_type::mouse_wheel_event,
            .u = {
                .mouse_wheel_event = {
                    .x = x,
                    .y = y
                }
            }
        }
    );
}

void Imgui_viewport::on_event(const Mouse_wheel_event& mouse_wheel_event)
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddMouseWheelEvent(mouse_wheel_event.x, mouse_wheel_event.y);
}

void Imgui_viewport::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
)
{
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "imgui viewport {} on_key(keycode = {}, modifier_mask = {:04x}, pressed = {})",
        m_name,
        keycode,
        modifier_mask,
        pressed
    );

    std::lock_guard<std::mutex> lock{m_event_mutex};
    m_events.push_back(
        Imgui_event{
            .type = Imgui_event_type::key_event,
            .u = {
                .key_event = {
                    .keycode       = keycode,
                    .modifier_mask = modifier_mask,
                    .pressed       = pressed
                }
            }
        }
    );
}

void Imgui_viewport::on_event(const Key_event& event)
{
    using erhe::toolkit::Keycode;

    ImGuiIO& io = m_imgui_context->IO;

    update_key_modifiers(io, event.modifier_mask);
    io.AddKeyEvent(from_erhe(event.keycode), event.pressed);
}

void Imgui_viewport::on_char(
    const unsigned int codepoint
)
{
    SPDLOG_LOGGER_TRACE(
        log_imgui,
        "imgui viewport {} on_char(codepoint = {})",
        m_name,
        codepoint
    );

    std::lock_guard<std::mutex> lock{m_event_mutex};
    m_events.push_back(
        Imgui_event{
            .type = Imgui_event_type::char_event,
            .u = {
                .char_event = {
                    .codepoint = codepoint
                }
            }
        }
    );
}

void Imgui_viewport::on_event(const Char_event& char_event)
{
    ImGuiIO& io = m_imgui_context->IO;
    io.AddInputCharacter(char_event.codepoint);
}

void Imgui_viewport::flush_queud_events()
{
    std::lock_guard<std::mutex> lock{m_event_mutex};
    for (const auto& event : m_events) {
        switch (event.type) {
            case Imgui_event_type::key_event         : on_event(event.u.key_event); break;
            case Imgui_event_type::char_event        : on_event(event.u.char_event); break;
            case Imgui_event_type::focus_event       : on_event(event.u.focus_event); break;
            case Imgui_event_type::cursor_enter_event: on_event(event.u.cursor_enter_event); break;
            case Imgui_event_type::mouse_move_event  : on_event(event.u.mouse_move_event); break;
            case Imgui_event_type::mouse_button_event: on_event(event.u.mouse_button_event); break;
            case Imgui_event_type::mouse_wheel_event : on_event(event.u.mouse_wheel_event); break;
            default: break;
        }
    }
    m_events.clear();
}

#pragma endregion Events

#else
auto Imgui_viewport::want_capture_mouse() const -> bool
{
    return false;
}

void Imgui_viewport::on_focus(int focused)
{
    static_cast<void>(focused);
}

void Imgui_viewport::on_cursor_enter(int entered)
{
    static_cast<void>(entered);
}

void Imgui_viewport::on_mouse_button(
    const uint32_t button,
    const bool     pressed
)
{
    static_cast<void>(button);
    static_cast<void>(pressed);
}

void Imgui_viewport::on_mouse_wheel(
    const float x,
    const float y
)
{
    static_cast<void>(x);
    static_cast<void>(y);
}

void Imgui_viewport::make_imgui_context_current()
{
}

void Imgui_viewport::make_imgui_context_uncurrent()
{
}

void Imgui_viewport::on_key(
    const signed int keycode,
    const uint32_t   modifier_mask,
    const bool       pressed
)
{
    static_cast<void>(keycode);
    static_cast<void>(modifier_mask);
    static_cast<void>(pressed);
}

void Imgui_viewport::on_char(
    const unsigned int codepoint
)
{
    static_cast<void>(codepoint);
}

#endif

} // namespace erhe::imgui
