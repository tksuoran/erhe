#pragma once

#include "erhe_commands/command_binding.hpp"

#include <functional>
#include <string_view>
#include <string>

namespace erhe::commands {

class Menu_binding : public Command_binding
{
public:
    Menu_binding();
    Menu_binding(Command* command, std::string_view menu_path, std::function<bool()> enabled_callback = {});

    [[nodiscard]] auto get_type() const -> Type override { return Command_binding::Type::Menu; }
    [[nodiscard]] auto get_menu_path() const -> const std::string&;
    [[nodiscard]] auto get_enabled() const -> bool;

private:
    std::string           m_menu_path;
    std::function<bool()> m_enabled_callback;
};

} // namespace erhe::commands

