#include "tools/clipboard.hpp"

#include "editor_context.hpp"
#include "editor_log.hpp"
#include "editor_message_bus.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

Clipboard_paste_command::Clipboard_paste_command(erhe::commands::Commands& commands, Editor_context& context)
    : Command  {commands, "Clipboard.paste"}
    , m_context{context}
{
}

void Clipboard_paste_command::try_ready()
{
    if (m_context.clipboard->try_ready()) {
        set_ready();
    }
}

auto Clipboard_paste_command::try_call() -> bool
{
    return m_context.clipboard->try_paste();
}

Clipboard::Clipboard(erhe::commands::Commands& commands, Editor_context& context)
    : m_paste_command{commands, context}
    , m_context      {context}
{
    commands.register_command(&m_paste_command);
    commands.bind_command_to_key(&m_paste_command, erhe::window::Key_insert, true, erhe::window::Key_modifier_bit_shift);
    commands.bind_command_to_key(&m_paste_command, erhe::window::Key_v,      true, erhe::window::Key_modifier_bit_ctrl);
    m_paste_command.set_host(this);
}

auto Clipboard::try_ready() -> bool
{
    // TODO Paste into content library
    const auto target_node = m_context.selection->get<erhe::scene::Node>();
    return !m_contents.empty() && target_node;
}

auto Clipboard::try_paste() -> bool
{
    // TODO Paste into content library

    Compound_operation::Parameters compound_parameters{};

    const auto target_hierarchy = m_context.selection->get<erhe::Hierarchy>();

    for (const auto& item : m_contents) {
        if (target_hierarchy) {
            const auto& src_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
            if (src_hierarchy) {
                std::shared_ptr<erhe::Item_base> clone_of_item = item->clone();
                const auto& dst_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(clone_of_item);

                compound_parameters.operations.push_back(
                    std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context = m_context,
                            .item    = dst_hierarchy,
                            .parent  = target_hierarchy,
                            .mode    = Item_insert_remove_operation::Mode::insert
                        }
                    )
                );
            }
            //const auto& node_attachment = std::dynamic_pointer_cast<erhe::scene::Node_attachment>(item);
            //if (node_attachment) {
            //    compound_parameters.operations.push_back(
            //        std::make_shared<Node_attach_operation>(
            //            node_attachment,
            //            target_node
            //        )
            //    );
            //}
        }
    }
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(std::move(compound_parameters))
    );

    return true; // TODO
}

void Clipboard::set_contents(const std::vector<std::shared_ptr<erhe::Item_base>>& items)
{
    m_contents = items;
}

auto Clipboard::get_contents() -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    return m_contents;
}

}
