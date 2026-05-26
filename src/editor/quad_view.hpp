#pragma once

#include "rendertarget_view.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace erhe::graphics {
    class Command_buffer;
    class Device;
    class Render_pass;
}
namespace erhe::imgui          { class Imgui_renderer; }
namespace erhe::rendergraph    { class Rendergraph; }
namespace erhe::scene          { class Node; }
namespace erhe::scene_renderer { class Mesh_memory; }
namespace erhe::xr             { class Quad_layer; }

namespace editor {

class App_context;
class Headset_view;
class Rendertarget_imgui_host;
class Rendertarget_mesh;
class Scene_root;

// Unified owner of a 2D UI quad that is drawn either as scene geometry
// (Path A: Rendertarget_mesh, used on desktop and as VR fallback) or as an
// OpenXR quad composition layer (Path B: erhe::xr::Quad_layer). Hud / Hotbar
// own a Quad_view and drive it through this path-agnostic interface; the
// per-path details live here. The owned Rendertarget_imgui_host renders the UI
// content the same way in both paths -- in Path A into the mesh texture, in
// Path B directly into the acquired quad swapchain image.
//
// Path selection happens once at construction: Path B is chosen when OpenXR is
// active, the composition_quad_layers config knob is set, and the quad
// swapchain is created successfully; otherwise Path A is used.
class Quad_view : public Rendertarget_view
{
public:
    // Resources are passed explicitly (not read from App_context): a Quad_view
    // may be created from a part constructor (e.g. Hud), where App_context's
    // pointers are not yet initialized. context is only stored (forwarded to the
    // ImGui host and used at runtime), never read during construction.
    // headset_view selects Path B: when it is non-null, has a headset, and that
    // headset's config enables composition quad layers, the quad swapchain is
    // created (falling back to Path A on failure).
    Quad_view(
        App_context&                       context,
        erhe::graphics::Device&            graphics_device,
        erhe::graphics::Command_buffer&    command_buffer,
        erhe::scene_renderer::Mesh_memory& mesh_memory,
        erhe::imgui::Imgui_renderer&       imgui_renderer,
        erhe::rendergraph::Rendergraph&    rendergraph,
        Scene_root&                        scene_root,
        Headset_view*                      headset_view,
        int                                width,
        int                                height,
        float                              pixels_per_meter,
        std::string_view                   debug_label,
        bool                               imgui_ini
    );
    ~Quad_view() noexcept;
    Quad_view     (const Quad_view&) = delete;
    void operator=(const Quad_view&) = delete;
    Quad_view     (Quad_view&&)      = delete;
    void operator=(Quad_view&&)      = delete;

    [[nodiscard]] auto uses_composition_layer() const -> bool;
    [[nodiscard]] auto get_imgui_host        () const -> Rendertarget_imgui_host*;
    [[nodiscard]] auto get_imgui_host_shared () const -> std::shared_ptr<Rendertarget_imgui_host>;

    // The quad's world transform (last set via set_world_from_quad /
    // set_world_from_node). Valid in both paths.
    [[nodiscard]] auto get_world_from_quad() const -> glm::mat4;

    // Intersect a world-space ray with the quad rectangle. Returns the world
    // hit point if the ray crosses the quad within its bounds, else nullopt.
    // Used by the Hud drag to grab the quad in the composition-layer path,
    // where there is no scene mesh to hover.
    [[nodiscard]] auto intersect_ray(glm::vec3 origin_in_world, glm::vec3 direction_in_world) const -> std::optional<glm::vec3>;

    // Path A scene objects. Both are null when uses_composition_layer() is true.
    [[nodiscard]] auto get_rendertarget_mesh() const -> Rendertarget_mesh*;
    [[nodiscard]] auto get_node             () const -> erhe::scene::Node*;

    // Position the quad. Path A: reparent the node under parent_node (a null
    // parent detaches the node) and set its world transform.
    void set_world_from_quad(const std::shared_ptr<erhe::scene::Node>& parent_node, const glm::mat4& world_from_quad);

    // Set the quad's world transform without reparenting. Path A: callers that
    // parent the node once (e.g. Hud) use this for per-frame repositioning.
    void set_world_from_node(const glm::mat4& world_from_quad);

    // Toggle the quad's visibility and the ImGui host's enabled state. Does not
    // touch any owning tool's window visibility -- the tool handles that.
    void set_visible(bool visible);

    // Make the quad visible from behind as well. Path B only: the OpenXR quad
    // layer submits a second back-facing quad reusing the same image. The back
    // face is readable but horizontally flipped in world space (see
    // erhe::xr::Quad_layer::build_back_layer). No-op in Path A (scene mesh).
    void set_double_sided(bool double_sided);

    // Depth-test the quad against the scene so geometry can occlude it (Path B
    // only; requires runtime support + submitted projection depth). No-op Path A.
    void set_depth_tested(bool depth_tested);

    // Composition order: higher composites later (on top). Path B only; used to
    // keep this quad in front of others (e.g. the Hud over the Hotbar).
    void set_composition_order(int order);

    // --- Rendertarget_view (render target + input sources for the ImGui host) ---
    [[nodiscard]] auto get_width () const -> float override; // content width in pixels
    [[nodiscard]] auto get_height() const -> float override; // content height in pixels
    [[nodiscard]] auto acquire_render_pass(erhe::graphics::Command_buffer& command_buffer) -> erhe::graphics::Render_pass* override;
    void               finish_render      (erhe::graphics::Command_buffer& command_buffer, App_context& context) override;
    [[nodiscard]] auto get_output_texture () const -> std::shared_ptr<erhe::graphics::Texture> override;
    [[nodiscard]] auto is_quad_visible       () const -> bool override;
    [[nodiscard]] auto get_pointer           () const -> std::optional<glm::vec2> override;
    [[nodiscard]] auto world_to_window       (glm::vec3 world_position) const -> std::optional<glm::vec2> override;
    [[nodiscard]] auto get_plane_world_origin() const -> glm::vec3 override;
    [[nodiscard]] auto get_plane_world_normal() const -> glm::vec3 override;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    void update_headset_hand_tracking() override;
#endif

private:
    App_context&                             m_context;
    Headset_view*                            m_headset_view{nullptr};
    bool                                     m_uses_composition_layer{false};

    // Content dimensions (both paths).
    int                                      m_pixel_width {0};
    int                                      m_pixel_height{0};
    float                                    m_local_width {0.0f}; // meters
    float                                    m_local_height{0.0f}; // meters

    // Always created.
    std::shared_ptr<Rendertarget_imgui_host> m_imgui_host;

    // Path A.
    std::shared_ptr<Rendertarget_mesh>       m_rendertarget_mesh;
    std::shared_ptr<erhe::scene::Node>       m_rendertarget_node;

    // Path B. m_world_from_quad is the world transform last set via
    // set_world_from_quad/set_world_from_node, used both to derive the stage-
    // space quad layer pose and to map the controller ray to window coords.
    glm::mat4                                m_world_from_quad{1.0f};
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::unique_ptr<erhe::xr::Quad_layer>    m_quad_layer;
    // Render passes for the quad swapchain images, keyed by the wrapped texture
    // (stable for the swapchain's lifetime), built lazily on first use.
    std::unordered_map<erhe::graphics::Texture*, std::shared_ptr<erhe::graphics::Render_pass>> m_quad_render_passes;
#endif
};

} // namespace editor
