#include "erhe/application/commands/controller_trigger_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

namespace erhe::application {

Controller_trigger_binding::Controller_trigger_binding(
    Command* const command,
    const float    min_value,
    const float    max_value
)
    : Command_binding{command}
    , m_min_value    {min_value}
    , m_max_value    {max_value}
{
}

Controller_trigger_binding::Controller_trigger_binding()
{
}

Controller_trigger_binding::~Controller_trigger_binding() noexcept
{
}

Controller_trigger_binding::Controller_trigger_binding(Controller_trigger_binding&& other) noexcept
    : Command_binding{std::move(other)}
    , m_min_value    {other.m_min_value}
    , m_max_value    {other.m_max_value}
{
}

auto Controller_trigger_binding::operator=(Controller_trigger_binding&& other) noexcept -> Controller_trigger_binding&
{
    Command_binding::operator=(std::move(other));
    this->m_min_value = other.m_min_value;
    this->m_max_value = other.m_max_value;
    return *this;
}

[[nodiscard]] auto Controller_trigger_binding::get_min_value() const -> float
{
    return m_min_value;
}

[[nodiscard]] auto Controller_trigger_binding::get_max_value() const -> float
{
    return m_max_value;
}

auto Controller_trigger_binding::on_trigger(
    Command_context& context,
    const float      trigger_value
) -> bool
{
    if ((trigger_value < m_min_value) || (trigger_value > m_max_value))
    {
        return false;
    }

    auto* const command = get_command();
    if (command->state() == State::Disabled)
    {
        return false;
    }

    const bool consumed = command->try_call(context);
    log_input_event_consumed->trace(
        "{} consumed controller trigger value = {} click",
        command->name(),
        trigger_value
    );
    return consumed;
}

} // namespace erhe::application

