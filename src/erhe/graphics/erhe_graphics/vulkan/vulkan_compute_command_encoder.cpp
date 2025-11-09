#include "erhe_graphics/compute_command_encoder.hpp"

namespace erhe::graphics {

Compute_command_encoder::Compute_command_encoder(Device& device)
    : Command_encoder{device}
{
}

Compute_command_encoder::~Compute_command_encoder()
{
}

void Compute_command_encoder::set_compute_pipeline_state(const Compute_pipeline_state& pipeline)
{
    static_cast<void>(pipeline);
}

void Compute_command_encoder::dispatch_compute(
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
