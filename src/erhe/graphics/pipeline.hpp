#pragma once

namespace erhe::graphics
{

class Shader_stages;
class Vertex_input_state;
struct Input_assembly_state;
struct Rasterization_state;
struct Depth_stencil_state;
struct Color_blend_state;
struct Viewport_state;


struct Pipeline
{
    const Shader_stages*        shader_stages {nullptr};
    const Vertex_input_state*   vertex_input  {nullptr};
    const Input_assembly_state* input_assembly{nullptr};
    const Rasterization_state*  rasterization {nullptr};
    const Depth_stencil_state*  depth_stencil {nullptr};
    const Color_blend_state*    color_blend   {nullptr};
    const Viewport_state*       viewport      {nullptr};
};

} // namespace erhe::graphics
