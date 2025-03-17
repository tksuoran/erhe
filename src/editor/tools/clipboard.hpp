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

namespace editor {

class Content_library;
class Editor_context;
class Editor_message;
class Editor_message_bus;
class Scene_view;
class Scene_root;
class Tools;

class Clipboard_paste_command : public erhe::commands::Command
{
public:
    Clipboard_paste_command(erhe::commands::Commands& commands, Editor_context& context);

    void try_ready()         override;
    auto try_call () -> bool override;

private:
    Editor_context& m_context;
};

class Clipboard : public erhe::commands::Command_host
{
public:
    Clipboard(
        erhe::commands::Commands& commands,
        Editor_context&           context,
        Editor_message_bus&       editor_message_bus
    );

    // Commands
    auto try_ready() -> bool;
    auto try_paste(const std::shared_ptr<erhe::Hierarchy>& target_parent, std::size_t index_in_parent) -> bool;
    auto try_paste() -> bool;

    // Public API
    void set_contents(const std::vector<std::shared_ptr<erhe::Item_base>>& items);
    void set_contents(const std::shared_ptr<erhe::Item_base>& item);
    auto get_contents() -> const std::vector<std::shared_ptr<erhe::Item_base>>&;

private:
    void on_message(Editor_message& message);
    [[nodiscard]] auto resolve_paste_target() -> std::shared_ptr<erhe::Hierarchy>;

    Clipboard_paste_command                       m_paste_command;
    Editor_context&                               m_context;
    std::vector<std::shared_ptr<erhe::Item_base>> m_contents;

    Scene_view*                                   m_hover_scene_view          {nullptr};
    Scene_view*                                   m_last_hover_scene_view     {nullptr};
    Scene_root*                                   m_last_hover_scene_item_tree{nullptr};
};

} // namespace editor
