#include "erhe/application/commands/command_binding.hpp"
#include "erhe/application/commands/command.hpp"

namespace erhe::application {

Command_binding::Command_binding(Command* const command)
    : m_command{command}
{
}

Command_binding::Command_binding()
{
}

Command_binding::~Command_binding()
{
}

Command_binding::Command_binding(Command_binding&& other) noexcept
    : m_command{other.m_command}
    , m_id     {std::move(other.m_id)}
{
}

auto Command_binding::operator=(Command_binding&& other) noexcept -> Command_binding&
{
    m_command = other.m_command;
    m_id      = std::move(other.m_id);
    return *this;
}

auto Command_binding::get_id() const -> erhe::toolkit::Unique_id<Command_binding>::id_type
{
    return m_id.get_id();
}

auto Command_binding::get_command() const -> Command*
{
    return m_command;
}

} // namespace erhe::application

