#pragma once

#include "texture_graph/texture_payload.hpp"
#include "graph_editor/graph_editor_node.hpp"

#include <nlohmann/json_fwd.hpp>

#include <memory>
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
class Graph_texture;
class Texture_compose_dag;
class Texture_graph;
class Texture_renderer;

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
class Texture_graph_node : public Graph_editor_node
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

    // Implements Graph_editor_node - also invalidates this node's preview cache.
    void mark_dirty() override;

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

    // Whether the preview texture resolution follows the thumbnail's actual
    // on-canvas display size (zoom-scaled, quantized to powers of two in
    // [64, 512]) so a zoomed-in node is not upscaled from a low-resolution
    // render. True for plain thumbnail previews; overridden false where the
    // texture is content with a configured resolution (buffer / output /
    // material output nodes, whose "size" is a serialized parameter).
    [[nodiscard]] virtual auto uses_display_scaled_preview() const -> bool;

    // The quantized resolution matching the display size draw_preview()
    // recorded last (screen pixels, zoom-scaled).
    [[nodiscard]] auto get_preview_desired_texture_size() const -> int;

    // Whether render_products() should run this frame: the composition
    // changed (preview_needs_render), or - display-scaled previews only -
    // the on-canvas display size no longer matches the texture resolution.
    [[nodiscard]] auto preview_render_pending() const -> bool;

    // Recomposes this node's subtree and (re)renders its products: the base
    // renders the primary output into the preview thumbnail; sink nodes (the
    // output node) override this to bake and assign a Content_library texture.
    // Called by Texture_graph_window::update() for nodes whose composition
    // changed. Uses context.current_command_buffer; a no-op when it is null or
    // not recording, when the node has no descriptor, or when the composition
    // contains an error marker (the previous good texture is kept).
    virtual void render_products(App_context& context, Texture_renderer& renderer);

    virtual void evaluate(Texture_graph& graph);

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

    // The Graph_texture asset this node belongs to (set when the node is added
    // to a graph). A sink node uses it so an in-window "assign to material" binds
    // the material to the owning asset (a live, persisted texture_reference) rather
    // than pushing a one-off baked texture. Graphs only live in the content
    // library, so this is always set for a node in a graph.
    void set_owning_graph_texture(const std::weak_ptr<Graph_texture>& graph_texture);
    [[nodiscard]] auto get_owning_graph_texture() const -> std::shared_ptr<Graph_texture>;

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

    // Implements Graph_editor_node
    [[nodiscard]] auto pin_key_color(std::size_t key) const -> ImU32 override;
    void commit_parameter_operation(App_context& context, std::string&& before_parameters, std::string&& after_parameters) override;
    // Draws the node's preview thumbnail after its content widgets.
    void after_node_content(App_context& context) override;

    std::vector<Texture_payload>             m_input_payloads;
    std::vector<Texture_payload>             m_output_payloads;
    std::weak_ptr<Graph_texture>             m_owning_graph_texture;
    std::shared_ptr<erhe::graphics::Texture> m_preview_texture;
    bool                                     m_preview_needs_render{true};
    float                                    m_preview_display_size{96.0f}; // screen px, zoom-scaled

private:
    void draw_preview(App_context& context);
};

} // namespace editor
