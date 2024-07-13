#include "erhe_commands/menu_binding.hpp"
#include "erhe_commands/command.hpp"

namespace erhe::commands {

Menu_binding::Menu_binding(Command* const command, std::string_view menu_path, std::function<bool()> enabled_callback)
    : Command_binding{command}
    , m_menu_path{menu_path}
    , m_enabled_callback{enabled_callback}
{
}

Menu_binding::Menu_binding() = default;

auto Menu_binding::get_menu_path() const -> const std::string&
{
    return m_menu_path;
}

auto Menu_binding::get_enabled() const -> bool
{
    if (m_enabled_callback) {
        return m_enabled_callback();
    }
    return false;
}

} // namespace erhe::commands

