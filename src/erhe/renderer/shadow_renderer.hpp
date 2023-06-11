#pragma once

#include "erhe/renderer/light_buffer.hpp"
#include "erhe/renderer/draw_indirect_buffer.hpp"
#include "erhe/renderer/primitive_buffer.hpp"

#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/components/components.hpp"
#include "erhe/graphics/pipeline.hpp"
#include "erhe/scene/viewport.hpp"

#include <gsl/gsl>

#include <initializer_list>

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
    class Sampler;
    class Texture;
    class Vertex_input_state;
}

namespace erhe::scene
{
    class Camera;
    class Light;
    class Mesh;
}

namespace erhe::renderer
{

class Scene_root;
class Scene_view;
//class Shadow_render_node;

class Shadow_renderer
    : public erhe::components::Component
{
public:
    static const int shadow_texture_unit{15};

    class Config
    {
    public:
        bool enabled                   {true};
        bool tight_frustum_fit         {true};
        int  shadow_map_resolution     {2048};
        int  shadow_map_max_light_count{8};
    };
    Config config;

    static constexpr std::string_view c_type_name{"Shadow_renderer"};
    static constexpr uint32_t c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Shadow_renderer ();
    ~Shadow_renderer() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void deinitialize_component     () override;

    // Public API
    class Render_parameters
    {
    public:
        const erhe::graphics::Vertex_input_state*                  vertex_input_state;
        gl::Draw_elements_type                                     index_type;

        const erhe::scene::Camera*                                 view_camera;
        const erhe::scene::Viewport                                view_camera_viewport;
        const erhe::scene::Viewport                                light_camera_viewport;
        erhe::graphics::Texture&                                   texture;
        const std::vector<
            std::unique_ptr<erhe::graphics::Framebuffer>
        >&                                                         framebuffers;
        const std::initializer_list<
            const gsl::span<
                const std::shared_ptr<erhe::scene::Mesh>
            >
        >&                                                         mesh_spans;
        const gsl::span<const std::shared_ptr<erhe::scene::Light>> lights;
        Light_projections&                                         light_projections;
    };

    auto render    (const Render_parameters& parameters) -> bool;
    void next_frame();

private:
    class Pipeline_cache_entry
    {
    public:
        uint64_t                            serial            {0};
        erhe::graphics::Vertex_input_state* vertex_input_state{nullptr};
        erhe::graphics::Pipeline            pipeline;
    };

    [[nodiscard]] auto get_pipeline(
        const erhe::graphics::Vertex_input_state* vertex_input_state
    ) -> erhe::graphics::Pipeline&;

    uint64_t                          m_pipeline_cache_serial{0};
    std::vector<Pipeline_cache_entry> m_pipeline_cache_entries;

    std::unique_ptr<erhe::graphics::Vertex_input_state> m_vertex_input;
    std::unique_ptr<erhe::graphics::Gpu_timer>          m_gpu_timer;
    std::unique_ptr<Light_buffer        >               m_light_buffers;
    std::unique_ptr<Draw_indirect_buffer>               m_draw_indirect_buffers;
    std::unique_ptr<Primitive_buffer    >               m_primitive_buffers;

    std::unique_ptr<erhe::graphics::Shader_stages> m_shader_stages;
    std::unique_ptr<erhe::graphics::Sampler>       m_nearest_sampler;
};

extern Shadow_renderer* g_shadow_renderer;

} // namespace erhe::renderer
