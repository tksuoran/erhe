#include "erhe/application/commands/controller_trigger_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

namespace erhe::application {

Controller_trigger_binding::Controller_trigger_binding(
    Command* const command,
    const float    min_to_activate,
    const float    max_to_deactivate
)
    : Command_binding    {command}
    , m_min_to_activate  {min_to_activate}
    , m_max_to_deactivate{max_to_deactivate}
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
    , m_min_to_activate  {other.m_min_to_activate  }
    , m_max_to_deactivate{other.m_max_to_deactivate}
{
}

auto Controller_trigger_binding::operator=(Controller_trigger_binding&& other) noexcept -> Controller_trigger_binding&
{
    Command_binding::operator=(std::move(other));
    this->m_min_to_activate   = other.m_min_to_activate  ;
    this->m_max_to_deactivate = other.m_max_to_deactivate;
    return *this;
}

[[nodiscard]] auto Controller_trigger_binding::get_min_to_activate() const -> float
{
    return m_min_to_activate;
}

[[nodiscard]] auto Controller_trigger_binding::get_max_to_deactivate() const -> float
{
    return m_max_to_deactivate;
}

auto Controller_trigger_binding::on_trigger(
    Command_context& context,
    const float      trigger_value
) -> bool
{
    if (!m_active)
    {
        if (trigger_value >= m_min_to_activate)
        {
            m_active = true;
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
    }
    else
    {
        if (trigger_value <= m_max_to_deactivate)
        {
            m_active = false;
        }
    }
    return false;
}

} // namespace erhe::application

