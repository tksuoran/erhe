#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/command_buffer.hpp"

namespace erhe::graphics {

Render_command_encoder::Render_command_encoder(Device& device, const std::shared_ptr<Framebuffer>& framebuffer)
    : m_device     {device}
    , m_framebuffer{framebuffer}
{
}

Render_command_encoder::~Render_command_encoder()
{
}

} // namespace erhe::graphics
