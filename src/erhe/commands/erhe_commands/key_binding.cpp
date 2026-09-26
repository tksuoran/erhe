#include "erhe_commands/key_binding.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands_log.hpp"

namespace erhe::commands {

Key_binding::Key_binding(
    Command* const                command,
    const erhe::window::Keycode   code,
    const Button_trigger          trigger,
    const std::optional<uint32_t> modifier_mask
)
    : Command_binding{command      }
    , m_code         {code         }
    , m_trigger      {trigger      }
    , m_modifier_mask{modifier_mask}
{
}

Key_binding::Key_binding() = default;

Key_binding::~Key_binding() noexcept = default;

auto Key_binding::get_keycode() const -> erhe::window::Keycode
{
    return m_code;
}

auto Key_binding::get_trigger() const -> Button_trigger
{
    return m_trigger;
}

auto Key_binding::get_modifier_mask() const -> std::optional<uint32_t>
{
    return m_modifier_mask;
}

auto Key_binding::on_key(Input_arguments& input, const bool pressed, const erhe::window::Keycode code, const uint32_t modifier_mask) -> bool
{
    if ((m_code != code) || !test_button_trigger(m_trigger, pressed)) {
        return false;
    }

    auto* const command = get_command();

    if (m_modifier_mask.has_value() && m_modifier_mask.value() != modifier_mask) {
        log_input_event_filtered->trace(
            "{} rejected key {} {} due to modifier mask mismatch",
            command->get_name(),
            pressed ? "press" : "release",
            erhe::window::c_str(code)
        );
        return false;
    }

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    const bool consumed = command->try_call_with_input(input);
    if (consumed) {
        log_input_event_consumed->trace("{} consumed key {} {}", command->get_name(), erhe::window::c_str(code), pressed ? "press" : "release");
    }
    // A release is never reported consumed, so every binding triggering on
    // the release of the same key sees it.
    return consumed && pressed;
}

} // namespace erhe::commands

