#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/opengl_state_tracker.hpp"

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
    m_device.opengl_state_tracker.execute_(pipeline);
}

} // namespace erhe::graphics
