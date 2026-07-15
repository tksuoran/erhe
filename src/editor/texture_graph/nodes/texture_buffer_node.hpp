#pragma once

#include "texture_graph/texture_graph_node.hpp"

namespace editor {

class App_context;

// Phase 5 buffer node: an explicit render-to-texture cut point
// (doc/texture-graph-plan.md; Material Maker's gen_buffer.gd). It renders its
// connected input subtree ONCE into a persistent square texture at a chosen
// power-of-two resolution, then exposes that texture as a sampler2D source
// downstream - so a filter that samples its input many times (blur, warp) reads
// cheap texture taps instead of the whole upstream subtree being inlined per
// tap.
//
// It carries three typed input pins (f / rgb / rgba) and three matching output
// pins so any value type can pass through (pin keys are per-type). The
// highest-channel connected input is rendered; each output slot samples the one
// rendered rgba8 texture, converted to that slot's value type at compose time
// (the compose DAG walk stops here - build_texture_compose_dag emits a
// sampler-source Compose_node for this node instead of recursing into its
// input).
//
// The buffer is itself a small sink for its input subtree: its render_products()
// composes + renders its input each dirty pass (buffers are rendered in
// topological order, before their downstream consumers, so a buffer feeding
// another buffer works). "Pause" skips re-rendering, freezing the last texture.
class Texture_buffer_node : public Texture_graph_node
{
public:
    explicit Texture_buffer_node(App_context& context);

    // Implements Texture_graph_node
    void evaluate           (Texture_graph& graph) override;
    void imgui              () override;
    void render_products    (App_context& context, Texture_renderer& renderer) override;
    void write_parameters   (nlohmann::json& out) const override;
    void read_parameters    (const nlohmann::json& in) override;
    [[nodiscard]] auto is_buffer            () const -> bool  override;
    [[nodiscard]] auto preview_output_index () const -> int   override;
    [[nodiscard]] auto preview_display_size () const -> float override;
    [[nodiscard]] auto render_target_size   () const -> int   override;
    // The buffer's texture is sampled by downstream compositions at its
    // configured resolution (the "size" parameter) - never display-scaled.
    [[nodiscard]] auto uses_display_scaled_preview() const -> bool override { return false; }

private:
    // Index (0..2) of the connected input pin to render, preferring the highest
    // channel count (rgba > rgb > f); -1 when nothing is connected.
    [[nodiscard]] auto connected_input_index() const -> int;

    int  m_size_index{2};    // 0:128 1:256 2:512 3:1024 4:2048
    bool m_pause     {false};
};

} // namespace editor
