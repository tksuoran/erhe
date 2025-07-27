#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_state_tracker.hpp"

#include "erhe_gl/wrapper_functions.hpp"

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
    m_device.get_impl().m_gl_state_tracker.execute_(pipeline);
}

void Compute_command_encoder::dispatch_compute(
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
