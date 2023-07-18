#pragma once

#include "renderers/programs.hpp"
#include "scene/scene_view.hpp"

#include "erhe/rendergraph/rendergraph_node.hpp"
#include "erhe/commands/command.hpp"
#include "erhe/imgui/imgui_window.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/toolkit/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::geometry {
    class Geometry;
}
namespace erhe::graphics {
    class Framebuffer;
    class Texture;
    class Renderbuffer;
}
namespace erhe::imgui {
    class Imgui_viewport;
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

namespace editor
{

class Editor_message_bus;
class Editor_rendering;
class Editor_scenes;
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
class Viewport_config_window;
class Viewport_window;
class Viewport_windows;


// Rendergraph producer node for rendering contents of scene into output rendergraph node.
//
// Typical output rendergraph nodes are:
//  - Multisample_resolve_node
//  - Post_processing_node
//  - default Rendergraph_node representing default framebuffer
// Inputs:  "shadow_maps"
// Outputs: "viewport"
class Viewport_window
    : public Scene_view
    , public erhe::rendergraph::Rendergraph_node
    , public std::enable_shared_from_this<Viewport_window>
{
public:
    Viewport_window(
        Editor_context&                             editor_context,
        erhe::rendergraph::Rendergraph&             rendergraph,
        Tools&                                      tools,
        Viewport_config_window&                     viewport_config_window,
        const std::string_view                      name,
        const char*                                 ini_label,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera
    );
    ~Viewport_window() noexcept override;

    // Implements Scene_view
    auto get_scene_root            () const -> std::shared_ptr<Scene_root>                              override;
    auto get_camera                () const -> std::shared_ptr<erhe::scene::Camera>                     override;
    auto get_rendergraph_node      () -> erhe::rendergraph::Rendergraph_node*                           override;
    auto as_viewport_window        () -> Viewport_window*                                               override;
    auto as_viewport_window        () const -> const Viewport_window*                                   override;
    auto get_closest_point_on_line (const glm::vec3 P0, const glm::vec3 P1) -> std::optional<glm::vec3> override;
    auto get_closest_point_on_plane(const glm::vec3 N , const glm::vec3 P ) -> std::optional<glm::vec3> override;

    // Implements Rendergraph_node
    auto get_type_name           () const -> std::string_view override { return "Viewport_window"; }
    void execute_rendergraph_node() override;

    // Public API
    void reconfigure               (int sample_count);
    void set_window_viewport       (erhe::toolkit::Viewport viewport);
    void set_is_hovered            (bool is_hovered);
    void set_camera                (const std::shared_ptr<erhe::scene::Camera>& camera);
    void update_pointer_2d_position(glm::vec2 position_in_viewport);
    void update_hover              ();
    void link_to                   (std::shared_ptr<erhe::rendergraph::Multisample_resolve_node> node);
    void link_to                   (std::shared_ptr<Post_processing_node> node);
    void set_final_output          (std::shared_ptr<Rendergraph_node> node);

    [[nodiscard]] auto ini_label               () const -> const char* { return m_ini_label; }
    [[nodiscard]] auto viewport_from_window    (const glm::vec2 position_in_window) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport     (const glm::vec3 position_in_world ) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto unproject_to_world      (const glm::vec3 position_in_window) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto is_hovered              () const -> bool;
    [[nodiscard]] auto window_viewport         () const -> const erhe::toolkit::Viewport&;
    [[nodiscard]] auto projection_viewport     () const -> const erhe::toolkit::Viewport&;
    [[nodiscard]] auto get_position_in_viewport() const -> std::optional<glm::vec2>;
    [[nodiscard]] auto position_in_world_viewport_depth(float viewport_depth) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto viewport_toolbar        () -> bool;
    [[nodiscard]] auto get_shadow_render_node  () const -> Shadow_render_node* override;
    [[nodiscard]] auto get_post_processing_node() -> Post_processing_node*;
    [[nodiscard]] auto get_final_output        () -> Rendergraph_node*;

private:
    [[nodiscard]] auto get_override_shader_stages() const -> erhe::graphics::Shader_stages*;

    void update_hover_with_id_render();

    static int s_serial;

    // TODO Consider if this is the right place for ownership
    std::shared_ptr<erhe::rendergraph::Multisample_resolve_node> m_multisample_resolve_node;
    std::shared_ptr<Post_processing_node>                        m_post_processing_node    ;
    std::shared_ptr<Rendergraph_node>                            m_final_output            ;

    std::optional<glm::vec2>           m_position_in_viewport;

    std::string                        m_name;
    const char*                        m_ini_label{nullptr};
    std::weak_ptr<Scene_root>          m_scene_root;
    std::weak_ptr<Scene_root>          m_tool_scene_root;
    std::weak_ptr<erhe::scene::Camera> m_camera               {};
    erhe::toolkit::Viewport            m_window_viewport      {0, 0, 0, 0, true};
    erhe::toolkit::Viewport            m_projection_viewport  {0, 0, 0, 0, true};
    //Shader_stages_variant              m_shader_stages_variant{Shader_stages_variant::standard};
    Shader_stages_variant              m_shader_stages_variant{Shader_stages_variant::circular_brushed_metal};
    bool                               m_is_hovered           {false};
};

} // namespace editor
