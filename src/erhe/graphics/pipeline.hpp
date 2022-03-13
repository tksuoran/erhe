#pragma once

#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/graphics/state/input_assembly_state.hpp"
#include "erhe/graphics/state/rasterization_state.hpp"

#include <mutex>

namespace erhe::graphics
{

class Shader_stages;
class Vertex_input_state;

class Pipeline_data
{
public:
    const char*          name          {nullptr};
    Shader_stages*       shader_stages {nullptr};
    Vertex_input_state*  vertex_input  {nullptr};
    Input_assembly_state input_assembly;
    Rasterization_state  rasterization;
    Depth_stencil_state  depth_stencil;
    Color_blend_state    color_blend;
};

class Pipeline final
{
public:
    Pipeline();
    explicit Pipeline(Pipeline_data&& create_info);
    ~Pipeline() noexcept;

    Pipeline(const Pipeline& other);
    auto operator=(const Pipeline& other) -> Pipeline&;
    Pipeline(Pipeline&&)       = delete; // TODO
    auto operator=(Pipeline&&) = delete; // TODO

    Pipeline_data data;

    static auto get_pipelines() -> std::vector<Pipeline*>;
    static std::mutex s_mutex;
    static std::vector<Pipeline*> s_pipelines;
};

} // namespace erhe::graphics
