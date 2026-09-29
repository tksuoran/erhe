#include "erhe_graphics/gl/gl_compute_command_encoder.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_sampler.hpp"
#include "erhe_graphics/gl/gl_state_tracker.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <optional>

namespace erhe::graphics {

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : Command_encoder_impl{device, command_buffer}
    , m_tracker{device.get_impl().get_state_tracker()}
{
    ERHE_VERIFY_GL_THREAD_MAIN_CONTEXT();
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
}

void Compute_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    static_cast<void>(bind_group_layout);
}

void Compute_command_encoder_impl::set_storage_image(uint32_t binding_point, const Texture& texture)
{
    // Bind the texture's level 0 as a load/store image at image unit
    // binding_point, matching the shader's layout(binding = binding_point)
    // image2D declaration injected by the bind group layout. read_write is
    // always safe for both LUT passes (transmittance writes; multi-scatter
    // reads unit 0 and writes unit 1).
    const Texture_impl& texture_impl = texture.get_impl();
    // Publication consumer: see Render_command_encoder_impl::set_sampled_image.
    texture_impl.wait_publication();
    const std::optional<gl::Internal_format> internal_format_opt =
        gl_helpers::convert_to_gl(texture_impl.get_pixelformat());
    ERHE_VERIFY(internal_format_opt.has_value());
    gl::bind_image_texture(
        binding_point,                 // unit
        texture_impl.gl_name(),        // texture
        0,                             // level
        GL_FALSE,                      // layered (single-layer 2D)
        0,                             // layer
        gl::Buffer_access::read_write, // access
        internal_format_opt.value()    // format
    );
}

void Compute_command_encoder_impl::set_sampled_image(uint32_t binding_point, const Texture& texture, const Sampler& sampler)
{
    // Dedicated samplers use binding_point as the texture unit, as in
    // Render_command_encoder_impl::set_sampled_image.
    // Publication consumer: see Render_command_encoder_impl::set_sampled_image.
    texture.get_impl().wait_publication();
    gl::bind_texture_unit(binding_point, texture.get_impl().gl_name());
    gl::bind_sampler(binding_point, sampler.get_impl().gl_name());
}

void Compute_command_encoder_impl::set_acceleration_structure(uint32_t binding_point, const Acceleration_structure& acceleration_structure)
{
    // No-op: the GL backend has no GPU ray tracing support
    // (Device_info::use_ray_query is false).
    static_cast<void>(binding_point);
    static_cast<void>(acceleration_structure);
}

void Compute_command_encoder_impl::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    m_tracker.execute_(pipeline);
}

void Compute_command_encoder_impl::set_compute_pipeline(const Compute_pipeline& pipeline)
{
    // GL uses the pipeline data to set shader program via state tracker
    const Compute_pipeline_data& data = pipeline.get_data();
    Compute_pipeline_state state{Compute_pipeline_data{data}};
    m_tracker.execute_(state);
}

void Compute_command_encoder_impl::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    gl::dispatch_compute(
        static_cast<unsigned int>(x_size),
        static_cast<unsigned int>(y_size),
        static_cast<unsigned int>(z_size)
    );
}

} // namespace erhe::graphics
