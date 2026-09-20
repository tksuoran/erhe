// Mcp_server tools that drive the editor's user interface the way a user does
// (doc/plans/mcp_ui_driving.md, part B):
//
//   inject_input_events - the raw form: a JSON event list is turned into
//                         erhe::window::Input_event values and injected into
//                         the editor's Context_window, one frame offset per
//                         pass of a deferred request. They travel the ordinary
//                         path (get_input_events() ->
//                         Editor::dispatch_input_event -> the Imgui_host tree
//                         and erhe::commands), so every consumer of real input
//                         sees them.
//   mouse_click / mouse_drag / mouse_release / mouse_wheel / key_press /
//   type_text           - the gestures, in the vocabulary a user would use.
//   get_input_state     - the pointer position, held buttons and modifier mask
//                         the injected events have built up, plus whether a
//                         gesture is stepping.
//   get_transform_handles - where the transform gizmo's handles are on screen,
//                         so a drag can aim at one.
//
// There is one gesture builder (Input_gesture_builder) and one stepping path
// (Mcp_server::step_input_gesture): a tool parses its arguments, fills the
// builder, and hands the recorded list over. No tool calls another tool.
//
// Only Context_window::inject_input_event() is used (B1); the single
// synthesizer callback slot stays with Fly_camera_tool (F4).
//
// The same file holds part A, the ImGui introspection the gestures aim with:
//
//   get_imgui_hosts       - the ImGui contexts the editor runs, their size and
//                           whether they hold a recorded frame.
//   get_imgui_windows     - the windows of one host with their rectangles and
//                           docking / focus state.
//   get_imgui_items       - every item Dear ImGui submitted in one recorded
//                           frame, with its rectangle and status flags.
//   get_imgui_item_rect   - one item by window plus label (or by id), with the
//                           center to aim a click at.
//
// The last two need Dear ImGui to report its items, which erhe::imgui::
// Imgui_item_recorder receives; recording is armed for exactly the frame a
// query asks for (A3).

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "transform/handle_enums.hpp"
#include "transform/handle_visualizations.hpp"
#include "transform/transform_tool.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_imgui/imgui_host.hpp"
#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_imgui/imgui_renderer.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/window_imgui_host.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_window/window.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <glm/glm.hpp>
#include <imgui/imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

using namespace mcp_server_detail;

namespace {

// A gesture may span at most this many frames. The MCP request timeout is five
// seconds (Mcp_server::k_request_timeout) and a deferred pass costs one editor
// frame, so a longer gesture would be dropped mid-way rather than answered.
constexpr int         c_max_frame_offset = 120;
constexpr std::size_t c_max_events       = 256;

// Frames between putting the pointer somewhere and pressing a button there.
// The hover state a viewport click or a gizmo drag arms against is computed
// once per frame from the pointer position, so a press in the same frame as
// the move that put the pointer there would act on the previous hover.
constexpr int c_pointer_settle_frames = 3;

// Frames a click holds the button down. A release in the frame right after the
// press is enough for ImGui, but erhe::commands only calls a mouse button
// binding once the editor has seen the press, so the extra frame keeps a click
// working the same way in both.
constexpr int c_click_hold_frames = 2;

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

// The keyboard key a real session holds down to produce one modifier bit. SDL
// reports the bit for as long as that key is down, so a gesture that wants the
// bit presses the key and releases it again.
[[nodiscard]] auto modifier_bit_key(const uint32_t bit) -> erhe::window::Keycode
{
    switch (bit) {
        case erhe::window::Key_modifier_bit_ctrl : return erhe::window::Key_left_control;
        case erhe::window::Key_modifier_bit_shift: return erhe::window::Key_left_shift;
        case erhe::window::Key_modifier_bit_super: return erhe::window::Key_left_super;
        case erhe::window::Key_modifier_bit_menu : return erhe::window::Key_left_alt;
        default: return erhe::window::Key_unknown;
    }
}

constexpr uint32_t c_modifier_bits[] = {
    erhe::window::Key_modifier_bit_ctrl,
    erhe::window::Key_modifier_bit_shift,
    erhe::window::Key_modifier_bit_super,
    erhe::window::Key_modifier_bit_menu
};

[[nodiscard]] auto read_point(const nlohmann::json& value, glm::vec2& out_point) -> bool
{
    if (value.is_array() && (value.size() == 2) && value.at(0).is_number() && value.at(1).is_number()) {
        out_point = glm::vec2{value.at(0).get<float>(), value.at(1).get<float>()};
        return true;
    }
    if (value.is_object() && value.contains("x") && value.contains("y") && value.at("x").is_number() && value.at("y").is_number()) {
        out_point = glm::vec2{value.at("x").get<float>(), value.at("y").get<float>()};
        return true;
    }
    return false;
}

} // anonymous namespace

// Records the events of one gesture into Mcp_server::Input_gesture_steps while
// tracking the pointer, modifier and cursor-entered state they build up, so
// the B3 rules (an implicit move before every button and wheel event, one
// cursor_enter + window_focus pair at the start of a session) are applied in
// exactly one place no matter which tool is recording.
class Input_gesture_builder
{
public:
    enum class Press_state { pressed, released };

    Input_gesture_builder(Mcp_server::Input_gesture_steps& steps, const Mcp_server::Input_pointer_state& state)
        : m_steps        {steps}
        , m_pointer_x    {state.x}
        , m_pointer_y    {state.y}
        , m_pointer_known{state.position_known}
        , m_modifier_mask{state.modifier_mask}
        , m_entered      {state.entered}
    {
        m_steps.clear();
    }

    [[nodiscard]] auto get_frame           () const -> int      { return m_frame; }
    [[nodiscard]] auto get_modifier_mask   () const -> uint32_t { return m_modifier_mask; }
    [[nodiscard]] auto has_pointer_position() const -> bool     { return m_pointer_known; }
    [[nodiscard]] auto get_pointer_x       () const -> float    { return m_pointer_x; }
    [[nodiscard]] auto get_pointer_y       () const -> float    { return m_pointer_y; }
    [[nodiscard]] auto get_event_count     () const -> std::size_t { return m_steps.events.size(); }

    void advance_frames(const int count) { m_frame += count; }

    // The frame offset an event is recorded at, when a caller names one
    // explicitly (inject_input_events). Frames never go back.
    auto set_frame(const int frame) -> bool
    {
        if (frame < m_frame) {
            return false;
        }
        m_frame = frame;
        return true;
    }

    void set_modifier_mask(const uint32_t mask) { m_modifier_mask = mask; }

    // B3: the first pointer event of a session tells the editor the cursor is
    // in the window and the window has focus, so ImGui and the hover path
    // start from the state a real session starts from.
    void ensure_entered()
    {
        if (m_entered) {
            return;
        }
        m_entered = true;
        erhe::window::Input_event cursor_enter = make_event(erhe::window::Input_event_type::cursor_enter_event);
        cursor_enter.u.cursor_enter_event.entered = 1;
        push(cursor_enter);
        erhe::window::Input_event window_focus = make_event(erhe::window::Input_event_type::window_focus_event);
        window_focus.u.window_focus_event.focused = true;
        push(window_focus);
    }

    // B3: a button or wheel event happens at a position, so the pointer is
    // moved there first and no consumer has to ask the window where the cursor
    // is (F7). Already being there needs no event - and must not get one,
    // because a move between a press and a release turns a click into a drag.
    void move_to(const float x, const float y)
    {
        ensure_entered();
        if (m_pointer_known && (x == m_pointer_x) && (y == m_pointer_y)) {
            return;
        }
        move_step(x, y);
    }

    // An unconditional move, for the interpolated steps of a drag.
    void move_step(const float x, const float y)
    {
        ensure_entered();
        erhe::window::Input_event event = make_event(erhe::window::Input_event_type::mouse_move_event);
        event.u.mouse_move_event.x             = x;
        event.u.mouse_move_event.y             = y;
        event.u.mouse_move_event.dx            = m_pointer_known ? (x - m_pointer_x) : 0.0f;
        event.u.mouse_move_event.dy            = m_pointer_known ? (y - m_pointer_y) : 0.0f;
        event.u.mouse_move_event.modifier_mask = m_modifier_mask;
        push(event);
    }

    void mouse_button(const erhe::window::Mouse_button button, const Press_state state)
    {
        ensure_entered();
        erhe::window::Input_event event = make_event(erhe::window::Input_event_type::mouse_button_event);
        event.u.mouse_button_event.button        = button;
        event.u.mouse_button_event.pressed       = (state == Press_state::pressed);
        event.u.mouse_button_event.modifier_mask = m_modifier_mask;
        push(event);
    }

    void mouse_wheel(const float dx, const float dy)
    {
        ensure_entered();
        erhe::window::Input_event event = make_event(erhe::window::Input_event_type::mouse_wheel_event);
        event.u.mouse_wheel_event.x             = dx;
        event.u.mouse_wheel_event.y             = dy;
        event.u.mouse_wheel_event.modifier_mask = m_modifier_mask;
        push(event);
    }

    void key(const erhe::window::Keycode keycode, const Press_state state)
    {
        erhe::window::Input_event event = make_event(erhe::window::Input_event_type::key_event);
        event.u.key_event.keycode       = keycode;
        event.u.key_event.pressed       = (state == Press_state::pressed);
        event.u.key_event.modifier_mask = m_modifier_mask;
        push(event);
    }

    // Holding a modifier is holding its key: the bit is set for the events in
    // between, exactly as a real session reports it.
    void press_modifier_keys(const uint32_t mask)
    {
        for (const uint32_t bit : c_modifier_bits) {
            if ((mask & bit) == 0) {
                continue;
            }
            m_modifier_mask |= bit;
            key(modifier_bit_key(bit), Press_state::pressed);
        }
    }

    void release_modifier_keys(const uint32_t mask)
    {
        for (const uint32_t bit : c_modifier_bits) {
            if ((mask & bit) == 0) {
                continue;
            }
            m_modifier_mask &= ~bit;
            key(modifier_bit_key(bit), Press_state::released);
        }
    }

    // One text event per chunk that fits Text_event::utf8_text, never splitting
    // a UTF-8 sequence; each chunk gets its own frame.
    auto type_text(const std::string& text, std::string& out_error) -> bool
    {
        constexpr std::size_t c_capacity = sizeof(erhe::window::Text_event::utf8_text) - 1;
        std::size_t offset = 0;
        bool        first  = true;
        while (offset < text.size()) {
            std::size_t length = std::min(c_capacity, text.size() - offset);
            // Back up off the continuation bytes of a sequence the chunk would
            // cut in half (a continuation byte has the top bits 10).
            while ((length > 0) && ((offset + length) < text.size()) &&
                   ((static_cast<unsigned char>(text[offset + length]) & 0xc0u) == 0x80u)) {
                --length;
            }
            if (length == 0) {
                out_error = "text holds a UTF-8 sequence longer than " + std::to_string(c_capacity) + " bytes";
                return false;
            }
            if (!first) {
                advance_frames(1);
            }
            first = false;
            erhe::window::Input_event event = make_event(erhe::window::Input_event_type::text_event);
            std::memset(event.u.text_event.utf8_text, 0, sizeof(event.u.text_event.utf8_text));
            std::memcpy(event.u.text_event.utf8_text, text.data() + offset, length);
            push(event);
            offset += length;
        }
        return true;
    }

    // Records an event built elsewhere (inject_input_events parses the whole
    // Input_event vocabulary), keeping the tracked state in step with it.
    void push(const erhe::window::Input_event& event)
    {
        m_steps.events.push_back(event);
        m_steps.event_frames.push_back(m_frame);
        switch (event.type) {
            case erhe::window::Input_event_type::mouse_move_event: {
                m_pointer_x     = event.u.mouse_move_event.x;
                m_pointer_y     = event.u.mouse_move_event.y;
                m_pointer_known = true;
                m_modifier_mask = event.u.mouse_move_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::mouse_button_event: {
                m_modifier_mask = event.u.mouse_button_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::mouse_wheel_event: {
                m_modifier_mask = event.u.mouse_wheel_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::key_event: {
                m_modifier_mask = event.u.key_event.modifier_mask;
                break;
            }
            case erhe::window::Input_event_type::cursor_enter_event:
            case erhe::window::Input_event_type::window_focus_event: {
                m_entered = true;
                break;
            }
            default: break;
        }
    }

private:
    Mcp_server::Input_gesture_steps& m_steps;
    int                              m_frame        {0};
    float                            m_pointer_x    {0.0f};
    float                            m_pointer_y    {0.0f};
    bool                             m_pointer_known{false};
    uint32_t                         m_modifier_mask{0};
    bool                             m_entered      {false};
};

// The three stages every gesture tool shares -------------------------------

auto Mcp_server::input_gesture_preamble() -> std::optional<std::string>
{
    if (m_input_gesture_steps.request != nullptr) {
        if (m_input_gesture_steps.request == m_current_request) {
            // Our own deferred request came back around: the events are
            // recorded already, this pass only steps them.
            return step_input_gesture();
        }
        return make_error_content("An input gesture is already stepping; one gesture runs at a time");
    }
    if (m_context.context_window == nullptr) {
        return make_error_content("No context window to inject input events into");
    }
    return std::nullopt;
}

auto Mcp_server::commit_input_gesture() -> std::string
{
    Input_gesture_steps& steps = m_input_gesture_steps;
    if (steps.events.empty()) {
        steps.clear();
        return make_error_content("The gesture holds no events");
    }
    if (steps.events.size() > c_max_events) {
        steps.clear();
        return make_error_content("The gesture holds more than " + std::to_string(c_max_events) + " events");
    }
    const int last_frame = steps.event_frames.back();
    if (last_frame > c_max_frame_offset) {
        steps.clear();
        return make_error_content("The gesture spans more than " + std::to_string(c_max_frame_offset) + " frames");
    }
    steps.request    = m_current_request;
    steps.last_frame = last_frame;
    return step_input_gesture();
}

// Injects this pass's frame, then hands the editor a frame to dispatch and
// render it. After the last event's frame the request defers once more (R4),
// so a capture_screenshot issued right after this call sees a frame that
// already shows the gesture's result.
auto Mcp_server::step_input_gesture() -> std::string
{
    erhe::window::Context_window* const context_window = m_context.context_window;
    if (context_window == nullptr) {
        m_input_gesture_steps.clear();
        return make_error_content("No context window to inject input events into");
    }

    Input_gesture_steps& steps = m_input_gesture_steps;

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

// inject_input_events - doc/plans/mcp_ui_driving.md B2 / B3 / B4.
auto Mcp_server::action_inject_input_events(const nlohmann::json& args) -> std::string
{
    const std::optional<std::string> early = input_gesture_preamble();
    if (early.has_value()) {
        return early.value();
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

    Input_gesture_builder builder{m_input_gesture_steps, m_input_pointer_state};
    std::string           error;

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
            if (!builder.set_frame(entry.at("frame").get<int>())) {
                return make_error_content(where + "frame must not go back (events are injected in order)");
            }
        }

        if (entry.contains("modifiers")) {
            uint32_t modifier_mask = 0;
            if (!parse_modifiers(entry.at("modifiers"), modifier_mask, error)) {
                return make_error_content(where + error);
            }
            builder.set_modifier_mask(modifier_mask);
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
                event.u.key_event.modifier_mask = builder.get_modifier_mask();
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
                break;
            }
            case erhe::window::Input_event_type::cursor_enter_event: {
                event.u.cursor_enter_event.entered = entry.value("entered", 1);
                break;
            }
            case erhe::window::Input_event_type::mouse_move_event: {
                if (!entry.contains("x") || !entry.contains("y") || !entry.at("x").is_number() || !entry.at("y").is_number()) {
                    return make_error_content(where + "x and y are required for a mouse move event");
                }
                const float x = entry.at("x").get<float>();
                const float y = entry.at("y").get<float>();
                builder.ensure_entered();
                event.u.mouse_move_event.x             = x;
                event.u.mouse_move_event.y             = y;
                event.u.mouse_move_event.dx            = entry.contains("dx") ? entry.at("dx").get<float>() : (builder.has_pointer_position() ? (x - builder.get_pointer_x()) : 0.0f);
                event.u.mouse_move_event.dy            = entry.contains("dy") ? entry.at("dy").get<float>() : (builder.has_pointer_position() ? (y - builder.get_pointer_y()) : 0.0f);
                event.u.mouse_move_event.modifier_mask = builder.get_modifier_mask();
                break;
            }
            case erhe::window::Input_event_type::mouse_button_event: {
                erhe::window::Mouse_button button{erhe::window::Mouse_button_left};
                if (entry.contains("button") && !parse_mouse_button(entry.at("button"), button)) {
                    return make_error_content(where + "button is not a known mouse button name or index");
                }
                builder.ensure_entered();
                if (entry.contains("pointer_x") && entry.contains("pointer_y")) {
                    builder.move_to(entry.at("pointer_x").get<float>(), entry.at("pointer_y").get<float>());
                } else if (!builder.has_pointer_position()) {
                    return make_error_content(where + "the pointer has no position yet - move it first, or give pointer_x / pointer_y");
                }
                event.u.mouse_button_event.button        = button;
                event.u.mouse_button_event.pressed       = entry.value("pressed", true);
                event.u.mouse_button_event.modifier_mask = builder.get_modifier_mask();
                break;
            }
            case erhe::window::Input_event_type::mouse_wheel_event: {
                builder.ensure_entered();
                if (entry.contains("pointer_x") && entry.contains("pointer_y")) {
                    builder.move_to(entry.at("pointer_x").get<float>(), entry.at("pointer_y").get<float>());
                } else if (!builder.has_pointer_position()) {
                    return make_error_content(where + "the pointer has no position yet - move it first, or give pointer_x / pointer_y");
                }
                event.u.mouse_wheel_event.x             = entry.contains("x") ? entry.at("x").get<float>() : 0.0f;
                event.u.mouse_wheel_event.y             = entry.contains("y") ? entry.at("y").get<float>() : 0.0f;
                event.u.mouse_wheel_event.modifier_mask = builder.get_modifier_mask();
                break;
            }
            case erhe::window::Input_event_type::controller_axis_event: {
                event.u.controller_axis_event.controller    = entry.value("controller", 0);
                event.u.controller_axis_event.axis          = entry.value("axis", 0);
                event.u.controller_axis_event.value         = entry.value("value", 0.0f);
                event.u.controller_axis_event.modifier_mask = builder.get_modifier_mask();
                break;
            }
            case erhe::window::Input_event_type::controller_button_event: {
                event.u.controller_button_event.controller    = entry.value("controller", 0);
                event.u.controller_button_event.button        = entry.value("button", 0);
                event.u.controller_button_event.value         = entry.value("value", true);
                event.u.controller_button_event.modifier_mask = builder.get_modifier_mask();
                break;
            }
            default: {
                return make_error_content(where + "event type is not injectable");
            }
        }
        builder.push(event);
    }

    return commit_input_gesture();
}

// The gestures ---------------------------------------------------------------

namespace {

// x / y (required) and the optional modifier list every pointer gesture takes.
class Pointer_arguments
{
public:
    glm::vec2                  position     {0.0f};
    erhe::window::Mouse_button button       {erhe::window::Mouse_button_left};
    uint32_t                   modifier_mask{0};
};

[[nodiscard]] auto parse_pointer_arguments(const nlohmann::json& args, Pointer_arguments& out, std::string& out_error) -> bool
{
    if (!args.contains("x") || !args.contains("y") || !args.at("x").is_number() || !args.at("y").is_number()) {
        out_error = "x and y are required (window pixels, the space get_viewports reports)";
        return false;
    }
    out.position = glm::vec2{args.at("x").get<float>(), args.at("y").get<float>()};
    if (args.contains("button") && !parse_mouse_button(args.at("button"), out.button)) {
        out_error = "button is not a known mouse button name or index";
        return false;
    }
    if (args.contains("modifiers") && !parse_modifiers(args.at("modifiers"), out.modifier_mask, out_error)) {
        return false;
    }
    return true;
}

} // anonymous namespace

// mouse_click - move, press, release; optionally twice for a double click.
auto Mcp_server::action_mouse_click(const nlohmann::json& args) -> std::string
{
    const std::optional<std::string> early = input_gesture_preamble();
    if (early.has_value()) {
        return early.value();
    }

    Pointer_arguments pointer;
    std::string       error;
    if (!parse_pointer_arguments(args, pointer, error)) {
        return make_error_content(error);
    }
    const bool double_click = args.value("double", false);

    Input_gesture_builder builder{m_input_gesture_steps, m_input_pointer_state};
    builder.press_modifier_keys(pointer.modifier_mask);
    builder.move_to(pointer.position.x, pointer.position.y);
    builder.advance_frames(c_pointer_settle_frames);
    builder.mouse_button(pointer.button, Input_gesture_builder::Press_state::pressed);
    builder.advance_frames(c_click_hold_frames);
    builder.mouse_button(pointer.button, Input_gesture_builder::Press_state::released);
    if (double_click) {
        // ImGui is the only consumer that knows a double click; it pairs two
        // clicks that land within io.MouseDoubleClickTime (0.30 s) and
        // io.MouseDoubleClickMaxDist (6 px) of each other. The second pair
        // follows in the next frames at the same position, which satisfies
        // both at any frame rate the editor runs at.
        builder.advance_frames(1);
        builder.mouse_button(pointer.button, Input_gesture_builder::Press_state::pressed);
        builder.advance_frames(1);
        builder.mouse_button(pointer.button, Input_gesture_builder::Press_state::released);
    }
    builder.advance_frames(1);
    builder.release_modifier_keys(pointer.modifier_mask);

    return commit_input_gesture();
}

// mouse_drag - press at from, interpolated moves, release at to unless hold.
auto Mcp_server::action_mouse_drag(const nlohmann::json& args) -> std::string
{
    const std::optional<std::string> early = input_gesture_preamble();
    if (early.has_value()) {
        return early.value();
    }

    glm::vec2 from{0.0f};
    glm::vec2 to  {0.0f};
    if (!args.contains("from") || !read_point(args.at("from"), from)) {
        return make_error_content("from must be [x, y] in window pixels");
    }
    if (!args.contains("to") || !read_point(args.at("to"), to)) {
        return make_error_content("to must be [x, y] in window pixels");
    }
    erhe::window::Mouse_button button{erhe::window::Mouse_button_left};
    if (args.contains("button") && !parse_mouse_button(args.at("button"), button)) {
        return make_error_content("button is not a known mouse button name or index");
    }
    uint32_t    modifier_mask = 0;
    std::string error;
    if (args.contains("modifiers") && !parse_modifiers(args.at("modifiers"), modifier_mask, error)) {
        return make_error_content(error);
    }
    const int  frames = args.value("frames", 10);
    const bool hold   = args.value("hold", false);
    if (frames < 1) {
        return make_error_content("frames must be at least 1");
    }
    // The move steps, the lead-in and the release all cost a frame each.
    if ((frames + c_pointer_settle_frames + 4) > c_max_frame_offset) {
        return make_error_content("frames is beyond the " + std::to_string(c_max_frame_offset) + " frame gesture limit");
    }

    Input_gesture_builder builder{m_input_gesture_steps, m_input_pointer_state};
    builder.press_modifier_keys(modifier_mask);
    builder.move_to(from.x, from.y);
    builder.advance_frames(c_pointer_settle_frames);
    builder.mouse_button(button, Input_gesture_builder::Press_state::pressed);
    for (int step = 1; step <= frames; ++step) {
        const float     t     = static_cast<float>(step) / static_cast<float>(frames);
        const glm::vec2 point = from + ((to - from) * t);
        builder.advance_frames(1);
        builder.move_step(point.x, point.y);
    }
    if (!hold) {
        builder.advance_frames(1);
        builder.mouse_button(button, Input_gesture_builder::Press_state::released);
        builder.advance_frames(1);
        builder.release_modifier_keys(modifier_mask);
    }

    return commit_input_gesture();
}

// mouse_release - end a held drag where the pointer now is.
auto Mcp_server::action_mouse_release(const nlohmann::json& args) -> std::string
{
    const std::optional<std::string> early = input_gesture_preamble();
    if (early.has_value()) {
        return early.value();
    }

    erhe::window::Mouse_button button{erhe::window::Mouse_button_left};
    if (args.contains("button") && !parse_mouse_button(args.at("button"), button)) {
        return make_error_content("button is not a known mouse button name or index");
    }
    if ((m_input_pointer_state.button_mask & (1u << button)) == 0) {
        return make_error_content(std::string{"The "} + erhe::window::c_str(button) + " mouse button is not held");
    }

    Input_gesture_builder builder{m_input_gesture_steps, m_input_pointer_state};
    builder.mouse_button(button, Input_gesture_builder::Press_state::released);
    // The modifier keys a held drag pressed stay held: the gesture that
    // pressed them is the one that releases them, and a held drag's modifiers
    // are part of what the drag is.
    builder.advance_frames(1);
    builder.release_modifier_keys(m_input_pointer_state.modifier_mask);

    return commit_input_gesture();
}

// mouse_wheel - one wheel step at a position.
auto Mcp_server::action_mouse_wheel(const nlohmann::json& args) -> std::string
{
    const std::optional<std::string> early = input_gesture_preamble();
    if (early.has_value()) {
        return early.value();
    }

    Pointer_arguments pointer;
    std::string       error;
    if (!parse_pointer_arguments(args, pointer, error)) {
        return make_error_content(error);
    }
    if (!args.contains("dy") && !args.contains("dx")) {
        return make_error_content("dy (or dx) is required - the wheel delta");
    }
    const float dx = args.value("dx", 0.0f);
    const float dy = args.value("dy", 0.0f);

    Input_gesture_builder builder{m_input_gesture_steps, m_input_pointer_state};
    builder.press_modifier_keys(pointer.modifier_mask);
    builder.move_to(pointer.position.x, pointer.position.y);
    // The fly camera's zoom step is proportional to the distance of what the
    // pointer hovers, so the hover under the new pointer position has to have
    // settled before the wheel event arrives.
    builder.advance_frames(c_pointer_settle_frames);
    builder.mouse_wheel(dx, dy);
    builder.advance_frames(1);
    builder.release_modifier_keys(pointer.modifier_mask);

    return commit_input_gesture();
}

// key_press - press, hold, release, with the modifier keys held around it.
auto Mcp_server::action_key_press(const nlohmann::json& args) -> std::string
{
    const std::optional<std::string> early = input_gesture_preamble();
    if (early.has_value()) {
        return early.value();
    }

    if (!args.contains("key")) {
        return make_error_content("key is required (an erhe::window::Keycode name or value)");
    }
    erhe::window::Keycode keycode{erhe::window::Key_unknown};
    if (!parse_keycode(args.at("key"), keycode)) {
        return make_error_content("key is not a known erhe::window::Keycode name or value");
    }
    uint32_t    modifier_mask = 0;
    std::string error;
    if (args.contains("modifiers") && !parse_modifiers(args.at("modifiers"), modifier_mask, error)) {
        return make_error_content(error);
    }
    const int hold_frames = args.value("hold_frames", 1);
    if (hold_frames < 1) {
        return make_error_content("hold_frames must be at least 1");
    }
    if ((hold_frames + 4) > c_max_frame_offset) {
        return make_error_content("hold_frames is beyond the " + std::to_string(c_max_frame_offset) + " frame gesture limit");
    }

    Input_gesture_builder builder{m_input_gesture_steps, m_input_pointer_state};
    builder.press_modifier_keys(modifier_mask);
    if (modifier_mask != 0) {
        builder.advance_frames(1);
    }
    builder.key(keycode, Input_gesture_builder::Press_state::pressed);
    builder.advance_frames(hold_frames);
    builder.key(keycode, Input_gesture_builder::Press_state::released);
    if (modifier_mask != 0) {
        builder.advance_frames(1);
        builder.release_modifier_keys(modifier_mask);
    }

    return commit_input_gesture();
}

// type_text - the string as text events, one chunk per frame.
auto Mcp_server::action_type_text(const nlohmann::json& args) -> std::string
{
    const std::optional<std::string> early = input_gesture_preamble();
    if (early.has_value()) {
        return early.value();
    }

    if (!args.contains("text") || !args.at("text").is_string()) {
        return make_error_content("text is required");
    }
    const std::string text = args.at("text").get<std::string>();
    if (text.empty()) {
        return make_error_content("text must not be empty");
    }

    Input_gesture_builder builder{m_input_gesture_steps, m_input_pointer_state};
    std::string           error;
    if (!builder.type_text(text, error)) {
        m_input_gesture_steps.clear();
        return make_error_content(error);
    }

    return commit_input_gesture();
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

// get_transform_handles ------------------------------------------------------
//
// Where the transform gizmo's handles are, in the window pixels mouse_drag
// aims at. The gizmo is drawn and hit tested analytically
// (Handle_visualizations::pick) with no meshes, so there is no scene object to
// query for a handle's position. Rather than restate the gizmo's geometry
// here, this runs the editor's own pick over a grid of rays through the
// gizmo's screen area and reports, per handle, the sample deepest inside that
// handle's own region - a point that by construction picks that handle.

namespace {

// Grid resolution over the gizmo's screen box. 81 x 81 rays is enough to land
// several samples inside the thinnest handle (a rotate ring arc) and cheap
// enough for a debug query that only runs when asked.
constexpr int c_handle_probe_samples = 81;
// How far past the gizmo radius the probed box reaches - the translate arrows
// and the view-rotate ring sit outside the rotate sphere.
constexpr float c_handle_probe_margin = 1.6f;

class Handle_probe_cell
{
public:
    Handle handle{Handle::e_handle_none};
    int    depth {0};
};

} // anonymous namespace

auto Mcp_server::query_transform_handles(const nlohmann::json& args) -> std::string
{
    if ((m_context.transform_tool == nullptr) || (m_context.scene_views == nullptr)) {
        return make_error_content("Transform tool or scene views are not available");
    }
    Handle_visualizations* const visualizations = m_context.transform_tool->shared.get_visualizations();
    if (visualizations == nullptr) {
        return make_error_content("The transform gizmo is not ready yet");
    }

    const std::string wanted_title = args.value("viewport", std::string{});
    std::shared_ptr<Viewport_scene_view> scene_view;
    std::string                          viewport_title;
    for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> candidate = viewport_window->viewport_scene_view();
        if (!candidate || !candidate->get_camera() || (candidate->get_window_viewport().width < 1)) {
            continue;
        }
        if (!wanted_title.empty() && (viewport_window->get_title() != wanted_title)) {
            continue;
        }
        scene_view     = candidate;
        viewport_title = viewport_window->get_title();
        break;
    }
    if (!scene_view) {
        return make_error_content(
            wanted_title.empty()
                ? std::string{"There is no viewport showing a scene through a camera"}
                : ("There is no viewport named '" + wanted_title + "'")
        );
    }

    // The same two calls Transform_tool::evaluate_handles_for() makes before
    // picking: the gizmo's one view state is what the pick is made against.
    visualizations->update_for_view(scene_view.get());
    visualizations->update_transforms();

    const std::shared_ptr<erhe::scene::Camera> camera = scene_view->get_camera();
    const glm::vec3 eye_position     = visualizations->get_eye(*camera.get());
    const glm::vec3 camera_position  = glm::vec3{camera->position_in_world()};
    const glm::vec3 anchor           = m_context.transform_tool->shared.world_from_anchor.get_translation();
    const float     gizmo_radius     = visualizations->get_gizmo_radius();
    if (!(gizmo_radius > 0.0f) || !std::isfinite(gizmo_radius)) {
        return make_error_content("The transform gizmo has no target (select something first)");
    }

    const erhe::math::Viewport&             window_viewport = scene_view->get_window_viewport();
    const erhe::math::Coordinate_conventions conventions    = scene_view->get_conventions();
    const bool flip_y = (conventions.framebuffer_origin == erhe::math::Framebuffer_origin::bottom_left);
    const auto window_from_viewport = [&](const glm::vec2 position_in_viewport) -> glm::vec2 {
        const float content_y = flip_y
            ? (static_cast<float>(window_viewport.height) - position_in_viewport.y)
            : position_in_viewport.y;
        return glm::vec2{
            position_in_viewport.x + static_cast<float>(window_viewport.x),
            content_y              + static_cast<float>(window_viewport.y)
        };
    };

    // The gizmo's screen box: the anchor plus the radius along both camera
    // axes, projected.
    const std::optional<glm::vec3> anchor_projected = scene_view->project_to_viewport(anchor);
    if (!anchor_projected.has_value()) {
        return make_error_content("The transform gizmo anchor does not project into the viewport");
    }
    const glm::vec3 camera_right = glm::vec3{camera->world_from_node()[0]};
    const glm::vec3 camera_up    = glm::vec3{camera->world_from_node()[1]};
    float half_extent = 0.0f;
    for (const glm::vec3& offset : {camera_right * gizmo_radius, camera_up * gizmo_radius}) {
        const std::optional<glm::vec3> projected = scene_view->project_to_viewport(anchor + offset);
        if (!projected.has_value()) {
            continue;
        }
        half_extent = std::max(half_extent, glm::length(glm::vec2{projected.value()} - glm::vec2{anchor_projected.value()}));
    }
    half_extent *= c_handle_probe_margin;
    if (!(half_extent > 1.0f)) {
        return make_error_content("The transform gizmo is too small on screen to probe");
    }

    const glm::vec2 box_min = glm::vec2{anchor_projected.value()} - glm::vec2{half_extent};
    const float     step    = (2.0f * half_extent) / static_cast<float>(c_handle_probe_samples - 1);

    // One pick per grid sample.
    std::vector<Handle_probe_cell> cells;
    cells.resize(static_cast<std::size_t>(c_handle_probe_samples) * static_cast<std::size_t>(c_handle_probe_samples));
    const auto sample_position = [&](const int ix, const int iy) -> glm::vec2 {
        return glm::vec2{
            box_min.x + (step * static_cast<float>(ix)),
            box_min.y + (step * static_cast<float>(iy))
        };
    };
    const auto pick_at = [&](const glm::vec2 position_in_viewport) -> std::optional<Handle_pick> {
        const std::optional<glm::vec3> near_point = scene_view->unproject_to_world(glm::vec3{position_in_viewport, 0.0f});
        const std::optional<glm::vec3> far_point  = scene_view->unproject_to_world(glm::vec3{position_in_viewport, 1.0f});
        if (!near_point.has_value() || !far_point.has_value()) {
            return std::nullopt;
        }
        // Reverse depth puts the near plane at depth 1, so the ray origin is
        // whichever of the two unprojected points is nearer to the camera.
        const bool      first_is_origin = glm::distance(near_point.value(), camera_position) <= glm::distance(far_point.value(), camera_position);
        const glm::vec3 origin          = first_is_origin ? near_point.value() : far_point.value();
        const glm::vec3 other           = first_is_origin ? far_point.value()  : near_point.value();
        const glm::vec3 direction       = other - origin;
        if (glm::length(direction) < 1.0e-6f) {
            return std::nullopt;
        }
        return visualizations->pick(eye_position, origin, glm::normalize(direction));
    };

    for (int iy = 0; iy < c_handle_probe_samples; ++iy) {
        for (int ix = 0; ix < c_handle_probe_samples; ++ix) {
            const std::optional<Handle_pick> pick = pick_at(sample_position(ix, iy));
            if (pick.has_value()) {
                cells[(static_cast<std::size_t>(iy) * c_handle_probe_samples) + ix].handle = pick->handle;
            }
        }
    }

    // Erode: a cell's depth grows while all eight of its neighbours carry the
    // same handle at the previous depth. The deepest cell of a handle is the
    // one furthest from any other handle's region and from the region's edge,
    // which is the point to aim a drag at.
    const auto cell_at = [&](const int ix, const int iy) -> const Handle_probe_cell& {
        return cells[(static_cast<std::size_t>(iy) * c_handle_probe_samples) + ix];
    };
    for (int depth = 0; depth < (c_handle_probe_samples / 2); ++depth) {
        bool grew = false;
        for (int iy = 1; iy < (c_handle_probe_samples - 1); ++iy) {
            for (int ix = 1; ix < (c_handle_probe_samples - 1); ++ix) {
                Handle_probe_cell& cell = cells[(static_cast<std::size_t>(iy) * c_handle_probe_samples) + ix];
                if ((cell.handle == Handle::e_handle_none) || (cell.depth != depth)) {
                    continue;
                }
                bool all = true;
                for (int dy = -1; (dy <= 1) && all; ++dy) {
                    for (int dx = -1; (dx <= 1) && all; ++dx) {
                        const Handle_probe_cell& neighbor = cell_at(ix + dx, iy + dy);
                        all = (neighbor.handle == cell.handle) && (neighbor.depth >= depth);
                    }
                }
                if (all) {
                    cell.depth = depth + 1;
                    grew       = true;
                }
            }
        }
        if (!grew) {
            break;
        }
    }

    // Best cell per handle.
    std::vector<Handle_probe_cell> best_cells;
    std::vector<int>               best_index;
    for (int iy = 0; iy < c_handle_probe_samples; ++iy) {
        for (int ix = 0; ix < c_handle_probe_samples; ++ix) {
            const Handle_probe_cell& cell = cell_at(ix, iy);
            if (cell.handle == Handle::e_handle_none) {
                continue;
            }
            const int index = (iy * c_handle_probe_samples) + ix;
            bool      found = false;
            for (std::size_t i = 0; i < best_cells.size(); ++i) {
                if (best_cells[i].handle != cell.handle) {
                    continue;
                }
                found = true;
                if (cell.depth > best_cells[i].depth) {
                    best_cells[i] = cell;
                    best_index[i] = index;
                }
                break;
            }
            if (!found) {
                best_cells.push_back(cell);
                best_index.push_back(index);
            }
        }
    }

    nlohmann::json handles = nlohmann::json::array();
    for (std::size_t i = 0; i < best_cells.size(); ++i) {
        const int       ix                  = best_index[i] % c_handle_probe_samples;
        const int       iy                  = best_index[i] / c_handle_probe_samples;
        const glm::vec2 position_in_viewport = sample_position(ix, iy);
        const glm::vec2 position_in_window   = window_from_viewport(position_in_viewport);
        // c_str(Handle) names the axis, not the direction: the positive and
        // negative arrow of an axis share a name. handle_value is the Handle
        // enumerator, the same value debug_set_transform_hover takes.
        nlohmann::json entry = {
            {"handle",       c_str(best_cells[i].handle)},
            {"handle_value", static_cast<unsigned int>(best_cells[i].handle)},
            {"x",            position_in_window.x},
            {"y",            position_in_window.y}
        };
        const std::optional<Handle_pick> pick = pick_at(position_in_viewport);
        if (pick.has_value()) {
            entry["world"] = {pick->position.x, pick->position.y, pick->position.z};
        }
        handles.push_back(entry);
    }

    const glm::vec2 anchor_in_window = window_from_viewport(glm::vec2{anchor_projected.value()});
    return make_json_content({
        {"viewport",     viewport_title},
        {"gizmo_radius", gizmo_radius},
        {"anchor", {
            {"x",     anchor_in_window.x},
            {"y",     anchor_in_window.y},
            {"world", {anchor.x, anchor.y, anchor.z}}
        }},
        {"handles", handles}
    }).dump();
}

// Part A: ImGui introspection ------------------------------------------------
//
// get_imgui_hosts / get_imgui_windows read live ImGui state and answer in the
// pass they are called in. get_imgui_items / get_imgui_item_rect need a frame
// in which Dear ImGui reported its items, so their first pass calls
// Imgui_host::request_item_recording() and defers; the MCP queue is drained
// before Imgui_windows::begin_frame() in the same tick, so the frame that
// records is the one right after the request, and the second pass reads it.
//
// Rectangles are in the host context's screen pixels. For the desktop host
// that is the editor window's pixel space - the same space mouse_click takes -
// so an item's center is directly a click target.

namespace {

[[nodiscard]] auto imgui_host_name(const erhe::imgui::Imgui_host& host) -> std::string
{
    return std::string{host.get_debug_label().string_view()};
}

// "Open Four View##menu_item" -> "Open Four View". ImGui hides the id part of
// a label from the user, so it is hidden from label matching too; the raw
// label is reported beside it for an item that needs the exact spelling.
[[nodiscard]] auto strip_imgui_id_suffix(const std::string_view label) -> std::string
{
    const std::size_t hash = label.find("##");
    return std::string{(hash == std::string_view::npos) ? label : label.substr(0, hash)};
}

[[nodiscard]] auto label_matches(const std::string_view label, const std::string& wanted) -> bool
{
    return (label == wanted) || (strip_imgui_id_suffix(label) == wanted);
}

// A window matches by its own name, by that name without its "##id" part, or
// by being a child region of it ("Scene Hierarchy [1]/##tree_ABCD"): the items
// of a scrolling region belong to the child window, and a caller naming the
// window they see means those too.
[[nodiscard]] auto window_matches(const char* window_name, const std::string& wanted) -> bool
{
    if (window_name == nullptr) {
        return false;
    }
    const std::string_view name{window_name};
    if (label_matches(name, wanted)) {
        return true;
    }
    return (name.size() > wanted.size()) &&
           (name.compare(0, wanted.size(), wanted) == 0) &&
           (name[wanted.size()] == '/');
}

[[nodiscard]] auto find_imgui_window_name(const ImGuiContext* context, const ImGuiID id) -> const char*
{
    for (int i = 0; i < context->Windows.Size; ++i) {
        if (context->Windows[i]->ID == id) {
            return context->Windows[i]->Name;
        }
    }
    return nullptr;
}

// R1's status set. HoveredId / ActiveId are this frame's values rather than
// the recorded frame's, which is what a caller about to click wants to know.
[[nodiscard]] auto item_status_to_json(const ImGuiContext* context, const erhe::imgui::Item_record& record) -> nlohmann::json
{
    return nlohmann::json{
        {"visible",   (record.status_flags & ImGuiItemStatusFlags_Visible     ) != 0},
        {"hovered",   ((record.status_flags & ImGuiItemStatusFlags_HoveredRect) != 0) || (context->HoveredId == record.id)},
        {"active",    context->ActiveId == record.id},
        {"edited",    (record.status_flags & ImGuiItemStatusFlags_Edited      ) != 0},
        {"checkable", (record.status_flags & ImGuiItemStatusFlags_Checkable   ) != 0},
        {"checked",   (record.status_flags & ImGuiItemStatusFlags_Checked     ) != 0},
        {"openable",  (record.status_flags & ImGuiItemStatusFlags_Openable    ) != 0},
        {"opened",    (record.status_flags & ImGuiItemStatusFlags_Opened      ) != 0},
        {"inputable", (record.status_flags & ImGuiItemStatusFlags_Inputable   ) != 0},
        {"disabled",  (record.item_flags   & ImGuiItemFlags_Disabled          ) != 0}
    };
}

[[nodiscard]] auto item_record_to_json(
    const ImGuiContext*             context,
    const erhe::imgui::Item_record& record,
    const std::string_view          label,
    const int                       index
) -> nlohmann::json
{
    const char* const window_name = find_imgui_window_name(context, record.window_id);
    nlohmann::json entry = {
        {"id",            static_cast<unsigned int>(record.id)},
        {"window",        (window_name != nullptr) ? std::string{window_name} : std::string{}},
        {"x",             record.x0},
        {"y",             record.y0},
        {"width",         record.x1 - record.x0},
        {"height",        record.y1 - record.y0},
        {"center_x",      0.5f * (record.x0 + record.x1)},
        {"center_y",      0.5f * (record.y0 + record.y1)},
        {"index",         index},
        {"status",        item_status_to_json(context, record)}
    };
    if (!label.empty()) {
        entry["label"]         = std::string{label};
        entry["display_label"] = strip_imgui_id_suffix(label);
    }
    return entry;
}

// An item a query reports by default: one Dear ImGui did not clip away. The
// window's own record (imgui.cpp registers every window as an item) carries no
// ImGuiLastItemData and therefore no Visible bit, so it is kept as well.
[[nodiscard]] auto is_item_visible(const erhe::imgui::Item_record& record) -> bool
{
    if (!record.has_item_data) {
        return true;
    }
    return (record.status_flags & ImGuiItemStatusFlags_Visible) != 0;
}

} // anonymous namespace

auto Mcp_server::resolve_imgui_host(const nlohmann::json& args, std::string& out_error) -> erhe::imgui::Imgui_host*
{
    if ((m_context.imgui_renderer == nullptr) || (m_context.imgui_windows == nullptr)) {
        out_error = "This build has no ImGui renderer or window manager";
        return nullptr;
    }
    const std::string wanted = args.value("host", std::string{});
    if (wanted.empty()) {
        erhe::imgui::Imgui_host* const host = m_context.imgui_windows->get_window_imgui_host().get();
        if (host == nullptr) {
            out_error = "There is no desktop ImGui host in this build";
            return nullptr;
        }
        return host;
    }
    const std::vector<erhe::imgui::Imgui_host*>& hosts = m_context.imgui_renderer->get_imgui_hosts();
    for (erhe::imgui::Imgui_host* const host : hosts) {
        if (imgui_host_name(*host) == wanted) {
            return host;
        }
    }
    for (erhe::imgui::Imgui_host* const host : hosts) {
        if (imgui_host_name(*host).find(wanted) != std::string::npos) {
            return host;
        }
    }
    out_error = "There is no ImGui host named '" + wanted + "' (get_imgui_hosts lists them)";
    return nullptr;
}

auto Mcp_server::request_recorded_imgui_frame(erhe::imgui::Imgui_host& host, std::string& out_error) -> bool
{
    if (m_imgui_recording_request != m_current_request) {
        if (!host.is_visible()) {
            out_error = "ImGui host '" + imgui_host_name(host) + "' is not rendering frames, so it cannot record its items";
            return false;
        }
        m_imgui_recording_request = m_current_request;
        host.request_item_recording();
        m_defer_current_request = true;
        return true;
    }
    m_imgui_recording_request = nullptr;
    if (!host.get_item_recorder().has_records()) {
        out_error = "ImGui host '" + imgui_host_name(host) + "' did not record a frame (it rendered nothing)";
        return false;
    }
    return false;
}

// get_imgui_hosts - doc/plans/mcp_ui_driving.md A6.
auto Mcp_server::query_imgui_hosts(const nlohmann::json& args) -> std::string
{
    static_cast<void>(args);
    if ((m_context.imgui_renderer == nullptr) || (m_context.imgui_windows == nullptr)) {
        return make_error_content("This build has no ImGui renderer or window manager");
    }
    const erhe::imgui::Imgui_host* const default_host = m_context.imgui_windows->get_window_imgui_host().get();

    nlohmann::json hosts = nlohmann::json::array();
    for (const erhe::imgui::Imgui_host* const host : m_context.imgui_renderer->get_imgui_hosts()) {
        const ImGuiContext* const context = host->imgui_context();
        hosts.push_back({
            {"name",             imgui_host_name(*host)},
            {"default",          host == default_host},
            {"visible",          host->is_visible()},
            {"width",            (context != nullptr) ? context->IO.DisplaySize.x : 0.0f},
            {"height",           (context != nullptr) ? context->IO.DisplaySize.y : 0.0f},
            {"window_count",     (context != nullptr) ? context->Windows.Size : 0},
            {"has_records",      host->get_item_recorder().has_records()},
            // Item recording is armed for one frame at a time; this counter
            // only moves in a frame a request armed, so an idle editor keeps
            // it at the same value (R6 verification).
            {"hook_calls_total", host->get_item_recorder().get_hook_call_count()}
        });
    }
    return make_json_content({{"hosts", hosts}}).dump();
}

// get_imgui_windows - doc/plans/mcp_ui_driving.md A4.
auto Mcp_server::query_imgui_windows(const nlohmann::json& args) -> std::string
{
    std::string error;
    erhe::imgui::Imgui_host* const host = resolve_imgui_host(args, error);
    if (host == nullptr) {
        return make_error_content(error);
    }
    const ImGuiContext* const context = host->imgui_context();
    if (context == nullptr) {
        return make_error_content("ImGui host '" + imgui_host_name(*host) + "' has no ImGui context");
    }

    nlohmann::json windows = nlohmann::json::array();
    for (int i = 0; i < context->Windows.Size; ++i) {
        const ImGuiWindow* const window = context->Windows[i];
        windows.push_back({
            {"name",          std::string{window->Name}},
            {"display_name",  strip_imgui_id_suffix(window->Name)},
            {"id",            static_cast<unsigned int>(window->ID)},
            {"x",             window->Pos.x},
            {"y",             window->Pos.y},
            {"width",         window->Size.x},
            {"height",        window->Size.y},
            {"active",        window->Active},
            {"hidden",        window->Hidden},
            {"collapsed",     window->Collapsed},
            {"child",         (window->Flags & ImGuiWindowFlags_ChildWindow) != 0},
            {"docked",        window->DockIsActive},
            {"dock_id",       static_cast<unsigned int>(window->DockId)},
            {"focused",       context->NavWindow == window}
        });
    }
    return make_json_content({
        {"host",    imgui_host_name(*host)},
        {"windows", windows}
    }).dump();
}

// get_imgui_items - doc/plans/mcp_ui_driving.md A7.
auto Mcp_server::query_imgui_items(const nlohmann::json& args) -> std::string
{
    std::string error;
    erhe::imgui::Imgui_host* const host = resolve_imgui_host(args, error);
    if (host == nullptr) {
        return make_error_content(error);
    }
    if (request_recorded_imgui_frame(*host, error)) {
        return {};
    }
    if (!error.empty()) {
        return make_error_content(error);
    }

    const ImGuiContext* const context = host->imgui_context();
    const std::string window_filter = args.value("window",         std::string{});
    const std::string label_filter  = args.value("label_contains", std::string{});
    const bool        visible_only  = args.value("visible_only",   true);
    // A frame of the editor submits thousands of items, so a query reports a
    // page of them and the total it matched.
    const int limit = args.value("limit", 200);
    if (limit < 1) {
        return make_error_content("limit must be at least 1");
    }

    const erhe::imgui::Imgui_item_recorder& recorder = host->get_item_recorder();
    nlohmann::json items = nlohmann::json::array();
    int total = 0;
    // 'index' is what get_imgui_item_rect takes to pick between items sharing
    // a label: the item's position, in submission order, among the frame's
    // items with the same display label in the same window. It is counted
    // over every record the visibility rule keeps, so the window and label
    // filters do not shift it.
    std::unordered_map<std::string, int> duplicate_index;
    for (const erhe::imgui::Item_record& record : recorder.get_records()) {
        const std::string_view label = recorder.get_label(record);
        if (visible_only && !is_item_visible(record)) {
            continue;
        }
        const std::string key   = std::to_string(record.window_id) + "\n" + strip_imgui_id_suffix(label);
        const int         index = duplicate_index[key]++;
        if (!window_filter.empty() && !window_matches(find_imgui_window_name(context, record.window_id), window_filter)) {
            continue;
        }
        if (!label_filter.empty()) {
            if (label.empty() || (strip_imgui_id_suffix(label).find(label_filter) == std::string::npos)) {
                continue;
            }
        }
        ++total;
        if (static_cast<int>(items.size()) < limit) {
            items.push_back(item_record_to_json(context, record, label, index));
        }
    }

    return make_json_content({
        {"host",      imgui_host_name(*host)},
        {"total",     total},
        {"returned",  static_cast<int>(items.size())},
        {"truncated", total > static_cast<int>(items.size())},
        {"items",     items}
    }).dump();
}

// get_imgui_item_rect - doc/plans/mcp_ui_driving.md A5.
auto Mcp_server::query_imgui_item_rect(const nlohmann::json& args) -> std::string
{
    std::string error;
    erhe::imgui::Imgui_host* const host = resolve_imgui_host(args, error);
    if (host == nullptr) {
        return make_error_content(error);
    }
    const bool        has_id = args.contains("id") && args.at("id").is_number_integer();
    const std::string label  = args.value("label", std::string{});
    if (!has_id && label.empty()) {
        return make_error_content("label (or id) is required");
    }
    if (request_recorded_imgui_frame(*host, error)) {
        return {};
    }
    if (!error.empty()) {
        return make_error_content(error);
    }

    const ImGuiContext* const context       = host->imgui_context();
    const std::string         window_filter = args.value("window", std::string{});
    const bool                visible_only  = args.value("visible_only", true);
    const ImGuiID             wanted_id     = has_id ? args.at("id").get<ImGuiID>() : 0;
    const int                 wanted_index  = args.value("index", 0);

    const erhe::imgui::Imgui_item_recorder& recorder = host->get_item_recorder();
    int                             match_count = 0;
    const erhe::imgui::Item_record* found       = nullptr;
    std::string_view                found_label;

    // Window_match::exact first: the window name get_imgui_items reports picks
    // out that one window, so an index taken from get_imgui_items means the
    // same item here. Only when no item of that exact window matches does the
    // search widen to its child regions, which is what a caller naming the
    // window they see on screen means.
    enum class Window_match { exact, with_children };
    const auto collect = [&](const Window_match window_match) {
        match_count = 0;
        found       = nullptr;
        found_label = std::string_view{};
        for (const erhe::imgui::Item_record& record : recorder.get_records()) {
            const std::string_view record_label = recorder.get_label(record);
            if (visible_only && !is_item_visible(record)) {
                continue;
            }
            if (has_id) {
                if (record.id != wanted_id) {
                    continue;
                }
            } else if (!label_matches(record_label, label)) {
                continue;
            }
            if (!window_filter.empty()) {
                const char* const window_name = find_imgui_window_name(context, record.window_id);
                const bool matched = (window_match == Window_match::exact)
                    ? ((window_name != nullptr) && label_matches(window_name, window_filter))
                    : window_matches(window_name, window_filter);
                if (!matched) {
                    continue;
                }
            }
            if (match_count == wanted_index) {
                found       = &record;
                found_label = record_label;
            }
            ++match_count;
        }
    };
    collect(Window_match::exact);
    if ((match_count == 0) && !window_filter.empty()) {
        collect(Window_match::with_children);
    }

    if (found == nullptr) {
        if (match_count == 0) {
            return make_error_content(
                has_id
                    ? ("No item with id " + std::to_string(wanted_id) + " was submitted in the recorded frame")
                    : ("No item labelled '" + label + "' was submitted in the recorded frame (get_imgui_items lists them)")
            );
        }
        return make_error_content(
            "index " + std::to_string(wanted_index) + " is beyond the " +
            std::to_string(match_count) + " matching item(s)"
        );
    }

    nlohmann::json entry = item_record_to_json(context, *found, found_label, wanted_index);
    entry["match_count"] = match_count;
    entry["host"]        = imgui_host_name(*host);
    return make_json_content(entry).dump();
}

} // namespace editor
