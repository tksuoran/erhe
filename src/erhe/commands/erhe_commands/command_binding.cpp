#include "erhe_commands/command_binding.hpp"
#include "erhe_commands/command.hpp"

namespace erhe::commands {

Command_binding::Command_binding(Command* const command)
    : m_command{command}
{
}

Command_binding::Command_binding() = default;

Command_binding::~Command_binding() noexcept = default;

auto Command_binding::get_command() const -> Command*
{
    return m_command;
}

auto Command_binding::is_command_host_enabled() const -> bool
{
    if (m_command == nullptr) {
        return false;
    }
    auto* const host = m_command->get_host();
    if (host == nullptr) {
        return true;
    }
    return host->is_enabled();
}

auto Command_binding::get_command_host_description() const -> const char*
{
    if (m_command == nullptr) {
        return "(no command)";
    }
    auto* const host = m_command->get_host();
    if (host == nullptr) {
        return "(no host)";
    }
    return host->get_description();
}

} // namespace erhe::commands

