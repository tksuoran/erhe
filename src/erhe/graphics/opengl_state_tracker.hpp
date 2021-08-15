#pragma once

#include "erhe/components/component.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/viewport_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/sampler.hpp"
#include <gsl/pointers>
#include <memory>
#include <stack>

namespace erhe::graphics
{

class Pipeline;


class OpenGL_state_tracker
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_name{"erhe::graphics::OpenGL_state_tracker"};
    OpenGL_state_tracker ();
    ~OpenGL_state_tracker() override;
    OpenGL_state_tracker (const OpenGL_state_tracker&) = delete;
    void operator=       (const OpenGL_state_tracker&) = delete;

    void on_thread_exit () override;
    void on_thread_enter() override;
    void reset          ();
    void execute        (gsl::not_null<const Pipeline*> pipeline);

    Shader_stages_tracker        shader_stages;
    Vertex_input_state_tracker   vertex_input;
    Input_assembly_state_tracker input_assembly;
    // tessellation
    Viewport_state_tracker       viewport;
    Rasterization_state_tracker  rasterization;
    // multisample
    Depth_stencil_state_tracker  depth_stencil;
    Color_blend_state_tracker    color_blend;
    // dynamic
};

} // namespace erhe::graphics
