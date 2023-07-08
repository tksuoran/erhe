#pragma once

#include "erhe/graphics/pipeline.hpp"
#include "erhe/graphics/shader_stages.hpp"
#include "erhe/graphics/sampler.hpp"
#include "erhe/graphics/state/vertex_input_state.hpp"
#include "erhe/graphics/gpu_timer.hpp"
#include "erhe/renderer/draw_indirect_buffer.hpp"
#include "erhe/rendergraph/rendergraph_node.hpp"
#include "erhe/toolkit/viewport.hpp"
#include "erhe/scene_renderer/light_buffer.hpp"
#include "erhe/scene_renderer/primitive_buffer.hpp"

#include <gsl/gsl>

#include <initializer_list>

namespace erhe::graphics
{
    class Framebuffer;
    class Gpu_timer;
    class Instance;
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

namespace erhe::scene_renderer
{

class Program_interface;
class Scene_root;
class Scene_view;

class Shadow_renderer
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

    Shadow_renderer(
        erhe::graphics::Instance& graphics_instance,
        Program_interface&        program_interface
    );

    // Public API
    class Render_parameters
    {
    public:
        const erhe::graphics::Vertex_input_state*                  vertex_input_state;
        gl::Draw_elements_type                                     index_type;

        const erhe::scene::Camera*                                 view_camera;
        const erhe::toolkit::Viewport                                view_camera_viewport;
        const erhe::toolkit::Viewport                                light_camera_viewport;
        std::shared_ptr<erhe::graphics::Texture>                   texture;
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
        uint64_t                 serial  {0};
        erhe::graphics::Pipeline pipeline{};
    };

    [[nodiscard]] auto get_pipeline(
        const erhe::graphics::Vertex_input_state* vertex_input_state
    ) -> erhe::graphics::Pipeline&;

    erhe::graphics::Instance&            m_graphics_instance;

    uint64_t                             m_pipeline_cache_serial{0};
    std::vector<Pipeline_cache_entry>    m_pipeline_cache_entries;

    erhe::graphics::Reloadable_shader_stages m_shader_stages;
    erhe::graphics::Sampler                  m_nearest_sampler;
    erhe::graphics::Vertex_input_state       m_vertex_input;
    erhe::graphics::Gpu_timer                m_gpu_timer;
    Light_buffer                             m_light_buffers;
    erhe::renderer::Draw_indirect_buffer     m_draw_indirect_buffers;
    Primitive_buffer                         m_primitive_buffers;
};


} // namespace erhe::scene_renderer
