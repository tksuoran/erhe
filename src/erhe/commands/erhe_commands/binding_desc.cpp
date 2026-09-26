#include "erhe_commands/binding_desc.hpp"

#include "erhe_window/window_event_handler.hpp"

#include <array>
#include <cctype>
#include <charconv>

namespace erhe::commands {

namespace {

class Modifier_name
{
public:
    uint32_t    bit;
    const char* token;
    const char* display;
};

constexpr std::array<Modifier_name, 4> c_modifier_names{
    Modifier_name{erhe::window::Key_modifier_bit_ctrl,  "ctrl",  "Ctrl" },
    Modifier_name{erhe::window::Key_modifier_bit_shift, "shift", "Shift"},
    Modifier_name{erhe::window::Key_modifier_bit_menu,  "alt",   "Alt"  },
    Modifier_name{erhe::window::Key_modifier_bit_super, "super", "Super"}
};

constexpr std::array<const char*, 7> c_binding_kind_strings{
    "key",
    "mouse_button",
    "mouse_drag",
    "mouse_wheel",
    "mouse_motion",
    "controller_axis",
    "controller_button"
};

[[nodiscard]] auto has_input_code(const Binding_kind kind) -> bool
{
    return (kind != Binding_kind::mouse_wheel) && (kind != Binding_kind::mouse_motion);
}

[[nodiscard]] auto has_trigger(const Binding_kind kind) -> bool
{
    return
        (kind == Binding_kind::key) ||
        (kind == Binding_kind::mouse_button) ||
        (kind == Binding_kind::controller_button);
}

[[nodiscard]] auto parse_binding_kind(const std::string_view text) -> std::optional<Binding_kind>
{
    for (std::size_t i = 0, end = c_binding_kind_strings.size(); i < end; ++i) {
        if (text == c_binding_kind_strings[i]) {
            return static_cast<Binding_kind>(i);
        }
    }
    return {};
}

[[nodiscard]] auto parse_modifier(const std::string_view text) -> std::optional<uint32_t>
{
    for (const Modifier_name& modifier : c_modifier_names) {
        if (text == modifier.token) {
            return modifier.bit;
        }
    }
    return {};
}

[[nodiscard]] auto parse_index(const std::string_view text) -> std::optional<int>
{
    int value = 0;
    const char* const begin = text.data();
    const char* const end   = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if ((result.ec != std::errc{}) || (result.ptr != end) || (value < 0)) {
        return {};
    }
    return value;
}

// Key names use spaces ("page up"); the text form uses underscores.
[[nodiscard]] auto key_token(const erhe::window::Keycode code) -> std::string
{
    std::string name{erhe::window::c_str(code)};
    for (char& c : name) {
        if (c == ' ') {
            c = '_';
        }
    }
    return name;
}

[[nodiscard]] auto parse_input_code(const Binding_kind kind, const std::string_view text) -> std::optional<int>
{
    switch (kind) {
        case Binding_kind::key: {
            std::string name{text};
            for (char& c : name) {
                if (c == '_') {
                    c = ' ';
                }
            }
            const erhe::window::Keycode code = erhe::window::keycode_from_string(name);
            if (code == erhe::window::Key_unknown) {
                return {};
            }
            return code;
        }
        case Binding_kind::mouse_button:
        case Binding_kind::mouse_drag: {
            const erhe::window::Mouse_button button = erhe::window::mouse_button_from_string(text);
            if (button == erhe::window::Mouse_button_none) {
                return {};
            }
            return static_cast<int>(button);
        }
        case Binding_kind::controller_axis:
        case Binding_kind::controller_button: {
            return parse_index(text);
        }
        default: {
            return {};
        }
    }
}

[[nodiscard]] auto input_token(const Binding_desc& desc) -> std::string
{
    switch (desc.kind) {
        case Binding_kind::key:               return key_token(desc.code);
        case Binding_kind::mouse_button:
        case Binding_kind::mouse_drag:        return erhe::window::c_str(static_cast<erhe::window::Mouse_button>(desc.code));
        case Binding_kind::controller_axis:
        case Binding_kind::controller_button: return std::to_string(desc.code);
        default:                              return {};
    }
}

[[nodiscard]] auto display_name(const std::string& token) -> std::string
{
    std::string result;
    result.reserve(token.size());
    bool word_start = true;
    for (const char c : token) {
        if ((c == '_') || (c == ' ')) {
            result.push_back(' ');
            word_start = true;
            continue;
        }
        result.push_back(word_start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        word_start = false;
    }
    return result;
}

} // anonymous namespace

auto c_str(const Binding_kind kind) -> const char*
{
    const std::size_t index = static_cast<std::size_t>(kind);
    return (index < c_binding_kind_strings.size()) ? c_binding_kind_strings[index] : "?";
}

auto c_str(const Input_kind kind) -> const char*
{
    switch (kind) {
        case Input_kind::internal: return "internal";
        case Input_kind::button:   return "button";
        case Input_kind::drag:     return "drag";
        case Input_kind::wheel:    return "wheel";
        case Input_kind::motion:   return "motion";
        case Input_kind::axis:     return "axis";
        default:                   return "?";
    }
}

auto get_input_kind(const Binding_kind kind) -> Input_kind
{
    switch (kind) {
        case Binding_kind::key:               return Input_kind::button;
        case Binding_kind::mouse_button:      return Input_kind::button;
        case Binding_kind::controller_button: return Input_kind::button;
        case Binding_kind::mouse_drag:        return Input_kind::drag;
        case Binding_kind::mouse_wheel:       return Input_kind::wheel;
        case Binding_kind::mouse_motion:      return Input_kind::motion;
        case Binding_kind::controller_axis:   return Input_kind::axis;
        default:                              return Input_kind::internal;
    }
}

auto Binding_desc::operator==(const Binding_desc& other) const -> bool
{
    return
        (kind          == other.kind         ) &&
        (code          == other.code         ) &&
        (modifier_mask == other.modifier_mask) &&
        (trigger       == other.trigger      );
}

auto Binding_desc::overlaps(const Binding_desc& other) const -> bool
{
    if (kind != other.kind) {
        return false;
    }
    if (has_input_code(kind) && (code != other.code)) {
        return false;
    }
    if (modifier_mask.has_value() && other.modifier_mask.has_value() && (modifier_mask.value() != other.modifier_mask.value())) {
        return false;
    }
    if (has_trigger(kind)) {
        const bool trigger_overlap =
            (trigger == Button_trigger::Any) ||
            (other.trigger == Button_trigger::Any) ||
            (trigger == other.trigger);
        if (!trigger_overlap) {
            return false;
        }
    }
    return true;
}

auto Binding_desc::get_input_kind() const -> Input_kind
{
    return erhe::commands::get_input_kind(kind);
}

auto Binding_desc::to_string() const -> std::string
{
    std::string result{c_str(kind)};

    std::string chord;
    if (!modifier_mask.has_value()) {
        chord = "any";
    } else {
        for (const Modifier_name& modifier : c_modifier_names) {
            if ((modifier_mask.value() & modifier.bit) != 0) {
                if (!chord.empty()) {
                    chord.push_back('+');
                }
                chord += modifier.token;
            }
        }
    }
    if (has_input_code(kind)) {
        if (!chord.empty()) {
            chord.push_back('+');
        }
        chord += input_token(*this);
    }
    if (!chord.empty()) {
        result.push_back(':');
        result += chord;
    }

    if (has_trigger(kind)) {
        switch (trigger) {
            case Button_trigger::Button_released: result += ":released"; break;
            case Button_trigger::Any:             result += ":any";      break;
            default: break;
        }
    }
    return result;
}

auto Binding_desc::to_display_string() const -> std::string
{
    std::string result;
    if (modifier_mask.has_value()) {
        for (const Modifier_name& modifier : c_modifier_names) {
            if ((modifier_mask.value() & modifier.bit) != 0) {
                result += modifier.display;
                result.push_back('+');
            }
        }
    }
    switch (kind) {
        case Binding_kind::key:               result += display_name(input_token(*this)); break;
        case Binding_kind::mouse_button:      result += display_name(input_token(*this)) + " Click"; break;
        case Binding_kind::mouse_drag:        result += display_name(input_token(*this)) + " Drag"; break;
        case Binding_kind::mouse_wheel:       result += "Wheel"; break;
        case Binding_kind::mouse_motion:      result += "Mouse Motion"; break;
        case Binding_kind::controller_axis:   result += "Axis " + input_token(*this); break;
        case Binding_kind::controller_button: result += "Button " + input_token(*this); break;
        default: break;
    }
    if (has_trigger(kind)) {
        switch (trigger) {
            case Button_trigger::Button_released: result += " (release)"; break;
            case Button_trigger::Any:             result += " (hold)";    break;
            default: break;
        }
    }
    return result;
}

auto Binding_desc::parse(const std::string_view text) -> std::optional<Binding_desc>
{
    // Split into at most three ':' separated parts.
    std::array<std::string_view, 3> parts{};
    std::size_t part_count = 0;
    std::size_t offset = 0;
    while (true) {
        if (part_count == parts.size()) {
            return {};
        }
        const std::size_t separator = text.find(':', offset);
        if (separator == std::string_view::npos) {
            parts[part_count++] = text.substr(offset);
            break;
        }
        parts[part_count++] = text.substr(offset, separator - offset);
        offset = separator + 1;
    }

    const std::optional<Binding_kind> kind = parse_binding_kind(parts[0]);
    if (!kind.has_value()) {
        return {};
    }

    Binding_desc desc{
        .kind          = kind.value(),
        .code          = 0,
        .modifier_mask = 0u,
        .trigger       = Button_trigger::Button_pressed
    };

    const bool          needs_input = has_input_code(desc.kind);
    const std::size_t   chord_part  = 1;
    const std::size_t   max_parts   = has_trigger(desc.kind) ? 3 : 2;
    if (part_count > max_parts) {
        return {};
    }
    if (needs_input && (part_count < 2)) {
        return {};
    }

    if (part_count > chord_part) {
        const std::string_view chord = parts[chord_part];
        if (chord.empty()) {
            return {};
        }

        // Split the chord at '+'; with an input the last token is the input.
        std::size_t token_begin = 0;
        std::size_t token_index = 0;
        bool        any_modifiers = false;
        uint32_t    mask = 0u;
        while (true) {
            const std::size_t plus = chord.find('+', token_begin);
            const bool        last = (plus == std::string_view::npos);
            const std::string_view token = last ? chord.substr(token_begin) : chord.substr(token_begin, plus - token_begin);
            if (token.empty()) {
                return {};
            }
            if (last && needs_input) {
                const std::optional<int> code = parse_input_code(desc.kind, token);
                if (!code.has_value()) {
                    return {};
                }
                desc.code = code.value();
            } else if ((token == "any") && (token_index == 0)) {
                any_modifiers = true;
            } else {
                if (any_modifiers) {
                    return {}; // any+ cannot be combined with explicit modifiers
                }
                const std::optional<uint32_t> bit = parse_modifier(token);
                if (!bit.has_value()) {
                    return {};
                }
                mask = mask | bit.value();
            }
            if (last) {
                break;
            }
            token_begin = plus + 1;
            ++token_index;
        }
        if (any_modifiers) {
            desc.modifier_mask.reset();
        } else {
            desc.modifier_mask = mask;
        }
    }

    if (part_count == 3) {
        const std::string_view trigger = parts[2];
        if (trigger == "pressed") {
            desc.trigger = Button_trigger::Button_pressed;
        } else if (trigger == "released") {
            desc.trigger = Button_trigger::Button_released;
        } else if (trigger == "any") {
            desc.trigger = Button_trigger::Any;
        } else {
            return {};
        }
    }
    if ((desc.kind == Binding_kind::mouse_button) && (desc.trigger == Button_trigger::Any)) {
        return {};
    }
    return desc;
}

} // namespace erhe::commands
