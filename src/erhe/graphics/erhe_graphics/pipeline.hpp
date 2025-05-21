#pragma once

#include "erhe_graphics/state/color_blend_state.hpp"
#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_graphics/state/input_assembly_state.hpp"
#include "erhe_graphics/state/multisample_state.hpp"
#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_profile/profile.hpp"

#include <mutex>
#include <vector>

namespace erhe::graphics {

class Shader_stages;
class Vertex_input_state;

class Pipeline_data
{
public:
    const char*               name          {nullptr};
    Shader_stages*            shader_stages {nullptr};
    const Vertex_input_state* vertex_input  {nullptr};
    Input_assembly_state      input_assembly{};
    Multisample_state         multisample   {};
    Rasterization_state       rasterization {};
    Depth_stencil_state       depth_stencil {};
    Color_blend_state         color_blend   {};
};

class Pipeline final
{
public:
    Pipeline();
    Pipeline(Pipeline_data&& create_info);
    ~Pipeline() noexcept;

    Pipeline(const Pipeline& other);
    auto operator=(const Pipeline& other) -> Pipeline&;
    Pipeline(Pipeline&& old);
    auto operator=(Pipeline&& old) -> Pipeline&;

    void reset();

    Pipeline_data data;

    static auto get_pipelines() -> std::vector<Pipeline*>;

    static ERHE_PROFILE_MUTEX_DECLARATION(std::mutex, s_mutex);
    static std::vector<Pipeline*>                     s_pipelines;
};

} // namespace erhe::graphics
