#pragma once

#include "renderers/renderpass.hpp"

#include "erhe/application/commands/command.hpp"
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
class Forward_renderer;
#if defined(ERHE_XR_LIBRARY_OPENXR)
class Headset_renderer;
#endif
class Id_renderer;
class Mesh_memory;
class Post_processing;
class Render_context;
class Rendertarget_node;
class Scene_root;
class Shadow_renderer;
class Tools;
class Viewport_window;
class Viewport_windows;

class Editor_rendering;

class Capture_frame_command
    : public erhe::application::Command
{
public:
    explicit Capture_frame_command(Editor_rendering& editor_rendering)
        : Command           {"editor.capture_frame"}
        , m_editor_rendering{editor_rendering}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Editor_rendering& m_editor_rendering;
};

class Editor_rendering
    : public erhe::components::Component
    , public erhe::components::IUpdate_once_per_frame
{
public:
    static constexpr std::string_view c_type_name{"Editor_rendering"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Editor_rendering ();
    ~Editor_rendering() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Implements IUpdate_once_per_frame
    void update_once_per_frame(const erhe::components::Time_context&) override;

    // Public API
    void trigger_capture          ();
    void render                   ();
    void render_viewport_main     (const Render_context& context, bool has_pointer);
    void render_viewport_overlay  (const Render_context& context, bool has_pointer);
    void render_content           (const Render_context& context);
    void render_selection         (const Render_context& context);
    void render_tool_meshes       (const Render_context& context);
    void render_rendertarget_nodes(const Render_context& context);
    void render_brush             (const Render_context& context);
    void render_id                (const Render_context& context);

    void begin_frame();
    void end_frame  ();

private:
    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    // Component dependencies
    std::shared_ptr<erhe::application::Application>       m_application;
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows>     m_imgui_windows;
    std::shared_ptr<erhe::application::Log_window>        m_log_window;
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;
    std::shared_ptr<erhe::application::Time>              m_time;
    std::shared_ptr<erhe::application::View>              m_view;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    // Commands
    Capture_frame_command m_capture_frame_command;

    std::shared_ptr<Tools>                          m_tools;
    std::shared_ptr<Forward_renderer>               m_forward_renderer;
#if defined(ERHE_XR_LIBRARY_OPENXR)
    std::shared_ptr<Headset_renderer>               m_headset_renderer;
#endif
    std::shared_ptr<Id_renderer>                    m_id_renderer;
    std::shared_ptr<Post_processing>                m_post_processing;
    std::shared_ptr<Shadow_renderer>                m_shadow_renderer;
    std::shared_ptr<Viewport_windows>               m_viewport_windows;

    bool                                            m_trigger_capture{false};

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
    Renderpass m_rp_rendertarget_nodes;

    std::unique_ptr<erhe::graphics::Gpu_timer> m_content_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_selection_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_gui_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_brush_timer;
    std::unique_ptr<erhe::graphics::Gpu_timer> m_tools_timer;
};

}
