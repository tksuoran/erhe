#pragma once

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
    class Tools;
    class View;
    class Log_window;
    class Line_renderer_set;
    class Render_context;
    class Text_renderer;
}

namespace editor
{

class Rendering;

class Rendering
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"Rendering"};
    static constexpr uint32_t hash {
        compiletime_xxhash::xxh32(
            c_name.data(),
            c_name.size(),
            {}
        )
    };

    Rendering ();
    ~Rendering() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    void init_state              ();
    void render                  ();
    void bind_default_framebuffer();
    void clear                   ();

private:
    void begin_frame();
    [[nodiscard]] auto width () const -> int;
    [[nodiscard]] auto height() const -> int;

    // Component dependencies
    std::shared_ptr<erhe::application::Application>       m_application;
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::application::Imgui_windows>     m_editor_imgui_windows;
    std::shared_ptr<erhe::application::View>              m_editor_view;
    std::shared_ptr<erhe::application::Line_renderer_set> m_line_renderer_set;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<erhe::application::Text_renderer>     m_text_renderer;

    bool                                                  m_trigger_capture{false};
};

}
