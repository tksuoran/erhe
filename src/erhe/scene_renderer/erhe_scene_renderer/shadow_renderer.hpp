#pragma once

#include "erhe_graphics/render_pipeline_state.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/gpu_timer.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_renderer/draw_indirect_buffer.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_scene_renderer/joint_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/material_buffer.hpp"
#include "erhe_scene_renderer/primitive_buffer.hpp"

#include <initializer_list>

namespace erhe::graphics {
    class Render_pass;
    class Gpu_timer;
    class Device;
    class Sampler;
    class Texture;
    class Vertex_input_state;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
}

namespace erhe::scene_renderer {

class Program_interface;
class Scene_root;
class Scene_view;
class Settings;

class Shadow_renderer
{
public:
    static const int shadow_texture_unit_compare{0};
    static const int shadow_texture_unit_no_compare{1};

    Shadow_renderer(erhe::graphics::Device& graphics_device, Program_interface& program_interface);

    // Public API
    class Render_parameters
    {
    public:
        const erhe::graphics::Vertex_input_state*                  vertex_input_state  {nullptr};
        erhe::dataformat::Format                                   index_type          {erhe::dataformat::Format::format_undefined};
        erhe::graphics::Buffer*                                    index_buffer        {nullptr};
        erhe::graphics::Buffer*                                    vertex_buffer       {nullptr};
        std::size_t                                                vertex_buffer_offset{0};

        const erhe::scene::Camera*                                 view_camera;
        const erhe::math::Viewport                                 view_camera_viewport;
        const erhe::math::Viewport                                 light_camera_viewport;
        std::shared_ptr<erhe::graphics::Texture>                   texture;
        const std::vector<
            std::unique_ptr<erhe::graphics::Render_pass>
        >&                                                         render_passes;
        const std::initializer_list<
            const std::span<
                const std::shared_ptr<erhe::scene::Mesh>
            >
        >&                                                                 mesh_spans;
        const std::span<const std::shared_ptr<erhe::scene::Light>>         lights;
        const std::span<const std::shared_ptr<erhe::scene::Skin>>&         skins{};
        const std::span<const std::shared_ptr<erhe::primitive::Material>>& materials{};
        Light_projections&                                                 light_projections;
    };

    auto render(const Render_parameters& parameters) -> bool;

private:
    class Pipeline_cache_entry
    {
    public:
        uint64_t                              serial  {0};
        erhe::graphics::Render_pipeline_state pipeline{};
    };

    [[nodiscard]] auto get_pipeline(const erhe::graphics::Vertex_input_state* vertex_input_state) -> erhe::graphics::Render_pipeline_state&;

    erhe::graphics::Device&                  m_graphics_device;
    uint64_t                                 m_pipeline_cache_serial{0};
    std::vector<Pipeline_cache_entry>        m_pipeline_cache_entries;
    erhe::graphics::Reloadable_shader_stages m_shader_stages;
    erhe::graphics::Sampler                  m_shadow_sampler_compare;
    erhe::graphics::Sampler                  m_shadow_sampler_no_compare;

    erhe::graphics::Vertex_input_state       m_vertex_input;
    erhe::renderer::Draw_indirect_buffer     m_draw_indirect_buffer;
    Joint_buffer                             m_joint_buffer;
    Light_buffer                             m_light_buffer;
    Primitive_buffer                         m_primitive_buffer;
    Material_buffer                          m_material_buffer;
    erhe::graphics::Gpu_timer                m_gpu_timer;
};


} // namespace erhe::scene_renderer
