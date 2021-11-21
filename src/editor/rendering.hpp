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
class Configuration;
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
    static constexpr std::string_view c_name{"Editor_rendering"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Editor_rendering ();
    ~Editor_rendering() override;

    // Implements Component
    auto get_type_hash() const -> uint32_t override { return hash; }
    void connect      () override;

    void init_state         ();
    void render             (const double time);
    auto to_scene_content   (const glm::vec2 position_in_root) const -> glm::vec2;
    auto is_content_in_focus() const -> bool;

    erhe::scene::Viewport scene_viewport{0, 0, 0, 0, true};

    void render_clear_primary();
    void render_content      (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void render_selection    (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void render_tool_meshes  (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);
    void render_brush        (erhe::scene::ICamera* camera, const erhe::scene::Viewport viewport);

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

    Application*                          m_application           {nullptr};
    Configuration*                        m_configuration         {nullptr};
    Editor_view*                          m_editor_view           {nullptr};
    Editor_tools*                         m_editor_tools          {nullptr};
    Forward_renderer*                     m_forward_renderer      {nullptr};
    Headset_renderer*                     m_headset_renderer      {nullptr};
    Id_renderer*                          m_id_renderer           {nullptr};
    Line_renderer*                        m_line_renderer         {nullptr};
    erhe::graphics::OpenGL_state_tracker* m_pipeline_state_tracker{nullptr};
    Scene_manager*                        m_scene_manager         {nullptr};
    Scene_root*                           m_scene_root            {nullptr};
    Shadow_renderer*                      m_shadow_renderer       {nullptr};
    Text_renderer*                        m_text_renderer         {nullptr};
    Viewport_config*                      m_viewport_config       {nullptr};
    Viewport_window*                      m_viewport_window       {nullptr};

    bool                                  m_trigger_capture{false};
};

}
