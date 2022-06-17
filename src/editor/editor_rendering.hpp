#pragma once

#include "tools/pointer_context.hpp"
#include "renderers/forward_renderer.hpp"
#include "windows/viewport_window.hpp"

#include "erhe/components/components.hpp"
#include "erhe/scene/viewport.hpp"

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
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

namespace erhe::application
{
    class Application;
    class Configuration;
    class Imgui_windows;
    class Time;
    class View;
    class Log_window;
    class Line_renderer_set;
    class Text_renderer;
}

namespace editor
{

class Editor_rendering;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Headset_renderer;
#endif
class Id_renderer;
class Mesh_memory;
class Pointer_context;
class Post_processing;
class Render_context;
class Scene_root;
class Shadow_renderer;
class Tools;
class Viewport_windows;

class Editor_rendering
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Editor_rendering"};
    static constexpr uint32_t hash {
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Editor_rendering ();
    ~Editor_rendering() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    void init_state              ();
    void render                  ();
    void render_viewport         (const Render_context& context, const bool has_pointer);
    void render_content          (const Render_context& context);
    void render_selection        (const Render_context& context);
    void render_tool_meshes      (const Render_context& context);
    //// void render_gui              (const Render_context& context);
    void render_brush            (const Render_context& context);
    void render_id               (const Render_context& context);
    void bind_default_framebuffer();
    void clear                   ();

private:
    void begin_frame();
    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    // Component dependencies
    std::shared_ptr<erhe::application::Application>       m_application;
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows>     m_imgui_windows;
    std::shared_ptr<erhe::application::Time>              m_time;
    std::shared_ptr<erhe::application::View>              m_view;
    std::shared_ptr<erhe::application::Log_window>        m_log_window;
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;

    std::shared_ptr<Tools>                                m_tools;
    std::shared_ptr<Forward_renderer>                     m_forward_renderer;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::shared_ptr<Headset_renderer>                     m_headset_renderer;
#endif
    std::shared_ptr<Id_renderer>                          m_id_renderer;
    std::shared_ptr<Pointer_context>                      m_pointer_context;
    std::shared_ptr<Post_processing>                      m_post_processing;
    std::shared_ptr<Scene_root>                           m_scene_root;
    std::shared_ptr<Shadow_renderer>                      m_shadow_renderer;
    std::shared_ptr<Viewport_windows>                     m_viewport_windows;

    bool                                                  m_trigger_capture{false};

    Renderpass m_rp_polygon_fill_standard;
    Renderpass m_rp_tool1_hidden_stencil;
    Renderpass m_rp_tool2_visible_stencil;
    Renderpass m_rp_tool3_depth_clear;
    Renderpass m_rp_tool4_depth;
    Renderpass m_rp_tool5_visible_color;
    Renderpass m_rp_tool6_hidden_color;
    Renderpass m_rp_line_hidden_blend;
    Renderpass m_rp_brush_back;
    Renderpass m_rp_brush_front;
    Renderpass m_rp_edge_lines;
    Renderpass m_rp_corner_points;
    Renderpass m_rp_polygon_centroids;

    std::unique_ptr<erhe::graphics::Gpu_timer> m_content_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_selection_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_gui_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_brush_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_tools_timer;
};

}
