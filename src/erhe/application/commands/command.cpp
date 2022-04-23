#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/log.hpp"

#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

Command::Command(const char* name)
    : m_name{name}
{
}

Command::~Command() = default;

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

auto Command::state() const -> State
{
    return m_state;
}

void Command::set_inactive(Command_context& context)
{
    log_command_state_transition.trace("{} -> inactive\n", name());
    on_inactive(context);
    m_state = State::Inactive;
    context.view().command_inactivated(this);
};

void Command::disable(Command_context& context)
{
    log_command_state_transition.trace("{} -> disabled\n", name());
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
    log_command_state_transition.trace("{} -> enabled\n", name());
    set_inactive(context);
};

void Command::set_ready(Command_context& context)
{
    static_cast<void>(context);
    log_command_state_transition.trace("{} -> ready\n", name());
    m_state = State::Ready;
}

void Command::set_active(Command_context& context)
{
    static_cast<void>(context);
    log_command_state_transition.trace("{} -> active\n", name());
    m_state = State::Active;
}

auto Command::name() const -> const char*
{
    return m_name;
}

}  // namespace erhe::application
