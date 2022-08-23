#pragma once

#include "erhe/components/components.hpp"
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
    static constexpr std::string_view c_type_name{"erhe::graphics::OpenGL_state_tracker"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    OpenGL_state_tracker ();
    ~OpenGL_state_tracker() noexcept override;
    OpenGL_state_tracker (const OpenGL_state_tracker&) = delete;
    void operator=       (const OpenGL_state_tracker&) = delete;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }

    void on_thread_exit () override;
    void on_thread_enter() override;
    void reset          ();
    void execute        (const Pipeline& pipeline);

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
