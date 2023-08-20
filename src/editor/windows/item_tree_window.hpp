#pragma once

#include "operations/compound_operation.hpp"

#include "erhe_imgui/imgui_window.hpp"
#include "erhe_item/item.hpp"
#include "erhe_item/unique_id.hpp"

#include <imgui/imgui.h>

#include <functional>
#include <memory>

namespace erhe {
    class Hierarchy;
}
namespace erhe::imgui {
    class Imgui_windows;
}
namespace erhe::scene {
    class Node;
    class Scene_message_bus;
}

namespace editor
{

class Editor_scenes;
class Editor_settings;
class Icon_set;
class IOperation;
class Operation_stack;
class Scene_commands;
class Selection_tool;

enum class Placement : unsigned int {
    Before_anchor = 0, // Place node before anchor
    After_anchor,      // Place node after anchor
    Last               // Place node as last child of parent, ignore anchor
};

enum class Selection_usage : unsigned int {
    Selection_ignored = 0,
    Selection_used
};

class Tree_node_state
{
public:
    bool is_open      {false};
    bool need_tree_pop{false};
};

class Item_tree_window
    : public erhe::imgui::Imgui_window
{
public:
    Item_tree_window(
        erhe::imgui::Imgui_renderer&            imgui_renderer,
        erhe::imgui::Imgui_windows&             imgui_windows,
        Editor_context&                         context,
        const std::string_view                  window_title,
        const std::string_view                  ini_label,
        const std::shared_ptr<erhe::Hierarchy>& root
    );

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

    void set_item_filter  (const erhe::Item_filter& filter);
    void set_item_callback(std::function<bool(const std::shared_ptr<erhe::Item>&)> fun);

    auto drag_and_drop_target(const std::shared_ptr<erhe::Item>& node) -> bool;

private:
    void set_item_selection_terminator(const std::shared_ptr<erhe::Item>& item);
    void set_item_selection           (const std::shared_ptr<erhe::Item>& item, bool selected);
    void clear_selection              ();
    void recursive_add_to_selection   (const std::shared_ptr<erhe::Item>& node);
    void select_all                   ();

    void move_selection(
        const std::shared_ptr<erhe::Item>& target,
        erhe::Item*                        payload_item,
        Placement                          placement
    );
    void attach_selection_to(
        const std::shared_ptr<erhe::Item>& target_node,
        erhe::Item*                        payload_item
    );

    void item_popup_menu      (const std::shared_ptr<erhe::Item>& item);
    void item_icon            (const std::shared_ptr<erhe::Item>& item);
    auto item_icon_and_text   (const std::shared_ptr<erhe::Item>& item, bool update, bool force_expand) -> Tree_node_state;
    void item_update_selection(const std::shared_ptr<erhe::Item>& item);
    void imgui_item_node      (const std::shared_ptr<erhe::Item>& item);

    enum class Show_mode : unsigned int {
        Hide          = 0,
        Show          = 1,
        Show_expanded = 2
    };
    [[nodiscard]] auto should_show(
        const std::shared_ptr<erhe::Item>& item
    ) -> Show_mode;

    auto get_item_by_id(
        std::size_t id
    ) const -> std::shared_ptr<erhe::Item>;

    void try_add_to_attach(
        Compound_operation::Parameters&    compound_parameters,
        const std::shared_ptr<erhe::Item>& target_node,
        const std::shared_ptr<erhe::Item>& node,
        Selection_usage                    selection_usage
    );

    void reposition(
        Compound_operation::Parameters&    compound_parameters,
        const std::shared_ptr<erhe::Item>& anchor_node,
        const std::shared_ptr<erhe::Item>& child_node,
        Placement                          placement,
        Selection_usage                    selection_usage
    );
    void drag_and_drop_source(const std::shared_ptr<erhe::Item>& node);

    Editor_context&                                         m_context;
    erhe::Item_filter                                       m_filter;
    ImGuiTextFilter                                         m_text_filter;
    std::shared_ptr<erhe::Hierarchy>                        m_root;
    std::function<bool(const std::shared_ptr<erhe::Item>&)> m_item_callback;

    std::shared_ptr<IOperation>        m_operation;
    std::vector<std::function<void()>> m_operations;

    bool                               m_toggled_open{false};
    std::weak_ptr<erhe::Item>          m_last_focus_item;
    std::shared_ptr<erhe::Item>        m_popup_item;
    std::string                        m_popup_id_string;
    unsigned int                       m_popup_id{0};

};

} // namespace editor
