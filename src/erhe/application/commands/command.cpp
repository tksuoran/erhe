#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Command::Command(const char* name)
    : m_name{name}
{
}

Command::~Command() noexcept
{
}

auto Command::try_call(Command_context& context) -> bool
{
    static_cast<void>(context);
    return false;
}

void Command::try_ready(Command_context& context)
{
    static_cast<void>(context);
}

void Command::on_inactive(Command_context& context)
{
    static_cast<void>(context);
}

auto Command::get_tool_state() const -> State
{
    return m_state;
}

void Command::set_inactive(Command_context& context)
{
    log_command_state_transition->trace(
        "{} -> inactive",
        get_name()
    );
    on_inactive(context);
    m_state = State::Inactive;
    context.commands().command_inactivated(this);
};

void Command::disable(Command_context& context)
{
    log_command_state_transition->trace("{} -> disabled", get_name());
    if (m_state == State::Active)
    {
        set_inactive(context);
    }
    m_state = State::Disabled;
};

void Command::enable(Command_context& context)
{
    if (m_state != State::Disabled)
    {
        return;
    }
    log_command_state_transition->trace("{} -> enabled", get_name());
    set_inactive(context);
};

void Command::set_ready(Command_context& context)
{
    static_cast<void>(context);
    log_command_state_transition->trace("{} -> ready", get_name());
    m_state = State::Ready;
}

void Command::set_active(Command_context& context)
{
    static_cast<void>(context);
    log_command_state_transition->trace("{} -> active", get_name());
    m_state = State::Active;
}

auto Command::get_name() const -> const char*
{
    return m_name;
}

}  // namespace erhe::application
