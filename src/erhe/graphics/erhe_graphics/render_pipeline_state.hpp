#pragma once

#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/multisample_state.hpp"
#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_graphics/state/scissor_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_profile/profile.hpp"

#include <mutex>
#include <vector>

namespace erhe::graphics {

class Shader_stages;
class Vertex_input_state;

class Render_pipeline_data
{
public:
    const char*                name                {nullptr};
    Shader_stages*             shader_stages       {nullptr};
    const Vertex_input_state*  vertex_input        {nullptr};
    Input_assembly_state       input_assembly      {};
    Multisample_state          multisample         {};
    Viewport_depth_range_state viewport_depth_range{};
    Scissor_state              scissor             {};
    Rasterization_state        rasterization       {};
    Depth_stencil_state        depth_stencil       {};
    Color_blend_state          color_blend         {};
};

class Render_pipeline_state final
{
public:
    Render_pipeline_state();
    Render_pipeline_state(Render_pipeline_data&& create_info);
    ~Render_pipeline_state() noexcept;

    Render_pipeline_state(const Render_pipeline_state& other);
    auto operator=(const Render_pipeline_state& other) -> Render_pipeline_state&;
    Render_pipeline_state(Render_pipeline_state&& old);
    auto operator=(Render_pipeline_state&& old) -> Render_pipeline_state&;

    void reset();

    Render_pipeline_data data;

    static auto get_pipelines() -> std::vector<Render_pipeline_state*>;

    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Render_pipeline_state*>        s_pipelines;
};

} // namespace erhe::graphics
