#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/command_buffer.hpp"

namespace erhe::graphics {

Render_command_encoder::Render_command_encoder(Render_pass_descriptor& render_pass_descriptor)
    : m_framebuffer{render_pass_descriptor.framebuffer}
{
}

Render_command_encoder::~Render_command_encoder()
{
}

} // namespace erhe::graphics
