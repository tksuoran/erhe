#include "erhe/application/commands/command.hpp"
#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/application_log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::application {

void Command_host::set_description(const std::string_view description)
{
    m_description = description;
}

auto Command_host::get_description() const -> const char*
{
    return m_description.c_str();
}

auto Command_host::is_enabled() const -> bool
{
    return m_enabled;
}

void Command_host::set_enabled(const bool enabled)
{
    m_enabled = enabled;
}

void Command_host::enable()
{
    m_enabled = true;
}

void Command_host::disable()
{
    m_enabled = false;
}


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

auto Command::get_command_state() const -> State
{
    return m_state;
}

auto Command::get_host() const -> Command_host*
{
    return m_host;
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

void Command::set_host(Command_host* host)
{
    m_host = host;
}

auto Command::get_base_priority() const -> int
{
    switch (m_state)
    {
        //using enum State;
        case State::Active:   return 4;
        case State::Ready:    return 3;
        case State::Inactive: return 2;
        case State::Disabled: return 1;
    }
    return 0;
}

auto Command::get_host_priority() const -> int
{
   return (m_host != nullptr)
        ? m_host->get_priority()
        : 0;
 }

auto Command::get_priority() const -> int
{
    return get_base_priority() * 20 + get_host_priority();
}

}  // namespace erhe::application
