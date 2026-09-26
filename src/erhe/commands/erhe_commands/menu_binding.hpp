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

    // Display label of the command's first button binding (e.g. "Ctrl+X"),
    // empty when it has none. Maintained by Commands when bindings change.
    [[nodiscard]] auto get_shortcut_label() const -> const std::string&;
    void set_shortcut_label(std::string_view label);

private:
    std::string           m_menu_path;
    std::string           m_shortcut_label;
    std::function<bool()> m_enabled_callback;
};

} // namespace erhe::commands

