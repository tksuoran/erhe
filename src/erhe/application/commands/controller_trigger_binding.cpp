#include "erhe/application/commands/controller_trigger_binding.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/application_log.hpp"

namespace erhe::application {

Controller_trigger_binding::Controller_trigger_binding(
    Command* const command,
    const float    min_to_activate,
    const float    max_to_deactivate,
    const bool     drag
)
    : Command_binding    {command}
    , m_min_to_activate  {min_to_activate}
    , m_max_to_deactivate{max_to_deactivate}
    , m_drag             {drag}
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
    , m_drag             {other.m_drag             }
    , m_active           {other.m_active           }
{
}

auto Controller_trigger_binding::operator=(Controller_trigger_binding&& other) noexcept -> Controller_trigger_binding&
{
    Command_binding::operator=(std::move(other));
    this->m_min_to_activate   = other.m_min_to_activate;
    this->m_max_to_deactivate = other.m_max_to_deactivate;
    this->m_drag              = other.m_drag;
    this->m_active            = other.m_active;
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
    auto* const command = get_command();

    if (command->get_tool_state() == State::Disabled)
    {
        return false;
    }

    if (!m_drag)
    {
        if (!m_active)
        {
            if (trigger_value >= m_min_to_activate)
            {
                m_active = true;
                if (command->get_tool_state() == State::Inactive)
                {
                    command->try_ready(context);
                }
                return false;
            }
        }
        else
        {
            if (trigger_value <= m_max_to_deactivate)
            {
                m_active = false;
                bool consumed{false};
                if (command->get_tool_state() == State::Ready)
                {
                    consumed = command->try_call(context);
                    log_input_event_consumed->trace(
                        "{} consumed controller trigger 'click'",
                        command->get_name()
                    );
                }
                command->set_inactive(context);
                return consumed;

            }
        }
        return false;
    }
    else
    {
        if (!m_active && (trigger_value >= m_min_to_activate))
        {
            if (command->get_tool_state() == State::Inactive)
            {
                command->try_ready(context);
            }
            m_active = true;
            //log_input_event_consumed->info("{} + {}", command->get_name(), trigger_value);
        }
        else if (m_active && (trigger_value <= m_max_to_deactivate))
        {
            m_active = false;
            //log_input_event_consumed->info("{} - {}", command->get_name(), trigger_value);
            command->on_inactive(context);
            return true;
        }
        if (m_active)
        {
            const bool consumed = command->try_call(context);
            log_input_event_consumed->trace(
                "{} consumed controller trigger value = {} click",
                command->get_name(),
                trigger_value
            );
            return consumed;
        }
        return false;
    }
}

} // namespace erhe::application

