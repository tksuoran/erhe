#pragma once

#include "texture_graph/texture_graph_node.hpp"

#include <memory>
#include <string>

namespace erhe::graphics  { class Sampler; class Texture; }
namespace erhe::primitive { class Material; }

namespace editor {

class App_context;
class Scene_root;

// Terminal node baking its input texture subtree into a Content_library texture
// at a chosen power-of-two size, optionally assigning it to a material's base
// color channel.
//
// It has no descriptor / GLSL of its own (descriptor() == nullptr); instead it
// composes and renders the node feeding its connected input. Three input pins
// (f / rgb / rgba) let any MVP value type connect (pin keys are per-type, so a
// single-type input would reject the others); the highest-channel connected
// input wins and is wrapped to vec4 by the composer at bake time.
//
// Baking runs on the main thread from Texture_graph_window::update() via the
// overridden render_products(): assemble + compile + render into a persistent
// Texture, (re)register it in the target scene's Content_library under the
// editable name, and - when "Assign to material base color" is on and a
// material is selected - set that material's base_color sampler. A disconnected
// input keeps the last baked texture (no crash).
class Texture_output_node : public Texture_graph_node
{
public:
    explicit Texture_output_node(App_context& context);
    ~Texture_output_node() noexcept override;

    // Implements Texture_graph_node
    void evaluate         (Texture_graph& graph) override;
    void imgui            () override;
    void on_removed_from_graph() override;
    void render_products  (App_context& context, Texture_renderer& renderer) override;
    void write_parameters (nlohmann::json& out) const override;
    void read_parameters  (const nlohmann::json& in) override;
    [[nodiscard]] auto preview_display_size() const -> float override;
    [[nodiscard]] auto render_target_size  () const -> int   override;
    // The bake resolution is content (the serialized "size" parameter) -
    // never display-scaled.
    [[nodiscard]] auto uses_display_scaled_preview() const -> bool override { return false; }

    // The most recently baked output texture (what is registered in the content
    // library), or nullptr when nothing has been baked yet. Used by the owning
    // Graph_texture asset to expose the graph's output as a Texture_reference.
    [[nodiscard]] auto get_baked_texture() const -> const erhe::graphics::Texture*;

private:
    // Index (0..3) of the connected input pin to bake, preferring the highest
    // channel count (rgba > rgb > f); -1 when nothing is connected.
    [[nodiscard]] auto connected_input_index() const -> int;
    void register_texture();
    void unregister_texture();
    void assign_to_material();

    App_context&                               m_context;
    std::shared_ptr<Scene_root>                m_scene_root;
    std::shared_ptr<erhe::primitive::Material> m_material;
    std::shared_ptr<erhe::graphics::Sampler>   m_sampler;
    std::shared_ptr<erhe::graphics::Texture>   m_registered_texture; // what is currently in the content library
    std::string                                m_texture_name{"Texture Graph"};
    int                                        m_size_index{2};      // 0:256 1:512 2:1024 3:2048
    bool                                       m_assign_to_material{false};
};

} // namespace editor
