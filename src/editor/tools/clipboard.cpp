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
#include "erhe_profile//profile.hpp"

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

Clipboard::Clipboard(erhe::commands::Commands& commands, Editor_context& context, Editor_message_bus& editor_message_bus)
    : m_paste_command{commands, context}
    , m_context      {context}
{
    ERHE_PROFILE_FUNCTION();

    commands.register_command(&m_paste_command);
    commands.bind_command_to_key(&m_paste_command, erhe::window::Key_insert, true, erhe::window::Key_modifier_bit_shift);
    commands.bind_command_to_key(&m_paste_command, erhe::window::Key_v,      true, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_menu(&m_paste_command, "Edit.Paste");
    m_paste_command.set_host(this);

    editor_message_bus.add_receiver(
        [&](Editor_message& message) {
            on_message(message);
        }
    );
}

void Clipboard::on_message(Editor_message& message)
{
    using namespace erhe::bit;
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_view)) {
        m_hover_scene_view = message.scene_view;
        if (message.scene_view != nullptr) {
            m_last_hover_scene_view = message.scene_view;
            m_last_hover_scene_item_tree = nullptr;
        }
    }
    if (test_all_rhs_bits_set(message.update_flags, Message_flag_bit::c_flag_bit_hover_scene_item_tree)) {
        m_last_hover_scene_item_tree = message.scene_root;
        if (message.scene_root != nullptr) {
            m_last_hover_scene_view = nullptr;
        }
    }
}

auto Clipboard::try_ready() -> bool
{
    // TODO Paste into content library
    const auto target_node = m_context.selection->get<erhe::scene::Node>();
    return !m_contents.empty() && target_node;
}

[[nodiscard]] auto Clipboard::resolve_paste_target() -> std::shared_ptr<erhe::Hierarchy>
{
    if (m_last_hover_scene_item_tree != nullptr) {
        const std::shared_ptr<erhe::Hierarchy>& root = m_last_hover_scene_item_tree->get_scene().get_root_node();
        if (root) {
            return root;
        }
    }
    const std::shared_ptr<erhe::Hierarchy>& selected_hierarchy = m_context.selection->get<erhe::Hierarchy>();
    if (selected_hierarchy) {
        return selected_hierarchy;
    }

    const std::shared_ptr<erhe::Hierarchy>& last_selected_hierarchy = m_context.selection->get_last_selected<erhe::Hierarchy>();
    if (last_selected_hierarchy) {
        return last_selected_hierarchy;
    }

    const std::shared_ptr<Scene_root> scene_root = m_last_hover_scene_view->get_scene_root();
    if (scene_root) {
        const erhe::scene::Scene& scene = scene_root->get_scene();
        std::shared_ptr<erhe::scene::Node> root_node = scene.get_root_node();
        const std::shared_ptr<erhe::Hierarchy>& hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(root_node);
        if (hierarchy) {
            return hierarchy;
        }
    }
    return {};
}

auto Clipboard::try_paste() -> bool
{
    const std::shared_ptr<erhe::Hierarchy>& target = resolve_paste_target();
    const std::size_t index_in_parent = target->get_child_count();
    return try_paste(target, index_in_parent);
}

auto Clipboard::try_paste(const std::shared_ptr<erhe::Hierarchy>& target_parent, std::size_t index_in_parent) -> bool
{
    if (!target_parent) {
        return false;
    }
    // TODO Paste into content library

    Compound_operation::Parameters compound_parameters{};

    //if (!target_hierarchy) {
    //    target_hierarchy = m_context.selection->get<erhe::Hierarchy>();
    //}

    for (const auto& item : m_contents) {
        if (target_parent) {
            const auto& src_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(item);
            if (src_hierarchy) {
                std::shared_ptr<erhe::Item_base> clone_of_item = item->clone();
                const auto& dst_hierarchy = std::dynamic_pointer_cast<erhe::Hierarchy>(clone_of_item);

                compound_parameters.operations.push_back(
                    std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context         = m_context,
                            .item            = dst_hierarchy,
                            .parent          = target_parent,
                            .mode            = Item_insert_remove_operation::Mode::insert,
                            .index_in_parent = index_in_parent
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

void Clipboard::set_contents(const std::shared_ptr<erhe::Item_base>& item)
{
    m_contents.clear();
    m_contents.push_back(item);
}

auto Clipboard::get_contents() -> const std::vector<std::shared_ptr<erhe::Item_base>>&
{
    return m_contents;
}

}
