#pragma once

#include "geometry_graph/geometry_payload.hpp"

#include "erhe_graph/node.hpp"

#include <string_view>
#include <vector>

struct ImDrawList;

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;
class Geometry_graph;

// Applies the Geometry::process() flags every generator / operation node
// needs on its output geometry, so downstream operation nodes find
// connectivity and edges present. Final render oriented processing
// (normals, tangents, texture coordinates) happens in the scene output
// node.
void process_for_graph(erhe::geometry::Geometry& geometry);

// Left / right arrow buttons cycling index through [0, count).
// ImGui popups (Combo, BeginPopup) cannot be used inside the
// ax::NodeEditor canvas, so node content uses these steppers instead.
auto imgui_index_stepper(const char* id, int& index, int count) -> bool;

// Index stepper followed by the current entry name.
auto imgui_enum_stepper(const char* id, int& index, const char* const* names, int count) -> bool;

// Base class for all geometry graph nodes.
//
// Follows the Shader_graph_node pattern: payload storage per pin slot,
// accumulate-from-links input pulling, and ax::NodeEditor rendering with
// input pins on the left edge and output pins on the right edge.
//
// Nodes are born dirty; parameter edits in imgui() must call mark_dirty().
// Geometry_graph re-evaluates the whole graph when any node is dirty and
// clears the flags afterwards.
class Geometry_graph_node : public erhe::graph::Node
{
public:
    explicit Geometry_graph_node(const char* label);

    // Pulls and combines payloads from all links connected to input pin slot i.
    // See Geometry_payload::operator+=() for multi-link semantics.
    [[nodiscard]] auto accumulate_input_from_links(std::size_t i) const -> Geometry_payload;

    // Stores accumulate_input_from_links() into every input payload slot.
    // Call at the start of evaluate() so imgui() can display input values
    // via get_input() without re-pulling (geometry accumulation allocates).
    void pull_inputs();

    [[nodiscard]] auto get_input (std::size_t i) const -> const Geometry_payload&;
    [[nodiscard]] auto get_output(std::size_t i) const -> const Geometry_payload&;
    void set_input      (std::size_t i, const Geometry_payload& payload);
    void set_output     (std::size_t i, const Geometry_payload& payload);
    void make_input_pin (std::size_t key, std::string_view name);
    void make_output_pin(std::size_t key, std::string_view name);

    [[nodiscard]] auto is_dirty() const -> bool;
    void mark_dirty ();
    void clear_dirty();

    void node_editor(App_context& context, ax::NodeEditor::EditorContext& node_editor);

    virtual void evaluate(Geometry_graph& graph);
    virtual void imgui   ();

    // Called when the node leaves the graph (deletion, undo of add,
    // graph clear / load). The node object may stay alive in the undo
    // stack, so side effects outside the graph - like the scene mesh
    // owned by the output node - must be released here rather than in
    // the destructor.
    virtual void on_removed_from_graph();

protected:
    void show_pins(
        ax::NodeEditor::EditorContext&                        node_editor,
        ImDrawList&                                           draw_list,
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
        float                                                 edge_x,
        bool                                                  right_edge
    );

    std::vector<Geometry_payload> m_input_payloads;
    std::vector<Geometry_payload> m_output_payloads;
    bool                          m_dirty{true};
};

}
