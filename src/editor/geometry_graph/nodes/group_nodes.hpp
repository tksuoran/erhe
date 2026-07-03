#pragma once

#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

// Group_node holds a Geometry_graph by value; its links vector needs the
// complete Link type wherever Group_node is destroyed.
#include "erhe_graph/link.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace editor {

class App_context;

// Interface node inside a group asset: the geometry entering the group
// flows out of this node into the group body. Its output payload is set
// by the enclosing Group_node before the group body evaluates, so
// evaluate() must not touch it.
class Group_input_node : public Geometry_graph_node
{
public:
    Group_input_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
};

// Interface node inside a group asset: the geometry arriving here is the
// group's result, read by the enclosing Group_node after the group body
// evaluates.
class Group_output_node : public Geometry_graph_node
{
public:
    Group_output_node();

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
};

// Reusable subgraph node: loads a graph asset (a JSON file saved by the
// geometry graph window, containing Group_input_node / Group_output_node
// interface nodes) into a private subgraph and evaluates it inline. The
// group's input pin feeds the asset's Group_input_node; the asset's
// Group_output_node input becomes this node's output. Group assets may
// nest other groups; a depth guard breaks reference cycles.
//
// Editing a group is done by loading its file in the geometry graph
// window (the file toolbar), editing, and saving it back; group nodes
// referencing the file pick the changes up when reloaded (path re-commit
// or graph reload).
class Group_node : public Geometry_graph_node
{
public:
    explicit Group_node(App_context& context);

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void on_removed_from_graph() override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Background evaluation support: a scene Output node inside a group
    // asset builds its evaluated products on the shadow clone's private
    // subgraph. This pairs the shadow subgraph's Output (and nested
    // Group) nodes with this live node's - both sides load the same
    // asset file, so the node order matches - and moves the products
    // over so apply happens on the live nodes that own the scene items.
    // Skipped when the loaded assets differ (path changed mid-flight);
    // the node is dirty again in that case and re-evaluates. Main thread
    // only.
    void adopt_subgraph_outputs(Group_node& shadow, int depth = 0);

private:
    // Loads the group asset into the private subgraph when the path
    // changed since the last load (or after unload). Failure leaves the
    // subgraph empty; evaluate() then outputs an empty payload.
    void ensure_loaded();
    void unload_subgraph();
    [[nodiscard]] auto load_subgraph(const std::filesystem::path& path) -> bool;

    App_context&                                      m_context;
    std::string                                       m_path;
    std::string                                       m_loaded_path;
    bool                                              m_load_attempted{false};
    Geometry_graph                                    m_subgraph;
    std::vector<std::shared_ptr<Geometry_graph_node>> m_subgraph_nodes;
    Group_input_node*                                 m_input_node {nullptr};
    Group_output_node*                                m_output_node{nullptr};
};

}
