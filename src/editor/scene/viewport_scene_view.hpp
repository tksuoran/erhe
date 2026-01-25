#pragma once

#include "renderers/programs.hpp"
#include "scene/scene_view.hpp"

#include "erhe_rendergraph/texture_rendergraph_node.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_math/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Render_pass;
    class Renderbuffer;
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
}
namespace erhe::scene_renderer {
    class Forward_renderer;
    class Light_projections;
}

namespace editor {

class App_message_bus;
class App_rendering;
class App_scenes;
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
        App_context&                                context,
        erhe::rendergraph::Rendergraph&             rendergraph,
        Tools&                                      tools,
        std::string_view                            name,
        const char*                                 ini_label,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera,
        int                                         msaa_sample_count
    );
    ~Viewport_scene_view() noexcept override;

    // Implements Scene_view
    auto get_camera                () const -> std::shared_ptr<erhe::scene::Camera>                     override;
    auto get_perspective_scale     () const -> float                                                    override;
    auto get_rendergraph_node      () -> erhe::rendergraph::Rendergraph_node*                           override;
    auto get_shadow_render_node    () const -> Shadow_render_node*                                      override;
    auto as_viewport_scene_view    () -> Viewport_scene_view*                                           override;
    auto as_viewport_scene_view    () const -> const Viewport_scene_view*                               override;
    auto get_closest_point_on_line (glm::vec3 P0, glm::vec3 P1) -> std::optional<glm::vec3> override;
    auto get_closest_point_on_plane(glm::vec3 N , glm::vec3 P ) -> std::optional<glm::vec3> override;

    // Implements Rendergraph_node
    auto get_type_name           () const -> std::string_view override { return "Viewport_scene_view"; }
    void execute_rendergraph_node() override;

    // Public API
    void set_window_viewport       (erhe::math::Viewport viewport);
    void set_is_scene_view_hovered (bool is_hovered);
    void set_camera                (const std::shared_ptr<erhe::scene::Camera>& camera);
    void update_pointer_2d_position(glm::vec2 position_in_viewport);
    void update_hover              (bool ray_only = false);

    void set_shader_stages_variant(Shader_stages_variant variant);
    auto get_shader_stages_variant() const -> Shader_stages_variant;

    [[nodiscard]] auto get_ini_label                       () const -> const char* { return m_ini_label; }
    [[nodiscard]] auto get_viewport_from_window            (glm::vec2 position_in_window) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport                 (glm::vec3 position_in_world ) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto unproject_to_world                  (glm::vec3 position_in_window) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto is_scene_view_hovered               () const -> bool;
    [[nodiscard]] auto get_window_viewport                 () const -> const erhe::math::Viewport&;
    [[nodiscard]] auto get_projection_viewport             () const -> const erhe::math::Viewport&;
    [[nodiscard]] auto get_position_in_viewport            () const -> std::optional<glm::vec2>;
    [[nodiscard]] auto get_position_in_world_viewport_depth(float viewport_depth) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto viewport_toolbar                    () -> bool;
    [[nodiscard]] auto get_show_navigation_gizmo           () const -> bool;

private:
    [[nodiscard]] auto get_override_shader_stages() const -> const erhe::graphics::Shader_stages*;

    void update_hover_with_id_render();

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

    //Shader_stages_variant              m_shader_stages_variant{Shader_stages_variant::standard};
    Shader_stages_variant              m_shader_stages_variant{Shader_stages_variant::standard};
    bool                               m_is_scene_view_hovered{false};
    bool                               m_show_navigation_gizmo{true};
};

}
