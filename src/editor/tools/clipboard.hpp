#pragma once

#include "erhe_commands/command.hpp"

#include <memory>
#include <vector>

namespace erhe {
    class Item_base;
    class Hierarchy;
}
namespace erhe::commands {
    class Commands;
}

namespace editor
{

class Content_library;
class Editor_context;
class Editor_message_bus;
class Tools;

class Clipboard_paste_command
    : public erhe::commands::Command
{
public:
    Clipboard_paste_command(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );

    void try_ready()         override;
    auto try_call () -> bool override;

private:
    Editor_context& m_context;
};

class Clipboard
    : public erhe::commands::Command_host
{
public:
    Clipboard(
        erhe::commands::Commands& commands,
        Editor_context&           context
    );

    // Commands
    auto try_ready() -> bool;
    auto try_paste() -> bool;

    // Public API
    void set_contents(const std::vector<std::shared_ptr<erhe::Item_base>>& items);
    auto get_contents() -> const std::vector<std::shared_ptr<erhe::Item_base>>&;

private:
    Clipboard_paste_command                       m_paste_command;
    Editor_context&                               m_context;
    std::vector<std::shared_ptr<erhe::Item_base>> m_contents;
};

} // namespace editor
