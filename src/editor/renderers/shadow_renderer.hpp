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

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
    class Mesh_layer;
}

namespace editor
{

class Configuration;
class Mesh_memory;

class Shadow_renderer
    : public erhe::components::Component
    , public Base_renderer
{
public:
    static constexpr std::string_view c_name{"Shadow_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_name.data(), c_name.size(), {});

    Shadow_renderer ();
    ~Shadow_renderer() override;

    // Implements Component
    auto get_type_hash       () const -> uint32_t override { return hash; }
    void connect             () override;
    void initialize_component() override;

    // Public API
    class Render_parameters
    {
    public:
        const std::vector<const erhe::scene::Mesh_layer*> mesh_layers;
        const erhe::scene::Light_layer*                   light_layer;
    };

    void render(const Render_parameters& parameters);

    [[nodiscard]] auto texture () const -> erhe::graphics::Texture*;
    [[nodiscard]] auto viewport() const -> erhe::scene::Viewport;

private:
    // Component dependencies
    std::shared_ptr<Configuration>                        m_configuration;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;

    erhe::graphics::Pipeline                                  m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state>       m_vertex_input;
    std::unique_ptr<erhe::graphics::Texture>                  m_texture;
    std::unique_ptr<erhe::graphics::Gpu_timer>                m_gpu_timer;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
    erhe::scene::Viewport                                     m_viewport{0, 0, 0, 0, true};
    size_t                                                    m_slot{0};
};

} // namespace editor
