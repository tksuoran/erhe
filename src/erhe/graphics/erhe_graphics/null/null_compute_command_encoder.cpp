#include "erhe_graphics/null/null_compute_command_encoder.hpp"

namespace erhe::graphics {

Compute_command_encoder_impl::Compute_command_encoder_impl(Device& device)
    : Command_encoder_impl{device}
{
}

Compute_command_encoder_impl::~Compute_command_encoder_impl() noexcept
{
}

void Compute_command_encoder_impl::set_bind_group_layout(const Bind_group_layout* bind_group_layout)
{
    static_cast<void>(bind_group_layout);
}

void Compute_command_encoder_impl::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    static_cast<void>(pipeline);
}

void Compute_command_encoder_impl::dispatch_compute(
    const std::uintptr_t x_size,
    const std::uintptr_t y_size,
    const std::uintptr_t z_size
)
{
    static_cast<void>(x_size);
    static_cast<void>(y_size);
    static_cast<void>(z_size);
}

} // namespace erhe::graphics
