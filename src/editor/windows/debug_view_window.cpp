#include "windows/debug_view_window.hpp"
#include "editor_imgui_windows.hpp"

#include "graphics/gl_context_provider.hpp"
#include "renderers/programs.hpp"
#include "renderers/shadow_renderer.hpp"

#include "erhe/graphics/debug.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/texture.hpp"

namespace editor
{

Debug_view_window::Debug_view_window()
    : erhe::components::Component{c_name}
    , Framebuffer_window         {c_name, c_title}
{
}

Debug_view_window::~Debug_view_window() = default;

void Debug_view_window::connect()
{
    m_pipeline_state_tracker = get    <erhe::graphics::OpenGL_state_tracker>();
    m_programs               = require<Programs>();
    m_shadow_renderer        = get    <Shadow_renderer>();

    require<Editor_imgui_windows>();
    require<Gl_context_provider >();
}

void Debug_view_window::initialize_component()
{
    Framebuffer_window::initialize(
        *get<Editor_imgui_windows>().get(),
        get<Gl_context_provider>(),
        get<Programs>()->visualize_depth.get()
    );
}

auto Debug_view_window::get_size() const -> glm::vec2
{
    return glm::vec2{
        static_cast<float>(m_shadow_renderer->texture()->width()),
        static_cast<float>(m_shadow_renderer->texture()->height()),
    };
}

void Debug_view_window::bind_resources()
{
    const unsigned int shadow_texture_unit = 0;
    const unsigned int shadow_texture_name = m_shadow_renderer->texture()->gl_name();
    gl::bind_sampler (shadow_texture_unit, m_programs->nearest_sampler->gl_name());
    gl::bind_textures(shadow_texture_unit, 1, &shadow_texture_name);
}

void Debug_view_window::render(
    erhe::graphics::OpenGL_state_tracker& pipeline_state_tracker
)
{
    if (
        (m_viewport.width < 1) ||
        (m_viewport.height < 1)
    )
    {
        return;
    }

    erhe::graphics::Scoped_debug_group pass_scope{m_debug_label};

    pipeline_state_tracker.execute(m_pipeline);

    bind_resources();
    bind_framebuffer();
    gl::draw_arrays     (m_pipeline.data.input_assembly.primitive_topology, 0, 4);
    gl::bind_framebuffer(gl::Framebuffer_target::draw_framebuffer, 0);
}

} // namespace editor
