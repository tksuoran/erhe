#pragma once

#include "renderers/base_renderer.hpp"

#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/components/components.hpp"

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
    class OpenGL_state_tracker;
    class Texture;
    class Vertex_input_state;
}

namespace editor
{

class Configuration;
class Mesh_memory;

class Texture_renderer
    : public erhe::components::Component
    , public Base_renderer
{
public:
    static constexpr std::string_view c_name{"Texture_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Texture_renderer ();
    ~Texture_renderer() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    void render(
        const erhe::graphics::Shader_stages* shader_stages,
        erhe::graphics::Texture*             texture
    );

private:
    // Component dependencies
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;

    erhe::graphics::Pipeline                            m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
};

} // namespace editor
