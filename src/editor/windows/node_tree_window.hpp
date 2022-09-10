#pragma once

#include "operations/compound_operation.hpp"

#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include "robin_hood.h"

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
class Selection_tool;

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
    void clear_selection           ();
    void recursive_add_to_selection(const std::shared_ptr<erhe::scene::Node>& node);
    void select_all                ();
    void move_selection_before     (const std::shared_ptr<erhe::scene::Node>& target_node, const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id);
    void move_selection_after      (const std::shared_ptr<erhe::scene::Node>& target_node, const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id);
    void attach_selection_to       (const std::shared_ptr<erhe::scene::Node>& target_node, const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id);

    auto node_items       (const std::shared_ptr<erhe::scene::Node>& node, const bool update) -> bool;
    void imgui_node_update(const std::shared_ptr<erhe::scene::Node>& node);
    void imgui_tree_node  (const std::shared_ptr<erhe::scene::Node>& node);
    auto get_node_by_id   (const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type id) -> std::shared_ptr<erhe::scene::Node>;
    void try_add_to_attach(
        Compound_operation::Parameters&           compound_parameters,
        const std::shared_ptr<erhe::scene::Node>& target_node,
        const std::shared_ptr<erhe::scene::Node>& node
    );
    void drag_and_drop_source (const std::shared_ptr<erhe::scene::Node>& node);
    auto drag_and_drop_target (const std::shared_ptr<erhe::scene::Node>& node) -> bool;

    // Component dependencies
    std::shared_ptr<Editor_scenes>  m_editor_scenes;
    std::shared_ptr<Icon_set>       m_icon_set;
    std::shared_ptr<Selection_tool> m_selection_tool;

    robin_hood::unordered_map<
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type,
        std::shared_ptr<erhe::scene::Node>
    > m_tree_nodes;

    robin_hood::unordered_map<
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type,
        std::shared_ptr<erhe::scene::Node>
    > m_tree_nodes_last_frame;

    std::shared_ptr<IOperation> m_drag_and_drop_operation;

    std::weak_ptr<erhe::scene::Node> m_last_focus_node;
};

} // namespace editor
