#include "erhe_scene_renderer/texel_renderer.hpp"

#include "erhe_graphics/device.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/scoped_debug_group.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_graphics/texture_heap.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene_renderer/program_interface.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::scene_renderer {

Texel_renderer::Texel_renderer(
    erhe::graphics::Device&         graphics_device,
    erhe::graphics::Command_buffer& init_command_buffer,
    Program_interface&              program_interface
)
    : m_graphics_device  {graphics_device}
    , m_program_interface{program_interface}
    , m_pipeline{
        graphics_device,
        erhe::graphics::Base_render_pipeline_create_info{
            .debug_label    = erhe::utility::Debug_label{"Shadow_debug"},
            .input_assembly = erhe::graphics::Input_assembly_state::triangle,
            .rasterization  = erhe::graphics::Rasterization_state::cull_mode_none,
            // Reverse-Z is static, so the depth state is baked from
            // Device::get_reverse_depth() here; shader stages are supplied per
            // draw via Render_parameters::shader_stages.
            .depth_stencil  = erhe::graphics::Depth_stencil_state::depth_test_enabled_stencil_test_disabled(graphics_device.get_reverse_depth())
        }
    }
    , m_camera_buffer    {graphics_device, program_interface.camera_interface}
    , m_light_buffer     {graphics_device, init_command_buffer, program_interface.light_interface}
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
    , m_dummy_texture{graphics_device.create_dummy_texture(init_command_buffer, erhe::dataformat::Format::format_8_vec4_snorm)}
    , m_texture_heap{
        std::make_unique<erhe::graphics::Texture_heap>(
            m_graphics_device,
            *m_dummy_texture.get(),
            m_fallback_sampler,
            program_interface.bind_group_layout.get()
        )
    }
{
}

Texel_renderer::~Texel_renderer() = default;

void Texel_renderer::render(const Render_parameters& parameters)
{
    ERHE_PROFILE_FUNCTION();

    const auto& viewport = parameters.viewport;
    const auto* camera   = parameters.camera;
    const auto& lights   = parameters.lights;

    erhe::graphics::Scoped_debug_group pass_scope{
        parameters.render_encoder.get_command_buffer(),
        "Texel_renderer::render()"
    };

    // The encoder builds its descriptor set against this layout; it must be set
    // before any buffer / sampler binds below (in particular the shadow sampler
    // bound via Light_buffer::bind_shadow_samplers and the texture heap). Every
    // other renderer does this first; omitting it leaves s_shadow_no_compare
    // unbound, so textureSize() returns 0.
    parameters.render_encoder.set_bind_group_layout(m_program_interface.bind_group_layout.get());

    parameters.render_encoder.set_viewport_rect(viewport.x, viewport.y, viewport.width, viewport.height);

    using Ring_buffer_range = erhe::graphics::Ring_buffer_range;
    std::optional<Ring_buffer_range> camera_buffer_range{};
    ERHE_VERIFY(camera != nullptr);
    camera_buffer_range = m_camera_buffer.update(
        *camera->projection(),
        *camera->get_node(),
        viewport,
        camera->get_exposure(),
        Grid_parameters{}, // unused by texel pass shaders
        Sky_parameters{},  // unused by texel pass shaders
        0, // frame_number -- ignored here
        true, // reverse_depth
        erhe::math::Depth_range::zero_to_one
    );
    m_camera_buffer.bind(parameters.render_encoder, camera_buffer_range.value());

    m_texture_heap->reset_heap(parameters.render_encoder.get_command_buffer());

    Ring_buffer_range light_range = m_light_buffer.update(lights, parameters.light_projections, glm::vec3{0.0f});
    m_light_buffer.bind_light_buffer(parameters.render_encoder, light_range);
    m_light_buffer.bind_shadow_samplers(parameters.render_encoder, parameters.light_projections);

    m_texture_heap->bind(parameters.render_encoder);

    erhe::graphics::Texture* shadowmap_texture = parameters.light_projections->shadow_map_texture.get();
    uint32_t texel_count_x = shadowmap_texture->get_width();
    uint32_t texel_count_y = shadowmap_texture->get_height();

    erhe::graphics::Render_pipeline* render_pipeline = m_pipeline.get_pipeline_for(
        parameters.render_pass.get_descriptor(),
        nullptr, // color_blend (defaults to color_blend_disabled)
        parameters.shader_stages,
        nullptr, // vertex_input  (gl_VertexID-driven, no vertex buffers)
        nullptr  // vertex_format
    );
    if (render_pipeline == nullptr) {
        return;
    }
    parameters.render_encoder.set_render_pipeline(*render_pipeline);

    const std::size_t texel_count  = texel_count_x * texel_count_y;
    const std::size_t vertex_count = texel_count * 6;
    parameters.render_encoder.draw_primitives(m_pipeline.data.input_assembly.primitive_topology, 0, vertex_count);

    camera_buffer_range.value().release();
    light_range.release();
    m_texture_heap->unbind(parameters.render_encoder.get_command_buffer());
}

} // namespace erhe::scene_renderer
