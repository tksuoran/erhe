#include "erhe/application/commands/key_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

namespace erhe::application {

Key_binding::Key_binding(
    Command* const                command,
    const erhe::toolkit::Keycode  code,
    const bool                    pressed,
    const std::optional<uint32_t> modifier_mask
)
    : Command_binding{command      }
    , m_code         {code         }
    , m_pressed      {pressed      }
    , m_modifier_mask{modifier_mask}
{
}

Key_binding::Key_binding() = default;

Key_binding::~Key_binding() noexcept = default;

[[nodiscard]] auto Key_binding::get_keycode() const -> erhe::toolkit::Keycode
{
    return m_code;
}

[[nodiscard]] auto Key_binding::get_pressed() const -> bool
{
    return m_pressed;
}

auto Key_binding::on_key(
    Input_arguments&             input,
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
) -> bool
{
    if (
        (m_code    != code   ) ||
        (m_pressed != pressed)
    ) {
        return false;
    }

    auto* const command = get_command();

    if (
        m_modifier_mask.has_value() &&
        m_modifier_mask.value() != modifier_mask
    ) {
        log_input_event_filtered->trace(
            "{} rejected key {} due to modifier mask mismatch",
            command->get_name(),
            pressed ? "press" : "release",
            erhe::toolkit::c_str(code)
        );
        return false;
    }

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    const bool consumed = command->try_call_with_input(input);
    if (consumed) {
        log_input_event_consumed->trace(
            "{} consumed key {} {}",
            command->get_name(),
            erhe::toolkit::c_str(code),
            pressed ? "press" : "release"
        );
    }
    return consumed && pressed;
}

} // namespace erhe::application

