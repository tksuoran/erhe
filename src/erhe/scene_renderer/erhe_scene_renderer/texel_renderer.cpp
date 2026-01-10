#include "erhe_scene_renderer/texel_renderer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Texel_renderer::Texel_renderer(erhe::graphics::Device& graphics_device, Program_interface& program_interface)
    : m_graphics_device  {graphics_device}
    , m_program_interface{program_interface}
    , m_camera_buffer    {graphics_device, program_interface.camera_interface}
    , m_light_buffer     {graphics_device, program_interface.light_interface}
    , m_fallback_sampler{
        graphics_device,
        erhe::graphics::Sampler_create_info{
            .min_filter        = erhe::graphics::Filter::nearest,
            .mag_filter        = erhe::graphics::Filter::nearest,
            .mipmap_mode       = erhe::graphics::Sampler_mipmap_mode::not_mipmapped,
            .address_mode      = { erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge, erhe::graphics::Sampler_address_mode::clamp_to_edge },
            .compare_enable    = false,
            .compare_operation = erhe::graphics::Compare_operation::always,
            .debug_label       = "Texel_renderer::m_fallback_sampler"
        }
    }
    , m_dummy_texture{graphics_device.create_dummy_texture(erhe::dataformat::Format::format_8_vec4_snorm)}
{
}

void Texel_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    const auto& viewport = parameters.viewport;
    const auto* camera   = parameters.camera;
    const auto& lights   = parameters.lights;

    erhe::graphics::Scoped_debug_group pass_scope{"Texel_renderer::render()"};

    parameters.render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;
    std::optional<Ring_buffer_range> camera_buffer_range{};
    ERHE_VERIFY(camera != nullptr);
    camera_buffer_range = m_camera_buffer.update(
        *camera->projection(),
        *camera->get_node(),
        viewport,
        camera->get_exposure(),
        glm::vec4{0.0f},
        glm::vec4{0.0f},
        0 // frame_number -- ignored here
    );
    m_camera_buffer.bind(parameters.render_encoder, camera_buffer_range.value());

    erhe::graphics::Texture_heap texture_heap{
        m_graphics_device,
        *m_dummy_texture.get(),
        m_fallback_sampler,
        erhe::scene_renderer::c_texture_heap_slot_count_reserved
    };

    Ring_buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, glm::vec3{0.0f}, texture_heap);
    m_light_buffer.bind_light_buffer(parameters.render_encoder, light_range);

    texture_heap.bind();

    const erhe::graphics::Render_pipeline_state& pipeline = parameters.pipeline;

    erhe::graphics::Texture* shadowmap_texture = parameters.light_projections->shadow_map_texture.get();
    uint32_t texel_count_x = shadowmap_texture->get_width();
    uint32_t texel_count_y = shadowmap_texture->get_height();

    parameters.render_encoder.set_render_pipeline_state(pipeline);

    const std::size_t texel_count  = texel_count_x * texel_count_y;
    const std::size_t vertex_count = texel_count * 6;
    parameters.render_encoder.draw_primitives(pipeline.data.input_assembly.primitive_topology, 0, vertex_count);

    camera_buffer_range.value().release();
    light_range.release();
    texture_heap.unbind();
}

} // namespace erhe::scene_renderer
