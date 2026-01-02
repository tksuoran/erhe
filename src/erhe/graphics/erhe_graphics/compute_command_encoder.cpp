#include "erhe_graphics/compute_command_encoder.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_compute_command_encoder.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp"
#endif

namespace erhe::graphics {

Compute_command_encoder::Compute_command_encoder(Device& device)
    : m_impl{std::make_unique<Compute_command_encoder_impl>(device)}
{
}
Compute_command_encoder::~Compute_command_encoder() noexcept
{
}
void Compute_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    m_impl->set_buffer(buffer_target, buffer, offset, length, index);
}
void Compute_command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    m_impl->set_buffer(buffer_target, buffer);
}
void Compute_command_encoder::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    m_impl->set_compute_pipeline_state(pipeline);
}
void Compute_command_encoder::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    m_impl->dispatch_compute(x_size, y_size, z_size);
}

} // namespace erhe::graphics
