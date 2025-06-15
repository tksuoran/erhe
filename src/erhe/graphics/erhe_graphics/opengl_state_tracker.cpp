#include "erhe_graphics/opengl_state_tracker.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/framebuffer.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::graphics {

OpenGL_state_tracker::OpenGL_state_tracker() = default;

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

void OpenGL_state_tracker::execute_(const Render_pipeline_state& pipeline, const bool skip_shader_stages)
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
