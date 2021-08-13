#pragma once

#include "erhe/components/component.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/xr/xr.hpp"


namespace erhe::graphics
{
    class Framebuffer;
    class OpenGL_state_tracker;
    class Renderbuffer;
    class Texture;
}
namespace erhe::scene
{
    class Camera;
    class ICamera;
    class Mesh;
    class Node;
}
namespace erhe::xr
{
    class Headset;
}

namespace editor
{

class Application;
class Editor_view;
class Editor_rendering;
class Editor_tools;
class Forward_renderer;
class Id_renderer;
class Line_renderer;
class Mesh_memory;
class Scene_manager;
class Scene_root;
class Shadow_renderer;
class Text_renderer;
class Viewport_config;
class Viewport_window;

class Headset_view_resources
{
public:
    Headset_view_resources(erhe::xr::Render_view& render_view,
                           Editor_rendering&      rendering);

    void update(erhe::xr::Render_view& render_view,
                Editor_rendering&      rendering);

    std::shared_ptr<erhe::graphics::Texture>      color_texture;
    std::shared_ptr<erhe::graphics::Texture>      depth_texture;
    //std::unique_ptr<erhe::graphics::Renderbuffer> depth_stencil_renderbuffer;
    std::unique_ptr<erhe::graphics::Framebuffer>  framebuffer;
    std::shared_ptr<erhe::scene::Node>            camera_node;
    std::shared_ptr<erhe::scene::Camera>          camera;
};

class Controller_visualization
{
public:
    Controller_visualization(Mesh_memory&       mesh_memory,
                             Scene_root&        scene_root,
                             erhe::scene::Node* view_root);

    void update  (const erhe::xr::Pose& pose);
    auto get_node() const -> erhe::scene::Node*;

private:
    std::shared_ptr<erhe::scene::Mesh> m_controller_mesh;
};

class Editor_rendering
    : public erhe::components::Component
{
public:
    static constexpr bool s_enable_gui    {true};
    static constexpr bool s_enable_headset{false};

    static constexpr const char* c_name = "Editor_rendering";

    Editor_rendering ();
    ~Editor_rendering() override;

    // Implements Component
    void connect             () override;
    void initialize_component() override;

    void init_state         ();
    void render             (const double time);
    auto to_scene_content   (const glm::vec2 position_in_root) const -> glm::vec2;
    auto is_content_in_focus() const -> bool;
    void render_headset     ();

    erhe::scene::Viewport scene_viewport{0, 0, 0, 0};

private:
    void render_shadowmaps   ();
    void render_id           (const double time);
    void render_clear_primary();
    void render_content      (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void render_selection    (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void render_tool_meshes  (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void begin_frame         ();
    void gui_render          ();

    auto is_primary_scene_output_framebuffer_ready() -> bool;
    void bind_primary_scene_output_framebuffer    ();
    void end_primary_framebuffer                  ();

    auto width () const -> int;
    auto height() const -> int;

    auto get_headset_view_resources(erhe::xr::Render_view& render_view) -> Headset_view_resources&;

    std::unique_ptr<erhe::xr::Headset> m_headset;

    std::shared_ptr<Application>                          m_application;
    std::shared_ptr<Editor_view>                          m_editor_view;
    std::shared_ptr<Editor_tools>                         m_editor_tools;
    std::shared_ptr<Forward_renderer>                     m_forward_renderer;
    std::shared_ptr<Id_renderer>                          m_id_renderer;
    std::shared_ptr<Line_renderer>                        m_line_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Scene_manager>                        m_scene_manager;
    std::shared_ptr<Scene_root>                           m_scene_root;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Text_renderer>                        m_text_renderer;
    std::shared_ptr<Viewport_config>                      m_viewport_config;
    std::shared_ptr<Viewport_window>                      m_viewport_window;

    std::vector<Headset_view_resources>                   m_view_resources;
    std::unique_ptr<Controller_visualization>             m_controller_visualization;

    bool                                                  m_trigger_capture{false};
};

}
