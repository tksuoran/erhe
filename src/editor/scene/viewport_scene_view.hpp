#pragma once

#include "renderers/programs.hpp"
#include "scene/scene_view.hpp"

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene_renderer/face_id_base_provider.hpp"
#include "erhe_scene_renderer/shader_key.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Command_buffer;
    class Render_pass;
    class Renderbuffer;
    class Texture;
}
namespace erhe::imgui {
    class Imgui_host;
    class Imgui_windows;
}
namespace erhe::renderer {
    class Line_renderer_set;
    class Text_renderer;
}
namespace erhe::rendergraph {
    class Multisample_resolve_node;
    class Rendergraph;
}
namespace erhe::scene {
    class Camera;
    class Mesh;
}
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Light_projections;
}

namespace ImViewGuizmo {
    class Context;
}

struct Viewport_config_data;

namespace editor {

enum class Renderer_choice : int {
    forward,
    draw_list
};

static constexpr const char* c_renderer_choice_strings[] = {
    "Forward Renderer",
    "Draw List Renderer"
};

class App_message_bus;
class App_rendering;
class App_scenes;
class Editor_settings_store;
class Physics_window;
class Post_processing;
class Post_processing_node;
class Programs;
class Render_context;
class Scene_root;
class Selection_tool;
class Shadow_render_node;
class Tools;
class Transform_tool;
class Viewport_scene_view;
class Scene_views;

// Per-frame assignment of a unique face-id base to each (content mesh,
// primitive) for the ID-buffer edge-line method. Built once per viewport frame
// and consulted by BOTH the edge-id pre-pass (Content_wide_line_renderer id
// mode) and the polygon-fill pass (Primitive_buffer), so the face ids agree.
// Bases start at 1 (0 = background / "no edge"); each primitive's base is spaced
// by its fill index count (an upper bound on its facet count), so base + facet
// never collides with the next primitive. Unregistered meshes return 0, which
// the EDGE_LINES_FROM_ID vertex shader treats as "no edge".
class Face_id_registry : public erhe::scene_renderer::Face_id_base_provider
{
public:
    void clear();
    void add_mesh(const erhe::scene::Mesh& mesh);
    [[nodiscard]] auto get_face_id_base(const erhe::scene::Mesh& mesh, std::size_t primitive_index) const -> uint32_t override;

private:
    std::unordered_map<const erhe::scene::Mesh*, std::vector<uint32_t>> m_bases;
    uint32_t m_next_base{1};
};

// Rendergraph producer node for rendering contents of scene into output rendergraph node.
// - See execute_rendergraph_node(): render_viewport_main() is used to render to connected
//   consumer rendergraph node, which provides viewport and framebuffer
// 
// Typical output rendergraph nodes are:
//  - Post_processing_node
//  - default Rendergraph_node representing default framebuffer
class Viewport_scene_view
    : public Scene_view
    , public erhe::rendergraph::Texture_rendergraph_node
    , public std::enable_shared_from_this<Viewport_scene_view>
{
public:
    Viewport_scene_view(
        Editor_settings_store*                      editor_settings_store,
        const Viewport_config_data&                 viewport_config_data,
        App_context&                                context,
        erhe::rendergraph::Rendergraph&             rendergraph,
        Tools&                                      tools,
        std::string_view                            name,
        const char*                                 ini_label,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera,
        int                                         msaa_sample_count,
        bool                                        enable_post_processing
    );
    ~Viewport_scene_view() noexcept override;

    // Implements Scene_view
    auto get_camera                () const -> std::shared_ptr<erhe::scene::Camera>         override;
    auto get_perspective_scale     () const -> float                                        override;
    auto get_rendergraph_node      () -> erhe::rendergraph::Rendergraph_node*               override;
    auto get_shadow_render_node    () const -> Shadow_render_node*                          override;
    auto as_viewport_scene_view    () -> Viewport_scene_view*                               override;
    auto as_viewport_scene_view    () const -> const Viewport_scene_view*                   override;
    auto get_closest_point_on_line (glm::vec3 P0, glm::vec3 P1) -> std::optional<glm::vec3> override;
    auto get_closest_point_on_plane(glm::vec3 N , glm::vec3 P ) -> std::optional<glm::vec3> override;

    // Implements Rendergraph_node
    auto get_type_name           () const -> std::string_view override { return "Viewport_scene_view"; }
    void execute_rendergraph_node(erhe::graphics::Command_buffer& command_buffer) override;

    // Post-processing overlay pass (issue #230). Called by Viewport_overlay_node
    // AFTER the post-processing node when post-processing is enabled. Draws the
    // tool gizmo / hotbar rendertarget overlay meshes on top of the
    // post-processed image (input_texture), depth-testing against this view's
    // content render target depth/stencil attachment (which is stored for that
    // purpose). get_overlay_output_texture returns the resulting image.
    void render_overlay_pass(
        erhe::graphics::Command_buffer&                 command_buffer,
        const std::shared_ptr<erhe::graphics::Texture>& input_texture
    );
    [[nodiscard]] auto get_overlay_output_texture() const -> std::shared_ptr<erhe::graphics::Texture>;

    // Public API
    void set_window_viewport         (erhe::math::Viewport viewport);
    void set_is_scene_view_hovered   (bool is_hovered);
    void set_camera                  (const std::shared_ptr<erhe::scene::Camera>& camera);
    void update_pointer_2d_position  (glm::vec2 position_in_viewport);
    void update_hover                (bool ray_only = false);
    void request_cursor_relative_hold(bool relative_hold_enable);
    
    void viewport_toolbar            () override;
    
    void set_shader_debug            (erhe::scene_renderer::Shader_debug shader_debug);
    auto get_shader_debug            () const -> erhe::scene_renderer::Shader_debug;

    void set_renderer_choice(Renderer_choice choice);
    auto get_renderer_choice() const -> Renderer_choice;

    [[nodiscard]] auto get_ini_label                       () const -> const char* { return m_ini_label; }
    [[nodiscard]] auto get_viewport_from_window            (glm::vec2 position_in_window) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport                 (glm::vec3 position_in_world ) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto unproject_to_world                  (glm::vec3 position_in_window) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto is_scene_view_hovered               () const -> bool;
    [[nodiscard]] auto get_window_viewport                 () const -> const erhe::math::Viewport&;
    [[nodiscard]] auto get_projection_viewport             () const -> const erhe::math::Viewport&;
    [[nodiscard]] auto get_position_in_viewport            () const -> std::optional<glm::vec2>;
    [[nodiscard]] auto get_position_in_world_viewport_depth(float viewport_depth) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto get_show_navigation_gizmo           () const -> bool;
    [[nodiscard]] auto get_cursor_relative_hold            () const -> bool;

    // Per-viewport navigation gizmo (ImGui overlay). Drawn by Viewport_window and
    // driven (orbit/zoom/pan/snap) by Navigation_gizmo_tool via erhe::commands.
    [[nodiscard]] auto get_navigation_gizmo                () -> ImViewGuizmo::Context&;

private:
    void update_hover_with_id_render();

    // (Re)builds the overlay render target (color matching the content sample
    // count + single-sample resolved output) and its render pass, which loads
    // the content render target's depth/stencil attachment. See issue #230.
    void update_overlay_render_target(int width, int height);

    // Implements Scene_view per-view "Scene and Camera" persistence: adds the
    // camera, shader_debug and renderer_choice that live on this derived view.
    void write_scene_and_camera_settings(Scene_and_camera_settings& out) const override;
    auto resolve_pending_scene_and_camera() -> bool override;

    static int s_serial;

    std::optional<glm::vec2>           m_position_in_viewport {};

    std::string                        m_name                 {};
    const char*                        m_ini_label            {nullptr};
    std::weak_ptr<Scene_root>          m_tool_scene_root      {};
    std::weak_ptr<erhe::scene::Camera> m_camera               {};

    // Host window bounds in host space (ImGui window, ImGui viewport / Context_window)
    erhe::math::Viewport               m_window_viewport      {0, 0, 0, 0};

    // viewport for 3d camera
    erhe::math::Viewport               m_projection_viewport  {0, 0, 0, 0};

    erhe::scene_renderer::Shader_debug m_shader_debug                   {erhe::scene_renderer::Shader_debug::none};
    Renderer_choice                    m_renderer_choice                {Renderer_choice::forward};
    bool                               m_is_scene_view_hovered          {false};
    bool                               m_show_navigation_gizmo          {true};
    bool                               m_relative_hold_enable           {false};
    std::unique_ptr<ImViewGuizmo::Context> m_navigation_gizmo;
    Property_editor m_property_editor;

    // Rebuilt each frame the ID-buffer edge-line method is active; feeds the same
    // per-(mesh,primitive) base to the edge-id pre-pass and the polygon-fill pass.
    Face_id_registry                   m_face_id_registry;

    // The content meshes registered for the edge method this frame, collected
    // during the edge feed so the seed pass can render exactly that visible
    // content. Persistent (cleared, capacity kept) to avoid per-frame allocation.
    std::vector<std::shared_ptr<erhe::scene::Mesh>> m_seed_meshes;

    // Post-processing overlay pass (issue #230). When post-processing is enabled
    // the content render pass stores depth/stencil and these resources draw the
    // tool / rendertarget overlay after post-processing, sharing that depth.
    bool                                         m_post_processing_enabled{false};
    std::shared_ptr<erhe::graphics::Texture>     m_overlay_color_texture;    // multisampled when MSAA, else == resolved
    std::shared_ptr<erhe::graphics::Texture>     m_overlay_resolved_texture; // single-sample output shown by ImGui
    std::unique_ptr<erhe::graphics::Render_pass> m_overlay_render_pass;
    const erhe::graphics::Texture*               m_overlay_depth_texture{nullptr};
    int                                          m_overlay_width {0};
    int                                          m_overlay_height{0};
};

}
