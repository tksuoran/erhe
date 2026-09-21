#pragma once

#include "erhe_scene/layout.hpp"
#include "erhe_scene/node_system.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace erhe::scene {

class Xformable; using Node = Xformable;

// The per-scene owner of the `Layout` value group's runtime state
// (doc/erhe/scene.md "Node systems", doc/plans/node_attachments_to_properties.md
// D2): the set of layout nodes the solve pass runs over, and each one's
// effective container values. One of these is owned by each `Scene` and added
// to it, which drives it from the three node-system change sites.
//
// The record is keyed by a raw `Node*` and holds no `shared_ptr`, and
// `on_node_unregistered` erases it, so a scene close releases everything the
// system holds.
class Layout_system : public INode_system
{
public:
    ~Layout_system() noexcept override;

    // Implements INode_system
    void on_node_registered    (Node& node) override;
    void on_node_unregistered  (Node& node) override;
    void on_values_changed     (Node& node, const erhe::property::Dependency_property& property) override;
    void on_node_active_changed(Node& node) override;

    // The layout nodes of this scene with their effective container values.
    [[nodiscard]] auto get_records() const -> const std::unordered_map<Node*, Layout_data>&;

    // The effective container values of one layout node, or null when the node
    // is not a layout node of this scene. Reading through the record costs no
    // allocation, which is what a per-frame reader (the debug visualization)
    // needs.
    [[nodiscard]] auto find(const Node& node) const -> const Layout_data*;

    // Arrange the children of every layout node, shallow-to-deep so a parent
    // layout runs before any nested child layout. Called once per frame by
    // Scene::update_layouts() before the world-transform passes. Every
    // container it walks is a member cleared at the start of its use, so a
    // steady-state pass allocates nothing.
    void update();

private:
    void apply       (Node& layout_node, const Layout_data& data);
    void layout_stack(Node& layout_node, const Layout_data& data);
    void layout_grid (Node& layout_node, const Layout_data& data);
    void layout_flow (Node& layout_node, const Layout_data& data);

    // A flow "line": children packed along the primary axis. cross_s / cross_t
    // are the maximum child sizes along the secondary / tertiary axes.
    class Flow_line
    {
    public:
        std::size_t first_member{0}; // index into m_flow_members
        std::size_t member_count{0};
        float       used_p {0.0f};
        float       cross_s{0.0f};
        float       cross_t{0.0f};
    };

    // A flow "sheet": lines stacked along the secondary axis. cross_t is the
    // maximum line tertiary size in this sheet.
    class Flow_sheet
    {
    public:
        std::size_t first_line {0}; // index into m_flow_sheet_lines
        std::size_t line_count {0};
        float       used_s {0.0f};
        float       cross_t{0.0f};
    };

    std::unordered_map<Node*, Layout_data>    m_records;

    // Scratch of update(); cleared at the start of each use, capacity kept.
    std::vector<std::pair<std::size_t, Node*>> m_sorted;
    std::array<std::vector<float>, 3>          m_track_edges;
    std::vector<std::shared_ptr<Node>>         m_flow_children;
    std::vector<erhe::math::Aabb>              m_flow_contents;
    std::vector<std::size_t>                   m_flow_members;
    std::vector<std::size_t>                   m_flow_sheet_lines;
    std::vector<Flow_line>                     m_flow_lines;
    std::vector<Flow_sheet>                    m_flow_sheets;
};

} // namespace erhe::scene
