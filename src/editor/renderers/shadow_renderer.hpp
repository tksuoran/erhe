#pragma once

#include "renderers/light_buffer.hpp"
#include "renderers/draw_indirect_buffer.hpp"
#include "renderers/primitive_buffer.hpp"

#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"
#include "erhe/components/components.hpp"

#include <gsl/gsl>

#include <initializer_list>

namespace erhe::application
{
    class Configuration;
}

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

class Mesh_memory;

class Shadow_renderer
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Shadow_renderer"};
    static constexpr uint32_t hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Shadow_renderer ();
    ~Shadow_renderer() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    class Render_parameters
    {
    public:
        const erhe::scene::Camera&                                 view_camera;
        const erhe::scene::Viewport                                view_camera_viewport;
        const std::initializer_list<
            const gsl::span<
                const std::shared_ptr<erhe::scene::Mesh>
            >
        >&                                                         mesh_spans;
        const gsl::span<const std::shared_ptr<erhe::scene::Light>> lights;
    };

    auto light_projections() const -> const Light_projections&;

    auto render(const Render_parameters& parameters) -> const Light_projections&;

    void next_frame();

    [[nodiscard]] auto texture () const -> erhe::graphics::Texture*;
    [[nodiscard]] auto viewport() const -> erhe::scene::Viewport;

private:
    // Component dependencies
    std::shared_ptr<erhe::application::Configuration>     m_configuration;
    std::shared_ptr<erhe::graphics::OpenGL_state_tracker> m_pipeline_state_tracker;
    std::shared_ptr<Mesh_memory>                          m_mesh_memory;

    erhe::graphics::Pipeline                                  m_pipeline;
    std::unique_ptr<erhe::graphics::Vertex_input_state>       m_vertex_input;
    std::unique_ptr<erhe::graphics::Texture>                  m_texture;
    std::unique_ptr<erhe::graphics::Gpu_timer>                m_gpu_timer;
    std::vector<std::unique_ptr<erhe::graphics::Framebuffer>> m_framebuffers;
    erhe::scene::Viewport                                     m_viewport{0, 0, 0, 0, true};
    Light_projections                                         m_light_projections;

    std::unique_ptr<Light_buffer        > m_light_buffers;
    std::unique_ptr<Draw_indirect_buffer> m_draw_indirect_buffers;
    std::unique_ptr<Primitive_buffer    > m_primitive_buffers;
};

} // namespace editor
