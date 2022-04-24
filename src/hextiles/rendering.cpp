#include "rendering.hpp"

#include "log.hpp"

#include "hextiles/map_window.hpp"
#include "hextiles/map_renderer.hpp"


#include "erhe/application/application.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/view.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/window.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/windows/log_window.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor {

Rendering::Rendering()
    : erhe::components::Component{c_label}
{
}

Rendering::~Rendering()
{
}

void Rendering::connect()
{
    m_configuration        = get<erhe::application::Configuration>();
    m_editor_imgui_windows = get<erhe::application::Imgui_windows>();
    m_editor_view          = get<erhe::application::View         >();
    m_line_renderer_set    = get<erhe::application::Line_renderer_set>();
    m_text_renderer        = get<erhe::application::Text_renderer    >();

    require<erhe::application::Gl_context_provider>();
}

void Rendering::initialize_component()
{
    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };
}

void Rendering::init_state()
{
    gl::clip_control(gl::Clip_control_origin::lower_left, gl::Clip_control_depth::zero_to_one);
    gl::disable     (gl::Enable_cap::primitive_restart);
    gl::enable      (gl::Enable_cap::primitive_restart_fixed_index);
    gl::enable      (gl::Enable_cap::texture_cube_map_seamless);
    gl::enable      (gl::Enable_cap::framebuffer_srgb);
}

auto Rendering::width() const -> int
{
    return m_editor_view->width();
}

auto Rendering::height() const -> int
{
    return m_editor_view->height();
}

void Rendering::begin_frame()
{
}

void Rendering::bind_default_framebuffer()
{
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
    gl::viewport        (0, 0, width(), height());
}

void Rendering::clear()
{
    ERHE_PROFILE_FUNCTION

    Expects(m_pipeline_state_tracker != nullptr);

    // Pipeline state required for NVIDIA driver not to complain about texture
    // unit state when doing the clear.
    m_pipeline_state_tracker->shader_stages.reset();
    m_pipeline_state_tracker->color_blend.execute(erhe::graphics::Color_blend_state::color_blend_disabled);
    gl::clear_color  (0.0f, 0.0f, 0.2f, 0.1f);
    gl::clear_depth_f(*m_configuration->depth_clear_value_pointer());
    gl::clear        (gl::Clear_buffer_mask::color_buffer_bit | gl::Clear_buffer_mask::depth_buffer_bit);
}

void Rendering::render()
{
    ERHE_PROFILE_FUNCTION

    Expects(m_editor_view);

    if (m_trigger_capture)
    {
        get<erhe::application::Window>()->begin_renderdoc_capture();
    }

    begin_frame();

    const auto& map_window = get<hextiles::Map_window>();
    if (map_window)
    {
        map_window->render();
    }

    m_editor_imgui_windows->imgui_windows();

    if (m_line_renderer_set) m_line_renderer_set->next_frame();
    if (m_text_renderer    ) m_text_renderer    ->next_frame();

    const auto& map_renderer = get<hextiles::Map_renderer>();
    if (map_renderer)
    {
        map_renderer->next_frame();
    }
}

}  // namespace editor
