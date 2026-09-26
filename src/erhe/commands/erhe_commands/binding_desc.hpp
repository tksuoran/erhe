#pragma once

#include "erhe_commands/command_binding.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace erhe::commands {

// Physical input a user-editable binding listens to.
enum class Binding_kind : unsigned int
{
    key               = 0,
    mouse_button      = 1,
    mouse_drag        = 2,
    mouse_wheel       = 3,
    mouse_motion      = 4,
    controller_axis   = 5,
    controller_button = 6
};

// The kind of input a command consumes. Only inputs of the command's kind can
// be bound to it; internal commands (update / XR only, or unbound) are not
// user-bindable.
enum class Input_kind : unsigned int
{
    internal = 0,
    button   = 1, // key, mouse_button, controller_button
    drag     = 2, // mouse_drag
    wheel    = 3, // mouse_wheel
    motion   = 4, // mouse_motion
    axis     = 5  // controller_axis
};

[[nodiscard]] auto c_str         (Binding_kind kind) -> const char*;
[[nodiscard]] auto c_str         (Input_kind kind) -> const char*;
[[nodiscard]] auto get_input_kind(Binding_kind kind) -> Input_kind;

// Description of one binding, independent of the command it binds.
//
// code is the erhe::window::Keycode (key), erhe::window::Mouse_button
// (mouse_button, mouse_drag), axis index (controller_axis) or button index
// (controller_button); unused for mouse_wheel and mouse_motion.
//
// modifier_mask: empty matches any modifiers, a value matches exactly those
// modifiers (erhe::window::Key_modifier_bit_*).
//
// trigger: key, mouse_button and controller_button only. mouse_button does
// not support Button_trigger::Any.
//
// drag_call_on_button_down_without_motion belongs to the command, not to the
// user's choice of input: it is not part of the text form, and a user binding
// takes it from the command's default binding.
class Binding_desc
{
public:
    Binding_kind            kind         {Binding_kind::key};
    int                     code         {0};
    std::optional<uint32_t> modifier_mask{};
    Button_trigger          trigger      {Button_trigger::Button_pressed};
    bool                    drag_call_on_button_down_without_motion{false};

    [[nodiscard]] auto operator==(const Binding_desc& other) const -> bool;

    // Compact text form used by config files, the UI and MCP:
    //
    //   <kind>[:<chord>][:<trigger>]
    //
    //   kind    : key, mouse_button, mouse_drag, mouse_wheel, mouse_motion,
    //             controller_axis, controller_button
    //   chord   : [<modifier>+]...<input>. Modifiers are ctrl, shift, alt,
    //             super; a chord starting with any+ ignores modifiers.
    //             <input> is a key name (erhe::window::c_str(Keycode) with
    //             spaces as underscores), a mouse button name or an index.
    //             mouse_wheel / mouse_motion have no input, the chord is only
    //             modifiers (any, or e.g. shift, or omitted for none).
    //   trigger : released or any; omitted means pressed.
    //
    // Examples: key:ctrl+x  key:any+w:any  mouse_button:left:released
    //           mouse_drag:alt+right  mouse_wheel:any  controller_axis:any+3
    [[nodiscard]] auto to_string() const -> std::string;

    // Short human readable label, e.g. "Ctrl+X" or "Alt+Right Drag".
    [[nodiscard]] auto to_display_string() const -> std::string;

    [[nodiscard]] auto get_input_kind() const -> Input_kind;

    // True when some input event would fire both bindings: same kind and
    // code, modifier masks that can both match (equal, or either one empty)
    // and overlapping triggers.
    [[nodiscard]] auto overlaps(const Binding_desc& other) const -> bool;

    // Returns an empty optional when text is not a valid binding.
    [[nodiscard]] static auto parse(std::string_view text) -> std::optional<Binding_desc>;
};

} // namespace erhe::commands
