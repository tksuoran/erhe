#pragma once

#include "erhe/renderer/base_renderer.hpp"

#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
    class OpenGL_state_tracker;
    class Texture;
    class Vertex_input_state;
}

namespace erhe::renderer
{

class Configuration;
class Mesh_memory;

class Texture_renderer
    : public Base_renderer
{
public:
    Texture_renderer();

    void render(
        const erhe::graphics::Shader_stages* shader_stages,
        erhe::graphics::Texture*             texture
    );

private:
    erhe::graphics::Pipeline           m_pipeline;
    erhe::graphics::Vertex_input_state m_vertex_input;
};

} // namespace erhe::renderer
