#pragma once

#include "operations/compound_operation.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include "robin_hood.h"

#include <functional>
#include <memory>
#include <optional>

namespace erhe::scene
{
    class Node;
}

namespace editor
{

class Editor_scenes;
class IOperation;
class Icon_set;
class Scene_commands;
class Selection_tool;

enum class Placement : unsigned int
{
    Before_anchor = 0, // Place node before anchor
    After_anchor,      // Place node after anchor
    Last               // Place node as last child of parent, ignore anchor
};

enum class Selection_usage : unsigned int
{
    Selection_ignored = 0,
    Selection_used
};

class Node_tree_window
    : public erhe::components::Component
    , public erhe::application::Imgui_window
{
public:
    static constexpr std::string_view c_type_name{"Node_tree"};
    static constexpr std::string_view c_title{"Node Tree"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Node_tree_window ();
    ~Node_tree_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void clear_selection            ();
    void recursive_add_to_selection (const std::shared_ptr<erhe::scene::Node>& node);
    void select_all                 ();

    void move_selection(
        const std::shared_ptr<erhe::scene::Node>&            target_node,
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id,
        Placement                                            placement
    );
    void attach_selection_to(
        const std::shared_ptr<erhe::scene::Node>&            target_node,
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id
    );

    auto node_items       (const std::shared_ptr<erhe::scene::Node>& node, bool update) -> bool;
    void imgui_node_update(const std::shared_ptr<erhe::scene::Node>& node);
    void imgui_tree_node  (const std::shared_ptr<erhe::scene::Node>& node);

    auto get_node_by_id   (const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type id) -> std::shared_ptr<erhe::scene::Node>;
    void try_add_to_attach(
        Compound_operation::Parameters&           compound_parameters,
        const std::shared_ptr<erhe::scene::Node>& target_node,
        const std::shared_ptr<erhe::scene::Node>& node,
        Selection_usage                           selection_usage
    );

    void reposition(
        Compound_operation::Parameters&           compound_parameters,
        const std::shared_ptr<erhe::scene::Node>& anchor_node,
        const std::shared_ptr<erhe::scene::Node>& child_node,
        Placement                                 placement,
        Selection_usage                           selection_usage
    );
    void drag_and_drop_source (const std::shared_ptr<erhe::scene::Node>& node);
    auto drag_and_drop_target (const std::shared_ptr<erhe::scene::Node>& node) -> bool;

    // Component dependencies
    std::shared_ptr<Editor_scenes>  m_editor_scenes;
    std::shared_ptr<Icon_set>       m_icon_set;
    std::shared_ptr<Scene_commands> m_scene_commands;
    std::shared_ptr<Selection_tool> m_selection_tool;

    robin_hood::unordered_map<
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type,
        std::shared_ptr<erhe::scene::Node>
    > m_tree_nodes;

    robin_hood::unordered_map<
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type,
        std::shared_ptr<erhe::scene::Node>
    > m_tree_nodes_last_frame;

    std::shared_ptr<IOperation> m_operation;

    std::weak_ptr<erhe::scene::Node> m_last_focus_node;

    std::vector<std::function<void()>> m_operations;
    std::shared_ptr<erhe::scene::Node> m_popup_node;
    std::string                        m_popup_id_string;
    unsigned int                       m_popup_id{0};
    erhe::scene::Visibility_filter     m_filter;
};

} // namespace editor
