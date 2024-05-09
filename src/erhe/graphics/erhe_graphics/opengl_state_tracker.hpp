#pragma once

#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/sampler.hpp"

#include <memory>
#include <stack>

namespace erhe::graphics
{

class Pipeline;


class OpenGL_state_tracker
{
public:
    OpenGL_state_tracker();
    OpenGL_state_tracker(const OpenGL_state_tracker&) = delete;
    void operator=      (const OpenGL_state_tracker&) = delete;

    void on_thread_exit ();
    void on_thread_enter();
    void reset          ();
    void execute        (const Pipeline& pipeline, bool skip_shader_stages = false);

    Shader_stages_tracker        shader_stages;
    Vertex_input_state_tracker   vertex_input;
    Input_assembly_state_tracker input_assembly;
    // TODO Tessellation state
    Viewport_state_tracker       viewport;
    Rasterization_state_tracker  rasterization;
    // TODO Multisample state?
    Depth_stencil_state_tracker  depth_stencil;
    Color_blend_state_tracker    color_blend;
    // RODO Dynamic state
};

} // namespace erhe::graphics
