#pragma once

#include "assets/asset_reference.hpp"
#include "texture_graph/texture_graph_node.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace erhe::graphics  { class Sampler; class Texture; }
namespace erhe::primitive { class Material; }
namespace erhe::texgen    { class Uniform; }

namespace editor {

class App_context;
class Content_library;
class Scene_root;
class Texture_renderer;

// One composed-and-rendered PBR channel export request produced by
// Material_output_node::build_channel_exports(): the assembled fragment body,
// its ordered uniform values, and the file-name suffix (e.g. "albedo",
// "metallic_roughness"). The MCP export tool renders + writes each of these to
// "<dir>/<base_name>_<suffix>.png".
class Material_channel_export
{
public:
    std::string                       suffix;
    std::string                       fragment;
    std::vector<erhe::texgen::Uniform> uniforms;
};

// Terminal PBR material node: its inputs are the physically-based channels
// (albedo, metallic, roughness, normal, occlusion, emissive), each composed
// from its connected subtree and baked to a Content_library texture at a chosen
// power-of-two size, then assembled into a full erhe::primitive::Material.
//
// Channel -> material slot mapping (matching how standard.frag samples, see
// erhe::scene_renderer::Material_buffer and res/shaders/standard.frag):
//   albedo    -> base_color.texture      (base_color scalar driven to white)
//   normal    -> normal.texture          (normal_texture_scale = 1)
//   emissive  -> emissive.texture         (emissive scalar driven to white)
//   occlusion \                          occlusion.texture (occlusion.r);
//   roughness  } -> ONE packed ORM texture, glTF convention R = occlusion,
//   metallic  /                          G = roughness, B = metallic; assigned
//                                        to metallic_roughness.texture
//                                        (roughness = .g, metallic = .b) and,
//                                        when occlusion is connected, also to
//                                        occlusion.texture (occlusion = .r).
//
// Each channel has two type-tagged input pins so common generators connect
// (colorize / uniform emit rgba, normal_map / color_noise emit rgb, the noise
// family emits grayscale); the first connected pin per channel is baked, with
// texgen conversion applied to the channel's target type. Unconnected channels
// bake no texture and leave the material's scalar default.
//
// Like Texture_output_node, baking runs on the main thread from
// render_products() (driven by Texture_graph_window per dirty node): assemble +
// compile + render into persistent Textures, (re)register them in the target
// scene's Content_library, and - when "Assign to material" is on and a material
// is selected - drive the material's texture samplers and scalar fallbacks.
// Disconnecting a channel drops its texture and reverts that slot; removing the
// node unregisters every texture and clears the assignments.
class Texture_material_output_node : public Texture_graph_node
{
public:
    explicit Texture_material_output_node(App_context& context);
    ~Texture_material_output_node() noexcept override;

    // Implements Texture_graph_node
    void evaluate         (Texture_graph& graph) override;
    void imgui            () override;
    void on_removed_from_graph() override;
    void render_products  (App_context& context, Texture_renderer& renderer) override;
    void write_parameters (nlohmann::json& out) const override;
    void read_parameters  (const nlohmann::json& in) override;
    [[nodiscard]] auto render_target_size  () const -> int   override;
    // The bake resolution is content (the serialized "size" parameter) -
    // never display-scaled.
    [[nodiscard]] auto uses_display_scaled_preview() const -> bool override { return false; }

    // Composes every connected channel's subtree into a fragment shader + its
    // ordered uniform values, for scripted PNG export (the ORM channels are
    // packed into one "metallic_roughness" fragment; occlusion is also exported
    // standalone). Pure string logic, no GPU. Empty when nothing is connected.
    [[nodiscard]] auto build_channel_exports() const -> std::vector<Material_channel_export>;
    [[nodiscard]] auto get_base_name() const -> const std::string&;

private:
    // A separately-baked channel (albedo / normal / emissive): its persistent
    // render target and the texture currently registered in the content library.
    class Baked_texture
    {
    public:
        std::shared_ptr<erhe::graphics::Texture> target;     // persistent render target (reused / resized)
        std::shared_ptr<erhe::graphics::Texture> registered; // what is currently in the content library
    };

    enum class Separate_channel : std::size_t { albedo = 0, normal = 1, emissive = 2, count = 3 };

    // The connected source for a channel (empty source_node when unconnected),
    // preferring the channel's primary input pin over its secondary one.
    [[nodiscard]] auto pick_channel(std::size_t first_pin) const -> Texture_payload;

    void render_separate_channel(
        Texture_renderer&      renderer,
        Separate_channel       channel,
        const Texture_payload& source,
        int                    size
    );
    void render_orm(
        Texture_renderer&      renderer,
        const Texture_payload& occlusion,
        const Texture_payload& roughness,
        const Texture_payload& metallic,
        int                    size
    );

    void register_texture  (Baked_texture& slot, const std::string& name);
    void unregister_texture(Baked_texture& slot);
    void unregister_orm    ();
    // The scene whose content library receives the baked textures: the
    // stored selection when set and still registered, else re-resolved
    // through the owning asset's host (single-scene fallback). The
    // resolved scene is stored back into m_scene_root.
    [[nodiscard]] auto resolve_scene_root() -> std::shared_ptr<Scene_root>;
    // resolve_scene_root()'s content library; null when no scene resolves
    // or the library has no textures folder.
    [[nodiscard]] auto get_content_library() -> std::shared_ptr<Content_library>;
    [[nodiscard]] auto ensure_sampler() -> const std::shared_ptr<erhe::graphics::Sampler>&;
    // Main-thread lazy resolution of the stored material key through the
    // asset manager (R4: read_parameters only stores the key).
    void resolve_reference();
    [[nodiscard]] auto get_material() const -> std::shared_ptr<erhe::primitive::Material>;

    App_context&                                                        m_context;
    // weak: this node is owned (via its Graph_texture asset) by the scene's
    // content library, so a shared_ptr here would be a strong ownership
    // cycle Scene_root -> content library -> Graph_texture -> this node ->
    // Scene_root, keeping a closed scene alive forever (scene-close bug
    // class, see AGENTS.md "Scene-hosted references in editor parts").
    // The scene is not an asset; the material reference is (R4).
    std::weak_ptr<Scene_root>                                           m_scene_root;
    Asset_reference                                                     m_material_reference;
    std::shared_ptr<erhe::graphics::Sampler>                            m_sampler;
    std::array<Baked_texture, static_cast<std::size_t>(Separate_channel::count)> m_separate;
    std::shared_ptr<erhe::graphics::Texture>                            m_orm_target;
    std::shared_ptr<erhe::graphics::Texture>                            m_orm_registered;
    std::string                                                         m_base_name{"Material"};
    int                                                                 m_size_index{2}; // 0:256 1:512 2:1024 3:2048
    bool                                                                m_assign_to_material{true};
};

} // namespace editor
