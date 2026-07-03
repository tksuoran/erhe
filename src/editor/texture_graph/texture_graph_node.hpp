#pragma once

#include "texture_graph/texture_payload.hpp"

#include "erhe_graph/node.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>
#include <vector>

struct ImDrawList;

namespace ax::NodeEditor { class EditorContext; }
namespace erhe::graphics { class Texture; }
namespace erhe::texgen {
    class Node_descriptor;
    class Compose_node;
}

namespace editor {

class App_context;
class Texture_compose_dag;
class Texture_graph;
class Texture_renderer;

// Left / right arrow buttons cycling index through [0, count).
// ImGui popups (Combo, BeginPopup) cannot be used inside the
// ax::NodeEditor canvas, so node content uses these steppers instead.
// (Duplicated from the geometry graph on purpose - the texture graph does
// not depend on geometry_graph; a shared-widgets extraction is deferred, see
// doc/texture-graph-plan.md decision 5. Distinct names avoid an ODR clash with
// the geometry graph's identically-shaped helpers.)
auto texture_index_stepper(const char* id, int& index, int count) -> bool;

// Index stepper followed by the current entry name.
auto texture_enum_stepper(const char* id, int& index, const char* const* names, int count) -> bool;

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

    // Preview / sink rendering (Phase 3 Step 3). The window drives a per-frame
    // refresh: a node whose composition changed (mark_dirty() set the flag)
    // recomposes and re-renders its preview / bakes its output once, then the
    // result is cached. See Texture_graph_window::update().
    [[nodiscard]] auto preview_needs_render() const -> bool;
    void clear_preview_needs_render();

    // Index of the output slot shown as this node's preview thumbnail, or a
    // negative value when the node has no previewable output (e.g. the output
    // node, which renders its own baked result instead). Default: output 0 when
    // the node has any output pins.
    [[nodiscard]] virtual auto preview_output_index() const -> int;

    // The texture drawn as this node's thumbnail (preview or bake result). The
    // window's render helper (re)creates it; the base draws it in node_editor().
    [[nodiscard]] auto get_preview_texture() const -> const std::shared_ptr<erhe::graphics::Texture>&;
    [[nodiscard]] auto get_preview_texture_ref() -> std::shared_ptr<erhe::graphics::Texture>&;

    // Edge length of the preview thumbnail drawn in the node UI.
    [[nodiscard]] virtual auto preview_display_size() const -> float;

    // Resolution of the rendered preview / bake texture (square).
    [[nodiscard]] virtual auto render_target_size() const -> int;

    // Recomposes this node's subtree and (re)renders its products: the base
    // renders the primary output into the preview thumbnail; sink nodes (the
    // output node) override this to bake and assign a Content_library texture.
    // Called by Texture_graph_window::update() for nodes whose composition
    // changed. Uses context.current_command_buffer; a no-op when it is null or
    // not recording, when the node has no descriptor, or when the composition
    // contains an error marker (the previous good texture is kept).
    virtual void render_products(App_context& context, Texture_renderer& renderer);

    void node_editor(App_context& context, ax::NodeEditor::EditorContext& node_editor);

    virtual void evaluate(Texture_graph& graph);
    virtual void imgui   ();

    // Node <-> texgen bridge (doc/texture-graph-plan.md, Phase 3 Step 2).
    //
    // descriptor() returns this node's immutable texgen Node_descriptor (the
    // GLSL snippet table shared by every instance of the type), or nullptr for
    // a sink node that has no descriptor of its own (e.g. the output node
    // arriving in Step 3). configure() pushes this node's live parameter values
    // into a Compose_node built from that descriptor (set_float / set_color /
    // set_enum_index / set_bool / set_size_exponent / set_seed).
    //
    // The editor node's pins are built FROM the descriptor
    // (build_pins_from_descriptor), so the pin count / order / types can never
    // drift from the descriptor's inputs and outputs. Step 3 walks the graph to
    // assemble a Compose_node DAG mirroring the editor graph and composes one
    // fragment shader; this step only establishes the descriptor, the
    // parameter values and the pins.
    [[nodiscard]] virtual auto descriptor() const -> const erhe::texgen::Node_descriptor*;
    virtual void configure(erhe::texgen::Compose_node& compose_node) const;

    // True for a Phase 5 buffer node - an explicit render-to-texture cut point.
    // The compose DAG walk stops at a buffer (it is sampled, not inlined); the
    // buffer renders its own input subtree into its texture each dirty pass.
    [[nodiscard]] virtual auto is_buffer() const -> bool;

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

    // Parameter undo support. The committed state is the write_parameters()
    // JSON dump the next Texture_graph_parameter_operation uses as its "before"
    // side; node_editor() captures widget edit gestures against it (one
    // operation per completed gesture, pushed on widget deactivation).
    [[nodiscard]] auto dump_parameters() const -> std::string;
    [[nodiscard]] auto get_committed_parameters() const -> const std::string&;
    void set_committed_parameters(const std::string& parameters);

protected:
    // Composes `dag` (its sink), binds every buffer texture the composition
    // samples (from dag.sampler_sources), and renders the result into `target`
    // at `size`. Returns render_into's success. Shared by the base preview, the
    // output/material bake, and the buffer node's own render, so buffer sampler
    // binding is wired in exactly one place. A no-op (returns false, keeping the
    // previous texture) when the DAG is empty, the composition errors, or a
    // sampled buffer has not produced its texture yet.
    [[nodiscard]] auto render_dag(
        App_context&                              context,
        Texture_renderer&                         renderer,
        const Texture_compose_dag&                dag,
        std::shared_ptr<erhe::graphics::Texture>& target,
        int                                       size
    ) -> bool;

    // Creates one input pin per descriptor input and one output pin per
    // descriptor output, keyed by each endpoint's Value_type. Keeps the editor
    // node's pins in lockstep with the descriptor.
    void build_pins_from_descriptor(const erhe::texgen::Node_descriptor& descriptor);

    void show_pins(
        ax::NodeEditor::EditorContext&                                  node_editor,
        ImDrawList&                                                     draw_list,
        const etl::vector<erhe::graph::Pin, erhe::graph::max_pin_count>& pins,
        float                                                           edge_x,
        bool                                                            right_edge
    );

    std::vector<Texture_payload>             m_input_payloads;
    std::vector<Texture_payload>             m_output_payloads;
    std::string                              m_type_name;
    std::string                              m_committed_parameters;
    std::shared_ptr<erhe::graphics::Texture> m_preview_texture;
    bool                                     m_dirty{true};
    bool                                     m_preview_needs_render{true};
    bool                                     m_parameter_edit_in_progress{false};

private:
    void draw_preview(App_context& context);
};

} // namespace editor
