
#pragma once

#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/multisample_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_graphics/state/scissor_state.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/gl/gl_shader_stages.hpp"
#include "erhe_graphics/gl/gl_vertex_input_state.hpp"
#include "erhe_profile/profile.hpp"

#include <mutex>

namespace erhe::graphics {

class Render_pipeline_state;
class Compute_pipeline_state;

static constexpr int MAX_ATTRIBUTE_COUNT { 16 }; // TODO(tksuoran@gmail.com): Get rid of this kind of constant?

class Color_blend_state_tracker
{
public:
    void reset  ();
    void execute(const Color_blend_state& state) noexcept;

private:
    Color_blend_state m_cache;
};

class Depth_stencil_state_tracker
{
public:
    void reset  ();
    void execute(const Depth_stencil_state& state);

private:
    static void execute_component(
        Stencil_face_direction  face,
        const Stencil_op_state& state,
        Stencil_op_state&       cache
    );

    static void execute_shared(
        const Stencil_op_state& state,
        Depth_stencil_state&    cache
    );

private:
    Depth_stencil_state m_cache;
};

class Multisample_state_tracker
{
public:
    void reset();
    void execute(const Multisample_state& state);

private:
    Multisample_state m_cache;
};

class Rasterization_state_tracker
{
public:
    void reset  ();
    void execute(const Rasterization_state& state);

private:
    Rasterization_state m_cache;
};

class Scissor_state_tracker
{
public:
    void reset();

    void execute(const Scissor_state& state);

private:
    Scissor_state m_cache;
};

class Vertex_input_state_tracker
{
public:
    void reset  ();
    void execute(const Vertex_input_state* state);

    void set_index_buffer (const Buffer* buffer) const;
    void set_vertex_buffer(std::uintptr_t binding, const Buffer* buffer, std::uintptr_t offset);

private:
    std::vector<Vertex_input_binding> m_bindings;
    unsigned int m_last{0};
};

class Viewport_rect_state_tracker
{
public:
    void reset();

    void execute(const Viewport_rect_state& state);

private:
    Viewport_rect_state m_cache;
};

class Viewport_depth_range_state_tracker
{
public:
    void reset();

    void execute(const Viewport_depth_range_state& state);

private:
    Viewport_depth_range_state m_cache;
};

class OpenGL_state_tracker
{
public:
    OpenGL_state_tracker();
    OpenGL_state_tracker(const OpenGL_state_tracker&) = delete;
    void operator=      (const OpenGL_state_tracker&) = delete;

    void on_thread_exit ();
    void on_thread_enter();
    void reset          ();
    void execute_       (const Render_pipeline_state& pipeline, bool skip_shader_stages = false);
    void execute_       (const Compute_pipeline_state& pipeline);

    Shader_stages_tracker              shader_stages;
    Vertex_input_state_tracker         vertex_input;
    Input_assembly_state_tracker       input_assembly;
    Multisample_state_tracker          multisample;
    Viewport_rect_state_tracker        viewport_rect;
    Viewport_depth_range_state_tracker viewport_depth_range;
    Scissor_state_tracker              scissor;
    Rasterization_state_tracker        rasterization;
    Depth_stencil_state_tracker        depth_stencil;
    Color_blend_state_tracker          color_blend;

    // TODO Tessellation state
};

} // namespace erhe::graphics
