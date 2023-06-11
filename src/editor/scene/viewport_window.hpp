#pragma once

#include "windows/viewport_config_window.hpp"
#include "renderers/programs.hpp"
#include "scene/scene_view.hpp"

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/imgui/imgui_window.hpp"
#include "erhe/components/components.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/viewport.hpp"

#include <glm/glm.hpp>

#include <memory>

namespace erhe::application
{
    class Configuration;
    class Imgui_viewport;
    class Imgui_windows;
    class Multisample_resolve_node;
    class Rendergraph;
    class View;
}

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::graphics
{
    class Framebuffer;
    class Texture;
    class Renderbuffer;
    class OpenGL_state_tracker;
}

namespace erhe::renderer
{
    class Light_projections;
}

namespace erhe::scene
{
    class Camera;
}

namespace editor
{

class Editor_rendering;
class Editor_scenes;
class Grid_tool;
class Id_renderer;
class Node_raytrace;
class Physics_window;
class Post_processing;
class Post_processing_node;
class Render_context;
class Scene_root;
class Selection_tool;
class Shadow_render_node;
class Transform_tool;
class Viewport_window;
class Viewport_windows;


/// <summary>
/// Rendergraph producer node for rendering contents of scene into output rendergraph node.
/// </summary>
/// Typical output rendergraph nodes are:
///  - Multisample_resolve_node
///  - Post_processing_node
///  - default Rendergraph_node representing default framebuffer
/// Inputs:  "shadow_maps"
/// Outputs: "viewport"
class Viewport_window
    : public Scene_view
    , public erhe::application::Rendergraph_node
    , public std::enable_shared_from_this<Viewport_window>
{
public:
    static constexpr std::string_view c_type_name{"Viewport_window"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Viewport_window(
        const std::string_view                      name,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera
    );
    ~Viewport_window();

    // Implements Scene_view
    auto get_scene_root            () const -> std::shared_ptr<Scene_root>                              override;
    auto get_camera                () const -> std::shared_ptr<erhe::scene::Camera>                     override;
    auto get_rendergraph_node      () -> std::shared_ptr<erhe::application::Rendergraph_node>           override;
    auto as_viewport_window        () -> Viewport_window*                                               override;
    auto as_viewport_window        () const -> const Viewport_window*                                   override;
    auto get_closest_point_on_line (const glm::vec3 P0, const glm::vec3 P1) -> std::optional<glm::vec3> override;
    auto get_closest_point_on_plane(const glm::vec3 N , const glm::vec3 P ) -> std::optional<glm::vec3> override;

    // Implements Rendergraph_node
    auto type_name() const -> std::string_view override { return c_type_name; }
    auto type_hash() const -> uint32_t         override { return c_type_hash; }
    void execute_rendergraph_node() override;

    // Public API
    void reconfigure        (int sample_count);
    void set_window_viewport(int x, int y, int width, int height);
    void set_is_hovered     (bool is_hovered);
    void set_camera         (const std::shared_ptr<erhe::scene::Camera>& camera);
    auto get_config         () -> Viewport_config*;

    [[nodiscard]] auto viewport_from_window(const glm::vec2 position_in_window) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport (const glm::vec3 position_in_world ) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto unproject_to_world  (const glm::vec3 position_in_window) const -> std::optional<glm::vec3>;
    [[nodiscard]] auto is_hovered         () const -> bool;
    [[nodiscard]] auto window_viewport    () const -> const erhe::scene::Viewport&;
    [[nodiscard]] auto projection_viewport() const -> const erhe::scene::Viewport&;

    // call with const glm::vec2 position_in_viewport = m_window.viewport_from_window(position_in_window);
    // Also updates hover slots
    void update_pointer_2d_position(glm::vec2 position_in_viewport);

    void update_hover();

    [[nodiscard]] auto get_position_in_viewport() const -> std::optional<glm::vec2>;

    [[nodiscard]] auto position_in_world_viewport_depth(float viewport_depth) const -> std::optional<glm::vec3>;

    auto get_shadow_render_node() const -> Shadow_render_node* override;

    [[nodiscard]] auto viewport_toolbar() -> bool;

    // TODO Consider if these links are a good thing, or if they should
    //      be discovered from the graph instead.
    void link_to                 (std::weak_ptr<erhe::application::Multisample_resolve_node> node);
    void link_to                 (std::weak_ptr<Post_processing_node> node);
    auto get_post_processing_node() -> std::shared_ptr<Post_processing_node>;
    void set_final_output        (std::weak_ptr<Rendergraph_node> node);
    auto get_final_output        () -> std::weak_ptr<Rendergraph_node>;

private:
    void update_hover_with_id_render();

    [[nodiscard]] auto get_override_shader_stages() const -> erhe::graphics::Shader_stages*;

    static int s_serial;

    Viewport_config                    m_viewport_config;

    // TODO Consider if these links are a good thing, or if they should
    //      be discovered from the graph instead.
    std::weak_ptr<erhe::application::Multisample_resolve_node> m_multisample_resolve_node;
    std::weak_ptr<Post_processing_node>                        m_post_processing_node;
    std::weak_ptr<Rendergraph_node>                            m_final_output;

    std::optional<glm::vec2>           m_position_in_viewport;

    std::string                        m_name;
    std::weak_ptr<Scene_root>          m_scene_root;
    std::weak_ptr<Scene_root>          m_tool_scene_root;
    std::weak_ptr<erhe::scene::Camera> m_camera               {};
    erhe::scene::Viewport              m_window_viewport      {0, 0, 0, 0, true};
    erhe::scene::Viewport              m_projection_viewport  {0, 0, 0, 0, true};
    Shader_stages_variant              m_shader_stages_variant{Shader_stages_variant::circular_brushed_metal};
    bool                               m_is_hovered           {false};
};

} // namespace editor
