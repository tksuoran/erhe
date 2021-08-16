#pragma once

#include "erhe/components/component.hpp"
#include "erhe/scene/viewport.hpp"

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

namespace editor
{

class Application;
class Editor_view;
class Editor_rendering;
class Editor_tools;
class Forward_renderer;
class Headset_renderer;
class Id_renderer;
class Line_renderer;
class Mesh_memory;
class Scene_manager;
class Scene_root;
class Shadow_renderer;
class Text_renderer;
class Viewport_config;
class Viewport_window;

class Editor_rendering
    : public erhe::components::Component
{
public:
    static constexpr bool s_enable_gui    {true};
    static constexpr bool s_enable_headset{true};

    static constexpr std::string_view c_name{"Editor_rendering"};

    Editor_rendering ();
    ~Editor_rendering() override;

    // Implements Component
    void connect             () override;

    void init_state         ();
    void render             (const double time);
    auto to_scene_content   (const glm::vec2 position_in_root) const -> glm::vec2;
    auto is_content_in_focus() const -> bool;

    erhe::scene::Viewport scene_viewport{0, 0, 0, 0};

    void render_clear_primary();
    void render_content      (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void render_selection    (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void render_tool_meshes  (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);

private:
    void render_shadowmaps   ();
    void render_id           (const double time);
    void begin_frame         ();
    void gui_render          ();

    auto is_primary_scene_output_framebuffer_ready() -> bool;
    void bind_primary_scene_output_framebuffer    ();
    void end_primary_framebuffer                  ();

    auto width () const -> int;
    auto height() const -> int;

    std::shared_ptr<Application>                          m_application;
    std::shared_ptr<Editor_view>                          m_editor_view;
    std::shared_ptr<Editor_tools>                         m_editor_tools;
    std::shared_ptr<Forward_renderer>                     m_forward_renderer;
    std::shared_ptr<Headset_renderer>                     m_headset_renderer;
    std::shared_ptr<Id_renderer>                          m_id_renderer;
    std::shared_ptr<Line_renderer>                        m_line_renderer;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Scene_manager>                        m_scene_manager;
    std::shared_ptr<Scene_root>                           m_scene_root;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Text_renderer>                        m_text_renderer;
    std::shared_ptr<Viewport_config>                      m_viewport_config;
    std::shared_ptr<Viewport_window>                      m_viewport_window;

    bool                                                  m_trigger_capture{false};
};

}
