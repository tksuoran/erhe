// Mcp_server tools that drive the editor's user interface the way a user does
// (doc/plans/mcp_ui_driving.md, part B):
//
//   inject_input_events - builds erhe::window::Input_event values from JSON and
//                         injects them into the editor's Context_window, one
//                         frame offset per pass of a deferred request. They
//                         travel the ordinary path (get_input_events() ->
//                         Editor::dispatch_input_event -> the Imgui_host tree
//                         and erhe::commands), so every consumer of real input
//                         sees them.
//   get_input_state     - the pointer position, held buttons and modifier mask
//                         the injected events have built up, plus whether a
//                         gesture is stepping.
//
// Only Context_window::inject_input_event() is used (B1); the single
// synthesizer callback slot stays with Fly_camera_tool (F4).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"

#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

// A gesture may span at most this many frames. The MCP request timeout is five
// seconds (Mcp_server::k_request_timeout) and a deferred pass costs one editor
// frame, so a longer gesture would be dropped mid-way rather than answered.
constexpr int         c_max_frame_offset = 120;
constexpr std::size_t c_max_events       = 256;

[[nodiscard]] auto now_ns() -> int64_t
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// "Key_left_shift", "left_shift", "LEFT SHIFT" -> "left shift", the spelling
// erhe::window::c_str(Keycode) returns.
[[nodiscard]] auto normalize_key_name(const std::string& text) -> std::string
{
    std::string name;
    name.reserve(text.size());
    for (const char c : text) {
        name.push_back((c == '_') ? ' ' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (name.rfind("key ", 0) == 0) {
        name.erase(0, 4);
    }
    return name;
}

// Keycode by the name erhe::window::c_str() gives it, or by its numeric value.
// The names are read back out of c_str() rather than duplicated into a table
// here, so the two can never disagree.
[[nodiscard]] auto parse_keycode(const nlohmann::json& value, erhe::window::Keycode& out_keycode) -> bool
{
    if (value.is_number_integer()) {
        out_keycode = value.get<erhe::window::Keycode>();
        return true;
    }
    if (!value.is_string()) {
        return false;
    }
    const std::string name = normalize_key_name(value.get<std::string>());
    for (erhe::window::Keycode code = erhe::window::Key_unknown; code <= erhe::window::Key_last; ++code) {
        const char* const candidate = erhe::window::c_str(code);
        if ((std::strcmp(candidate, "?") != 0) && (name == candidate)) {
            out_keycode = code;
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto parse_mouse_button(const nlohmann::json& value, erhe::window::Mouse_button& out_button) -> bool
{
    if (value.is_number_integer()) {
        const int index = value.get<int>();
        if ((index < 0) || (index >= static_cast<int>(erhe::window::Mouse_button_count))) {
            return false;
        }
        out_button = static_cast<erhe::window::Mouse_button>(index);
        return true;
    }
    if (!value.is_string()) {
        return false;
    }
    const std::string name = value.get<std::string>();
    for (erhe::window::Mouse_button button = 0; button < erhe::window::Mouse_button_count; ++button) {
        if (name == erhe::window::c_str(button)) {
            out_button = button;
            return true;
        }
    }
    return false;
}

// "ctrl" | "shift" | "super" | "menu" -> Key_modifier_mask. Returns the
// offending entry in out_error when one is not a known modifier name.
[[nodiscard]] auto parse_modifiers(const nlohmann::json& value, uint32_t& out_mask, std::string& out_error) -> bool
{
    out_mask = 0;
    if (!value.is_array()) {
        out_error = "modifiers must be an array of ctrl|shift|super|menu";
        return false;
    }
    for (const nlohmann::json& entry : value) {
        if (!entry.is_string()) {
            out_error = "modifiers must be an array of ctrl|shift|super|menu";
            return false;
        }
        const std::string name = entry.get<std::string>();
        if      (name == "ctrl" ) { out_mask |= erhe::window::Key_modifier_bit_ctrl;  }
        else if (name == "shift") { out_mask |= erhe::window::Key_modifier_bit_shift; }
        else if (name == "super") { out_mask |= erhe::window::Key_modifier_bit_super; }
        else if (name == "menu" ) { out_mask |= erhe::window::Key_modifier_bit_menu;  }
        else {
            out_error = "unknown modifier '" + name + "' (expected ctrl|shift|super|menu)";
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto modifiers_to_json(const uint32_t mask) -> nlohmann::json
{
    nlohmann::json list = nlohmann::json::array();
    if ((mask & erhe::window::Key_modifier_bit_ctrl ) != 0) { list.push_back("ctrl");  }
    if ((mask & erhe::window::Key_modifier_bit_shift) != 0) { list.push_back("shift"); }
    if ((mask & erhe::window::Key_modifier_bit_super) != 0) { list.push_back("super"); }
    if ((mask & erhe::window::Key_modifier_bit_menu ) != 0) { list.push_back("menu");  }
    return list;
}

[[nodiscard]] auto buttons_to_json(const uint32_t mask) -> nlohmann::json
{
    nlohmann::json list = nlohmann::json::array();
    for (erhe::window::Mouse_button button = 0; button < erhe::window::Mouse_button_count; ++button) {
        if ((mask & (1u << button)) != 0) {
            list.push_back(erhe::window::c_str(button));
        }
    }
    return list;
}

// The event type name, with or without the "_event" suffix the enumerator
// carries ("key" and "key_event" both name a key event).
[[nodiscard]] auto parse_event_type(const std::string& text, erhe::window::Input_event_type& out_type) -> bool
{
    using erhe::window::Input_event_type;
    std::string name = text;
    const std::string suffix{"_event"};
    if ((name.size() > suffix.size()) && (name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)) {
        name.erase(name.size() - suffix.size());
    }
    if      (name == "key"              ) { out_type = Input_event_type::key_event;               return true; }
    else if (name == "text"             ) { out_type = Input_event_type::text_event;              return true; }
    else if (name == "char"             ) { out_type = Input_event_type::char_event;              return true; }
    else if (name == "window_focus"     ) { out_type = Input_event_type::window_focus_event;      return true; }
    else if (name == "cursor_enter"     ) { out_type = Input_event_type::cursor_enter_event;      return true; }
    else if (name == "mouse_move"       ) { out_type = Input_event_type::mouse_move_event;        return true; }
    else if (name == "mouse_button"     ) { out_type = Input_event_type::mouse_button_event;      return true; }
    else if (name == "mouse_wheel"      ) { out_type = Input_event_type::mouse_wheel_event;       return true; }
    else if (name == "controller_axis"  ) { out_type = Input_event_type::controller_axis_event;   return true; }
    else if (name == "controller_button") { out_type = Input_event_type::controller_button_event; return true; }
    return false;
}

[[nodiscard]] auto make_event(const erhe::window::Input_event_type type) -> erhe::window::Input_event
{
    erhe::window::Input_event event{};
    event.type         = type;
    event.timestamp_ns = 0;
    event.handled      = false;
    return event;
}

} // anonymous namespace

// inject_input_events - doc/plans/mcp_ui_driving.md B2 / B3 / B4.
//
// Pass 1 parses the whole event list and records it; every pass injects the
// events of its own frame offset and defers, so the editor dispatches and
// renders between passes. After the last event's frame the request defers once
// more (R4), so a capture_screenshot issued right after this call sees a frame
// that already shows the gesture's result.
auto Mcp_server::action_inject_input_events(const nlohmann::json& args) -> std::string
{
    erhe::window::Context_window* const context_window = m_context.context_window;
    if (context_window == nullptr) {
        return make_error_content("No context window to inject input events into");
    }

    Input_gesture_steps& steps = m_input_gesture_steps;

    const bool continuation = (steps.request != nullptr) && (steps.request == m_current_request);
    if (!continuation) {
        if (steps.request != nullptr) {
            return make_error_content("An input gesture is already stepping; one gesture runs at a time");
        }
        if (!args.contains("events") || !args.at("events").is_array()) {
            return make_error_content("events must be an array of input events");
        }
        const nlohmann::json& events_json = args.at("events");
        if (events_json.empty()) {
            return make_error_content("events must hold at least one event");
        }
        if (events_json.size() > c_max_events) {
            return make_error_content("events holds more than " + std::to_string(c_max_events) + " entries");
        }

        steps.clear();

        // The pointer position and modifier mask the events build up as they
        // are parsed: an event without its own position or modifiers takes
        // what the events before it (and earlier calls) left.
        float    pointer_x       = m_input_pointer_state.x;
        float    pointer_y       = m_input_pointer_state.y;
        bool     pointer_known   = m_input_pointer_state.position_known;
        uint32_t modifier_mask   = m_input_pointer_state.modifier_mask;
        bool     entered         = m_input_pointer_state.entered;
        int      frame           = 0;
        std::string error;

        const auto push_event = [&steps](const erhe::window::Input_event& event, const int event_frame) {
            steps.events.push_back(event);
            steps.event_frames.push_back(event_frame);
        };

        // B3: the first pointer event of a session tells the editor the cursor
        // is in the window and the window has focus, so ImGui and the hover
        // path start from the same state a real session starts from.
        const auto ensure_entered = [&entered, &push_event, &frame]() {
            if (entered) {
                return;
            }
            entered = true;
            erhe::window::Input_event cursor_enter = make_event(erhe::window::Input_event_type::cursor_enter_event);
            cursor_enter.u.cursor_enter_event.entered = 1;
            push_event(cursor_enter, frame);
            erhe::window::Input_event window_focus = make_event(erhe::window::Input_event_type::window_focus_event);
            window_focus.u.window_focus_event.focused = true;
            push_event(window_focus, frame);
        };

        // B3: a button or wheel event is preceded, in the same frame, by a
        // move to the position it happens at, so no consumer has to ask the
        // window where the cursor is (F7).
        const auto ensure_pointer_at = [&](const float x, const float y) {
            if (pointer_known && (x == pointer_x) && (y == pointer_y)) {
                return;
            }
            erhe::window::Input_event move = make_event(erhe::window::Input_event_type::mouse_move_event);
            move.u.mouse_move_event.x             = x;
            move.u.mouse_move_event.y             = y;
            move.u.mouse_move_event.dx            = pointer_known ? (x - pointer_x) : 0.0f;
            move.u.mouse_move_event.dy            = pointer_known ? (y - pointer_y) : 0.0f;
            move.u.mouse_move_event.modifier_mask = modifier_mask;
            push_event(move, frame);
            pointer_x     = x;
            pointer_y     = y;
            pointer_known = true;
        };

        for (std::size_t index = 0; index < events_json.size(); ++index) {
            const nlohmann::json& entry = events_json.at(index);
            const std::string     where = "events[" + std::to_string(index) + "]: ";
            if (!entry.is_object()) {
                return make_error_content(where + "each event must be an object");
            }
            if (!entry.contains("type") || !entry.at("type").is_string()) {
                return make_error_content(where + "type is required");
            }
            erhe::window::Input_event_type type{};
            if (!parse_event_type(entry.at("type").get<std::string>(), type)) {
                return make_error_content(where + "unknown event type '" + entry.at("type").get<std::string>() + "'");
            }

            if (entry.contains("frame")) {
                if (!entry.at("frame").is_number_integer()) {
                    return make_error_content(where + "frame must be an integer");
                }
                const int event_frame = entry.at("frame").get<int>();
                if (event_frame < frame) {
                    return make_error_content(where + "frame must not go back (events are injected in order)");
                }
                if (event_frame > c_max_frame_offset) {
                    return make_error_content(where + "frame is beyond the " + std::to_string(c_max_frame_offset) + " frame limit");
                }
                frame = event_frame;
            }

            if (entry.contains("modifiers")) {
                if (!parse_modifiers(entry.at("modifiers"), modifier_mask, error)) {
                    return make_error_content(where + error);
                }
            }

            erhe::window::Input_event event = make_event(type);
            switch (type) {
                case erhe::window::Input_event_type::key_event: {
                    if (!entry.contains("keycode")) {
                        return make_error_content(where + "keycode is required for a key event");
                    }
                    erhe::window::Keycode keycode{erhe::window::Key_unknown};
                    if (!parse_keycode(entry.at("keycode"), keycode)) {
                        return make_error_content(where + "keycode is not a known erhe::window::Keycode name or value");
                    }
                    event.u.key_event.keycode       = keycode;
                    event.u.key_event.modifier_mask = modifier_mask;
                    event.u.key_event.pressed       = entry.value("pressed", true);
                    break;
                }
                case erhe::window::Input_event_type::text_event: {
                    const std::string text = entry.value("utf8_text", std::string{});
                    if (text.empty()) {
                        return make_error_content(where + "utf8_text is required for a text event");
                    }
                    if (text.size() >= sizeof(erhe::window::Text_event::utf8_text)) {
                        return make_error_content(
                            where + "utf8_text is longer than " +
                            std::to_string(sizeof(erhe::window::Text_event::utf8_text) - 1) + " bytes"
                        );
                    }
                    std::memset(event.u.text_event.utf8_text, 0, sizeof(event.u.text_event.utf8_text));
                    std::memcpy(event.u.text_event.utf8_text, text.data(), text.size());
                    break;
                }
                case erhe::window::Input_event_type::char_event: {
                    if (!entry.contains("codepoint") || !entry.at("codepoint").is_number_integer()) {
                        return make_error_content(where + "codepoint is required for a char event");
                    }
                    event.u.char_event.codepoint = entry.at("codepoint").get<unsigned int>();
                    break;
                }
                case erhe::window::Input_event_type::window_focus_event: {
                    event.u.window_focus_event.focused = entry.value("focused", true);
                    entered = true;
                    break;
                }
                case erhe::window::Input_event_type::cursor_enter_event: {
                    event.u.cursor_enter_event.entered = entry.value("entered", 1);
                    entered = true;
                    break;
                }
                case erhe::window::Input_event_type::mouse_move_event: {
                    if (!entry.contains("x") || !entry.contains("y") || !entry.at("x").is_number() || !entry.at("y").is_number()) {
                        return make_error_content(where + "x and y are required for a mouse move event");
                    }
                    const float x = entry.at("x").get<float>();
                    const float y = entry.at("y").get<float>();
                    ensure_entered();
                    event.u.mouse_move_event.x             = x;
                    event.u.mouse_move_event.y             = y;
                    event.u.mouse_move_event.dx            = entry.contains("dx") ? entry.at("dx").get<float>() : (pointer_known ? (x - pointer_x) : 0.0f);
                    event.u.mouse_move_event.dy            = entry.contains("dy") ? entry.at("dy").get<float>() : (pointer_known ? (y - pointer_y) : 0.0f);
                    event.u.mouse_move_event.modifier_mask = modifier_mask;
                    pointer_x     = x;
                    pointer_y     = y;
                    pointer_known = true;
                    break;
                }
                case erhe::window::Input_event_type::mouse_button_event: {
                    erhe::window::Mouse_button button{erhe::window::Mouse_button_left};
                    if (entry.contains("button") && !parse_mouse_button(entry.at("button"), button)) {
                        return make_error_content(where + "button is not a known mouse button name or index");
                    }
                    ensure_entered();
                    if (entry.contains("pointer_x") && entry.contains("pointer_y")) {
                        ensure_pointer_at(entry.at("pointer_x").get<float>(), entry.at("pointer_y").get<float>());
                    } else if (!pointer_known) {
                        return make_error_content(where + "the pointer has no position yet - move it first, or give pointer_x / pointer_y");
                    }
                    event.u.mouse_button_event.button        = button;
                    event.u.mouse_button_event.pressed       = entry.value("pressed", true);
                    event.u.mouse_button_event.modifier_mask = modifier_mask;
                    break;
                }
                case erhe::window::Input_event_type::mouse_wheel_event: {
                    ensure_entered();
                    if (entry.contains("pointer_x") && entry.contains("pointer_y")) {
                        ensure_pointer_at(entry.at("pointer_x").get<float>(), entry.at("pointer_y").get<float>());
                    } else if (!pointer_known) {
                        return make_error_content(where + "the pointer has no position yet - move it first, or give pointer_x / pointer_y");
                    }
                    event.u.mouse_wheel_event.x             = entry.contains("x") ? entry.at("x").get<float>() : 0.0f;
                    event.u.mouse_wheel_event.y             = entry.contains("y") ? entry.at("y").get<float>() : 0.0f;
                    event.u.mouse_wheel_event.modifier_mask = modifier_mask;
                    break;
                }
                case erhe::window::Input_event_type::controller_axis_event: {
                    event.u.controller_axis_event.controller    = entry.value("controller", 0);
                    event.u.controller_axis_event.axis          = entry.value("axis", 0);
                    event.u.controller_axis_event.value         = entry.value("value", 0.0f);
                    event.u.controller_axis_event.modifier_mask = modifier_mask;
                    break;
                }
                case erhe::window::Input_event_type::controller_button_event: {
                    event.u.controller_button_event.controller    = entry.value("controller", 0);
                    event.u.controller_button_event.button        = entry.value("button", 0);
                    event.u.controller_button_event.value         = entry.value("value", true);
                    event.u.controller_button_event.modifier_mask = modifier_mask;
                    break;
                }
                default: {
                    return make_error_content(where + "event type is not injectable");
                }
            }
            push_event(event, frame);
        }

        if (steps.events.size() > c_max_events) {
            steps.clear();
            return make_error_content("the implied move events push the gesture past " + std::to_string(c_max_events) + " events");
        }

        steps.request    = m_current_request;
        steps.last_frame = steps.event_frames.back();
    }

    // Inject this pass's frame, then hand the editor a frame to dispatch and
    // render it.
    const int64_t timestamp_ns = now_ns();
    while ((steps.next_event < steps.events.size()) && (steps.event_frames[steps.next_event] == steps.frame)) {
        erhe::window::Input_event event = steps.events[steps.next_event];
        event.timestamp_ns = timestamp_ns;
        context_window->inject_input_event(event);
        switch (event.type) {
            case erhe::window::Input_event_type::mouse_move_event: {
                m_input_pointer_state.x              = event.u.mouse_move_event.x;
                m_input_pointer_state.y              = event.u.mouse_move_event.y;
                m_input_pointer_state.position_known = true;
                m_input_pointer_state.modifier_mask  = event.u.mouse_move_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::mouse_button_event: {
                const uint32_t bit = 1u << event.u.mouse_button_event.button;
                if (event.u.mouse_button_event.pressed) {
                    m_input_pointer_state.button_mask |= bit;
                } else {
                    m_input_pointer_state.button_mask &= ~bit;
                }
                m_input_pointer_state.modifier_mask = event.u.mouse_button_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::mouse_wheel_event: {
                m_input_pointer_state.modifier_mask = event.u.mouse_wheel_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::key_event: {
                m_input_pointer_state.modifier_mask = event.u.key_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::cursor_enter_event: {
                m_input_pointer_state.entered = (event.u.cursor_enter_event.entered != 0);
                break;
            }
            case erhe::window::Input_event_type::window_focus_event: {
                m_input_pointer_state.entered = true;
                break;
            }
            default: break;
        }
        ++steps.next_event;
        ++steps.injected;
    }

    if (steps.frame < steps.last_frame) {
        ++steps.frame;
        m_defer_current_request = true;
        return {};
    }
    if (!steps.tail_frame) {
        // R4: one further frame, so the caller's next capture_screenshot shows
        // the result of the last event.
        steps.tail_frame = true;
        m_defer_current_request = true;
        return {};
    }

    const int injected = steps.injected;
    const int frames   = steps.last_frame + 1;
    steps.clear();

    return make_json_content({
        {"injected", injected},
        {"frames",   frames},
        {"pointer",  {
            {"x",     m_input_pointer_state.x},
            {"y",     m_input_pointer_state.y},
            {"known", m_input_pointer_state.position_known}
        }},
        {"buttons",   buttons_to_json  (m_input_pointer_state.button_mask)},
        {"modifiers", modifiers_to_json(m_input_pointer_state.modifier_mask)}
    }).dump();
}

// get_input_state - doc/plans/mcp_ui_driving.md B3.
auto Mcp_server::query_input_state(const nlohmann::json& args) -> std::string
{
    static_cast<void>(args);

    const Input_gesture_steps& steps = m_input_gesture_steps;
    const bool                 active = (steps.request != nullptr);

    return make_json_content({
        {"pointer", {
            {"x",     m_input_pointer_state.x},
            {"y",     m_input_pointer_state.y},
            {"known", m_input_pointer_state.position_known}
        }},
        {"buttons",        buttons_to_json  (m_input_pointer_state.button_mask)},
        {"modifiers",      modifiers_to_json(m_input_pointer_state.modifier_mask)},
        {"cursor_entered", m_input_pointer_state.entered},
        {"gesture", {
            {"active",           active},
            {"frame",            active ? steps.frame : 0},
            {"frames",           active ? (steps.last_frame + 1) : 0},
            {"injected",         active ? steps.injected : 0},
            {"pending_events",   active ? static_cast<int>(steps.events.size() - steps.next_event) : 0}
        }}
    }).dump();
}

} // namespace editor
