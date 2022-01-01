#pragma once

#include "tools/pointer_context.hpp"
#include "windows/viewport_window.hpp"
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
class Editor_time;
class Editor_tools;
class Forward_renderer;
class Log_window;
class Headset_renderer;
class Id_renderer;
class Line_renderer;
class Pointer_context;
class Mesh_memory;
class Scene_builder;
class Scene_root;
class Shadow_renderer;
class Text_renderer;
class Viewport_windows;

class Render_context
{
public:
    Viewport_window*      window         {nullptr};
    Viewport_config*      viewport_config{nullptr};
    erhe::scene::ICamera* camera         {nullptr};
    erhe::scene::Viewport viewport       {0, 0, 0, 0, true};
};

class Editor_rendering
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Editor_rendering"};
    static constexpr uint32_t hash {
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Editor_rendering ();
    ~Editor_rendering() override;

    // Implements Component
    [[nodiscard]]
    auto get_type_hash() const -> uint32_t override { return hash; }
    void connect      () override;

    void init_state         ();
    void render             ();

    [[nodiscard]]
    auto to_scene_content   (const glm::vec2 position_in_root) const -> glm::vec2;
    //auto is_content_in_focus() const -> bool;

    void render_viewport   (const Render_context& context, const bool has_pointer);
    void render_content    (const Render_context& context);
    void render_selection  (const Render_context& context);
    void render_tool_meshes(const Render_context& context);
    void render_brush      (const Render_context& context);
    void render_id         (const Render_context& context);
    void clear             ();

private:
    void begin_frame     ();
    void render_viewports();
    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    Application*                          m_application           {nullptr};
    Configuration*                        m_configuration         {nullptr};
    Editor_view*                          m_editor_view           {nullptr};
    Editor_time*                          m_editor_time           {nullptr};
    Editor_tools*                         m_editor_tools          {nullptr};
    Forward_renderer*                     m_forward_renderer      {nullptr};
    Log_window*                           m_log_window            {nullptr};
    Headset_renderer*                     m_headset_renderer      {nullptr};
    Id_renderer*                          m_id_renderer           {nullptr};
    Line_renderer*                        m_line_renderer         {nullptr};
    erhe::graphics::OpenGL_state_tracker* m_pipeline_state_tracker{nullptr};
    Pointer_context*                      m_pointer_context       {nullptr};
    Scene_builder*                        m_scene_builder         {nullptr};
    Scene_root*                           m_scene_root            {nullptr};
    Shadow_renderer*                      m_shadow_renderer       {nullptr};
    Text_renderer*                        m_text_renderer         {nullptr};
    Viewport_windows*                     m_viewport_windows      {nullptr};

    bool                                  m_trigger_capture{false};
};

}
