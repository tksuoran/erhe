#pragma once

#include "windows/viewport_config.hpp"
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

namespace erhe::scene
{
    class Camera;
}

namespace editor
{

class Editor_rendering;
class Grid_tool;
class Id_renderer;
class Light_projections;
class Node_raytrace;
class Physics_window;
class Post_processing;
class Post_processing_node;
class Render_context;
class Scene_root;
class Shadow_render_node;
class Trs_tool;
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
{
public:
    static constexpr std::string_view c_type_name{"Viewport_window"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Viewport_window(
        const std::string_view                      name,
        const erhe::components::Components&         components,
        const std::shared_ptr<Scene_root>&          scene_root,
        const std::shared_ptr<erhe::scene::Camera>& camera
    );
    ~Viewport_window();

    // Implements Scene_view
    [[nodiscard]] auto get_scene_root    () const -> std::shared_ptr<Scene_root> override;
    [[nodiscard]] auto get_camera        () const -> std::shared_ptr<erhe::scene::Camera> override;
    [[nodiscard]] auto as_viewport_window() -> Viewport_window* override;
    [[nodiscard]] auto as_viewport_window() const -> const Viewport_window* override;

    // Implements Rendergraph_node
    [[nodiscard]] auto type_name() const -> std::string_view override { return c_type_name; }
    [[nodiscard]] auto type_hash() const -> uint32_t         override { return c_type_hash; }
    void execute_rendergraph_node() override;

    // Public API
    void reconfigure        (int sample_count);
    void connect            (Viewport_windows* viewport_windows);
    void set_window_viewport(int x, int y, int width, int height);
    void set_is_hovered     (bool is_hovered);
    void set_camera         (const std::shared_ptr<erhe::scene::Camera>& camera);

    [[nodiscard]] auto to_scene_content   (const glm::vec2 position_in_root) const -> glm::vec2;
    [[nodiscard]] auto project_to_viewport(const glm::dvec3 position_in_world) const -> std::optional<glm::dvec3>;
    [[nodiscard]] auto unproject_to_world (const glm::dvec3 position_in_window) const -> std::optional<glm::dvec3>;
    [[nodiscard]] auto is_hovered         () const -> bool;
    [[nodiscard]] auto window_viewport    () const -> const erhe::scene::Viewport&;
    [[nodiscard]] auto projection_viewport() const -> const erhe::scene::Viewport&;

    // Pointer context API
    void raytrace         ();
    void update_grid_hover();

    // call with const glm::vec2 position_in_window = m_window.to_scene_content(position);
    void update_pointer_context(
        Id_renderer& id_renderer,
        glm::vec2    position_in_viewport
    );

    [[nodiscard]] auto position_in_world_viewport_depth(double viewport_depth) const -> std::optional<glm::dvec3>;

    auto get_shadow_render_node() const -> Shadow_render_node* override;

    void imgui_toolbar();

    // TODO Consider if these links are a good thing, or if they should
    //      be discovered from the graph instead.
    void link_to                 (std::weak_ptr<erhe::application::Multisample_resolve_node> node);
    void link_to                 (std::weak_ptr<Post_processing_node> node);
    auto get_post_processing_node() -> std::shared_ptr<Post_processing_node>;
    void set_final_output        (std::weak_ptr<Rendergraph_node> node);
    auto get_final_output        () -> std::weak_ptr<Rendergraph_node>;

private:
    [[nodiscard]] auto get_override_shader_stages() const -> erhe::graphics::Shader_stages*;

    static int s_serial;

    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Rendergraph>       m_render_graph;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Editor_rendering>                     m_editor_rendering;
    std::shared_ptr<Grid_tool>                            m_grid_tool;
    std::shared_ptr<Physics_window>                       m_physics_window;
    std::shared_ptr<Post_processing>                      m_post_processing;
    std::shared_ptr<Programs>                             m_programs;
    std::shared_ptr<Trs_tool>                             m_trs_tool;
    std::shared_ptr<Viewport_config>                      m_viewport_config;

    // TODO Consider if these links are a good thing, or if they should
    //      be discovered from the graph instead.
    std::weak_ptr<erhe::application::Multisample_resolve_node> m_multisample_resolve_node;
    std::weak_ptr<Post_processing_node>                        m_post_processing_node;
    std::weak_ptr<Rendergraph_node>                            m_final_output;

    std::string                        m_name;
    std::weak_ptr<Scene_root>          m_scene_root;
    std::weak_ptr<Scene_root>          m_tool_scene_root;
    std::weak_ptr<erhe::scene::Camera> m_camera                {};
    Viewport_windows*                  m_viewport_windows      {nullptr};
    erhe::scene::Viewport              m_window_viewport       {0, 0, 0, 0, true};
    erhe::scene::Viewport              m_projection_viewport   {0, 0, 0, 0, true};
    Shader_stages_variant              m_shader_stages_variant {Shader_stages_variant::standard};
    bool                               m_is_hovered            {false};
};

} // namespace editor
