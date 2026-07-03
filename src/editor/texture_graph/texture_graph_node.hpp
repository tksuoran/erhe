#pragma once

#include "texture_graph/texture_payload.hpp"

#include "erhe_graph/node.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>
#include <vector>

struct ImDrawList;

namespace ax::NodeEditor { class EditorContext; }

namespace editor {

class App_context;
class Texture_graph;

// Base class for all texture graph nodes.
//
// Mirrors Geometry_graph_node: payload storage per pin slot, input pulling
// from links, and ax::NodeEditor rendering with input pins on the left edge
// and output pins on the right edge. Texture payloads reference the producing
// node output rather than carrying a texture (see Texture_payload); the actual
// GLSL composition happens at the sink nodes (preview / output), added in a
// later step.
//
// Nodes are born dirty; parameter edits in imgui() must call mark_dirty().
// Texture_graph::evaluate_if_dirty() re-runs dirty nodes and everything
// downstream of them (dirtiness propagates along links in topological order);
// clean nodes keep their cached output payloads.
//
// Evaluation is synchronous (composition is cheap - decision 8 in
// doc/texture-graph-plan.md), so unlike the geometry graph there is no async
// shadow-clone machinery here.
class Texture_graph_node : public erhe::graph::Node
{
public:
    explicit Texture_graph_node(const char* label);

    // Payload of the single source connected to input pin slot i (empty when
    // unconnected). Multi-link inputs are not used by the MVP node set; if a
    // later node needs fan-in it accumulates here.
    [[nodiscard]] auto input_from_links(std::size_t i) const -> Texture_payload;

    // Stores input_from_links() into every input payload slot. Call at the
    // start of evaluate() so imgui() and downstream sinks can read the inputs
    // via get_input() without re-walking the links.
    void pull_inputs();

    [[nodiscard]] auto get_input (std::size_t i) const -> const Texture_payload&;
    [[nodiscard]] auto get_output(std::size_t i) const -> const Texture_payload&;
    void set_input      (std::size_t i, const Texture_payload& payload);
    void set_output     (std::size_t i, const Texture_payload& payload);
    void make_input_pin (std::size_t key, std::string_view name);
    void make_output_pin(std::size_t key, std::string_view name);

    [[nodiscard]] auto is_dirty() const -> bool;
    void mark_dirty ();
    void clear_dirty();

    void node_editor(App_context& context, ax::NodeEditor::EditorContext& node_editor);

    virtual void evaluate(Texture_graph& graph);
    virtual void imgui   ();

    // Called when the node leaves the graph (deletion, undo of add, graph
    // clear / load). Side effects outside the graph (e.g. a sink node's
    // preview / output textures) must be released here rather than in the
    // destructor, since the node object may stay alive in the undo stack.
    virtual void on_removed_from_graph();

    // Graph serialization: the factory type name recreates this node on load
    // (set by the factory); parameters are the node's editable values. (Named
    // to avoid clashing with erhe::Item::get_type_name().)
    [[nodiscard]] auto get_factory_type_name() const -> const std::string&;
    void set_factory_type_name(const std::string& type_name);
    virtual void write_parameters(nlohmann::json& out) const;
    virtual void read_parameters (const nlohmann::json& in);

    // Parameter undo support (used once undo/redo is wired in a later step):
    // the committed state is the write_parameters() JSON dump the next
    // parameter operation uses as its "before" side.
    [[nodiscard]] auto dump_parameters() const -> std::string;
    [[nodiscard]] auto get_committed_parameters() const -> const std::string&;
    void set_committed_parameters(const std::string& parameters);

protected:
    void show_pins(
        ax::NodeEditor::EditorContext&                                  node_editor,
        ImDrawList&                                                     draw_list,
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
        float                                                           edge_x,
        bool                                                            right_edge
    );

    std::vector<Texture_payload> m_input_payloads;
    std::vector<Texture_payload> m_output_payloads;
    std::string                  m_type_name;
    std::string                  m_committed_parameters;
    bool                         m_dirty{true};
};

} // namespace editor
