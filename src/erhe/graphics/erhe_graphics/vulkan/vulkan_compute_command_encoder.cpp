#include "erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device)
    : m_device{device}
{
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
}

void Compute_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Compute_command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

void Compute_command_encoder_impl::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(pipeline);
}

void Compute_command_encoder_impl::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    ERHE_FATAL("Not implemented");
    static_cast<void>(x_size);
    static_cast<void>(y_size);
    static_cast<void>(z_size);
}

} // namespace erhe::graphics
