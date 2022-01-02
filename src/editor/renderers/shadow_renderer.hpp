#pragma once

#include "renderers/base_renderer.hpp"

#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/components/component.hpp"

namespace erhe::graphics
{
    class Framebuffer;
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
    void render(
        const Mesh_layer_collection&    mesh_layers,
        const erhe::scene::Light_layer& light_layer
    );

    [[nodiscard]] auto texture () const -> erhe::graphics::Texture*;
    [[nodiscard]] auto viewport() const -> erhe::scene::Viewport;

    static constexpr size_t s_max_light_count    = 6;
    static constexpr size_t s_texture_resolution = 4 * 1024;
    static constexpr bool   s_enable             = true;

private:
    erhe::scene::Viewport                 m_viewport{0, 0, 0, 0, true};

    Configuration*                        m_configuration         {nullptr};
    erhe::graphics::OpenGL_state_tracker* m_pipeline_state_tracker{nullptr};
    Mesh_memory*                          m_mesh_memory           {nullptr};

    erhe::graphics::Pipeline                                  m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state>       m_vertex_input;
    std::unique_ptr<erhe::graphics::Texture>                  m_texture;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
};

} // namespace editor
