#pragma once

#include "operations/compound_operation.hpp"
#include "windows/imgui_window.hpp"

#include "erhe/components/components.hpp"

#include "erhe/toolkit/optional.hpp"
#include "erhe/toolkit/unique_id.hpp"

#include <memory>
#include <unordered_map>

namespace erhe::scene
{
    class Node;
}

namespace editor
{

class IOperation;
class Icon_set;
class Scene_root;
class Selection_tool;

class Node_tree_window
    : public erhe::components::Component
    , public Imgui_window
{
public:
    static constexpr std::string_view c_name {"Node_tree"};
    static constexpr std::string_view c_title{"Node Tree"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Node_tree_window ();
    ~Node_tree_window() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Implements Imgui_window
    void imgui   () override;
    void on_begin() override;
    void on_end  () override;

private:
    void clear_selection      ();
    void select_all           ();
    void move_selection_before(const std::shared_ptr<erhe::scene::Node>& target_node, const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id);
    void move_selection_after (const std::shared_ptr<erhe::scene::Node>& target_node, const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id);
    void attach_selection_to  (const std::shared_ptr<erhe::scene::Node>& target_node, const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type payload_id);

    auto node_items           (const std::shared_ptr<erhe::scene::Node>& node, const bool update) -> bool;
    void imgui_node_update    (const std::shared_ptr<erhe::scene::Node>& node);
    void imgui_tree_node      (const std::shared_ptr<erhe::scene::Node>& node);
    auto get_node_by_id       (const erhe::toolkit::Unique_id<erhe::scene::Node>::id_type id) -> std::shared_ptr<erhe::scene::Node>;
    void try_add_to_attach(
        Compound_operation::Parameters&           compound_parameters,
        const std::shared_ptr<erhe::scene::Node>& target_node,
        const std::shared_ptr<erhe::scene::Node>& node
    );
    void drag_and_drop_source (const std::shared_ptr<erhe::scene::Node>& node);
    auto drag_and_drop_target (const std::shared_ptr<erhe::scene::Node>& node) -> bool;

    // Component dependencies
    std::shared_ptr<Scene_root>     m_scene_root;
    std::shared_ptr<Selection_tool> m_selection_tool;
    std::shared_ptr<Icon_set>       m_icon_set;

    //nonstd::optional<int> m_range_select_start;
    //nonstd::optional<int> m_range_select_end;

    std::unordered_map<
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type,
        std::shared_ptr<erhe::scene::Node>
    > m_tree_nodes;
    std::unordered_map<
        erhe::toolkit::Unique_id<erhe::scene::Node>::id_type,
        std::shared_ptr<erhe::scene::Node>
    > m_tree_nodes_last_frame;
    std::shared_ptr<IOperation> m_drag_and_drop_operation;
};

} // namespace editor
