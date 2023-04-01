#include "erhe/graphics/opengl_state_tracker.hpp"
#include "erhe/graphics/framebuffer.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/toolkit/profile.hpp"

namespace erhe::graphics
{

OpenGL_state_tracker* g_opengl_state_tracker{nullptr};

OpenGL_state_tracker::OpenGL_state_tracker()
    : erhe::components::Component{c_type_name}
{
}

OpenGL_state_tracker::~OpenGL_state_tracker() noexcept
{
    ERHE_VERIFY(g_opengl_state_tracker == this);
    g_opengl_state_tracker = nullptr;
}

void OpenGL_state_tracker::initialize_component()
{
    ERHE_VERIFY(g_opengl_state_tracker == nullptr);
    g_opengl_state_tracker = this;
}

void OpenGL_state_tracker::on_thread_exit()
{
    vertex_input .reset();
    shader_stages.reset();

    Framebuffer       ::on_thread_exit();
    Vertex_input_state::on_thread_exit();
    Gpu_timer         ::on_thread_exit();
}

void OpenGL_state_tracker::on_thread_enter()
{
    Framebuffer       ::on_thread_enter();
    Vertex_input_state::on_thread_enter();
    Gpu_timer         ::on_thread_enter();
}

void OpenGL_state_tracker::reset()
{
    ERHE_PROFILE_FUNCTION();

    shader_stages .reset();
    vertex_input  .reset();
    input_assembly.reset();
    // tessellation
    viewport      .reset();
    rasterization .reset();
    // multisample
    depth_stencil .reset();
    color_blend   .reset();
    // dynamic
}

void OpenGL_state_tracker::execute(
    const Pipeline& pipeline,
    const bool      skip_shader_stages
)
{
    ERHE_PROFILE_FUNCTION();

    if (!skip_shader_stages) {
        shader_stages.execute(pipeline.data.shader_stages);
    }
    vertex_input  .execute(pipeline.data.vertex_input);
    input_assembly.execute(pipeline.data.input_assembly);
    // tessellation

    // For now
    //if (pipeline->viewport != nullptr)
    //{
    //    viewport.execute(pipeline->viewport);
    //}

    rasterization .execute(pipeline.data.rasterization);
    // multisample
    depth_stencil .execute(pipeline.data.depth_stencil);
    color_blend   .execute(pipeline.data.color_blend);
    // dynamic
}

} // namespace erhe::graphics
