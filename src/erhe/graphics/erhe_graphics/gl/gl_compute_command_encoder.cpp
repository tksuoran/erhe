#include "erhe_graphics/gl/gl_compute_command_encoder.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_state_tracker.hpp"
#include "erhe_graphics/gl/gl_texture.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/texture.hpp"

#include "erhe_gl/gl_helpers.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

#include <optional>

namespace erhe::graphics {

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device, Command_buffer& command_buffer)
    : Command_encoder_impl{device, command_buffer}
{
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
    // No-op: the GL multi-scatter compute path reads the LUT as a storage image
    // (set_storage_image above). Only the Vulkan KosmicKrisp workaround samples
    // the LUT in compute (WORKAROUND_NO_COMPUTE_STORAGE_IMAGE_READ).
    static_cast<void>(binding_point);
    static_cast<void>(texture);
    static_cast<void>(sampler);
}

void Compute_command_encoder_impl::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    m_device.get_impl().m_gl_state_tracker.execute_(pipeline);
}

void Compute_command_encoder_impl::set_compute_pipeline(const Compute_pipeline& pipeline)
{
    // GL uses the pipeline data to set shader program via state tracker
    const Compute_pipeline_data& data = pipeline.get_data();
    Compute_pipeline_state state{Compute_pipeline_data{data}};
    m_device.get_impl().m_gl_state_tracker.execute_(state);
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
