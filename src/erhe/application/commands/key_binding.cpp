#include "erhe/application/commands/key_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/log.hpp"

namespace erhe::application {

Key_binding::Key_binding(
    Command* const                   command,
    const erhe::toolkit::Keycode     code,
    const bool                       pressed,
    const nonstd::optional<uint32_t> modifier_mask
)
    : Command_binding{command      }
    , m_code         {code         }
    , m_pressed      {pressed      }
    , m_modifier_mask{modifier_mask}
{
}

Key_binding::Key_binding()
{
}

Key_binding::~Key_binding()
{
}

Key_binding::Key_binding(Key_binding&& other) noexcept
    : Command_binding{std::move(other)}
    , m_code         {other.m_code}
    , m_pressed      {other.m_pressed}
    , m_modifier_mask{other.m_modifier_mask}
{
}

auto Key_binding::operator=(Key_binding&& other) noexcept -> Key_binding&
{
    Command_binding::operator=(std::move(other));
    m_code          = other.m_code;
    m_pressed       = other.m_pressed;
    m_modifier_mask = other.m_modifier_mask;
    return *this;
}

auto Key_binding::on_key(
    Command_context&             context,
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
) -> bool
{
    if (
        (m_code    != code   ) ||
        (m_pressed != pressed)
    )
    {
        return false;
    }

    auto* const command = get_command();

    if (
        m_modifier_mask.has_value() &&
        m_modifier_mask.value() != modifier_mask
    )
    {
        log_input_event_filtered->trace(
            "{} rejected key {} due to modifier mask mismatch",
            command->name(),
            pressed ? "press" : "release",
            erhe::toolkit::c_str(code)
        );
        return false;
    }

    if (command->state() == State::Disabled)
    {
        return false;
    }

    const bool consumed = command->try_call(context);
    if (consumed)
    {
        log_input_event_consumed->trace(
            "{} consumed key {} {}",
            command->name(),
            erhe::toolkit::c_str(code),
            pressed ? "press" : "release"
        );
    }
    return consumed && pressed;
}

} // namespace erhe::application

