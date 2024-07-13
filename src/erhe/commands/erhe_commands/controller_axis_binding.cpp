#include "erhe_commands/controller_axis_binding.hpp"
#include "erhe_commands/command.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_commands/commands_log.hpp"

namespace erhe::commands {

Controller_axis_binding::Controller_axis_binding(Command* const command, const int axis, const std::optional<uint32_t> modifier_mask)
    : Command_binding{command}
    , m_axis         {axis}
    , m_modifier_mask{modifier_mask}
{
}

Controller_axis_binding::Controller_axis_binding() = default;

Controller_axis_binding::~Controller_axis_binding() noexcept = default;

auto Controller_axis_binding::get_axis() const -> int
{
    return m_axis;
}

auto Controller_axis_binding::get_modifier_mask() const -> const std::optional<uint32_t>&
{
    return m_modifier_mask;
}

auto Controller_axis_binding::get_type() const -> Type
{
    return Type::Controller_axis;
}

auto Controller_axis_binding::on_value_changed(Input_arguments& input) -> bool
{
    auto* const command = get_command();

    if (command->get_command_state() == State::Disabled) {
        return false;
    }

    if (m_modifier_mask.has_value() && m_modifier_mask.value() != input.modifier_mask) {
        log_input_event_filtered->trace("{} rejected controller axis {} due to modifier mask mismatch", command->get_name(), m_axis);
        return false;
    }

    bool consumed{false};
    command->try_ready();
    consumed = command->try_call_with_input(input);
    log_input_event_consumed->trace("{} consumed Controller Axis input event", command->get_name());
    return consumed;
}

} // namespace erhe::commands

