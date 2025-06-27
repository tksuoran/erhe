#pragma once

#include "erhe_renderer/base_renderer.hpp"

#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_scene/viewport.hpp"

namespace erhe::graphics
{
    class Render_pass;
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
    erhe::graphics::Render_pipeline_state m_pipeline;
    erhe::graphics::Vertex_input_state    m_vertex_input;
};

} // namespace erhe::renderer
