#include "erhe_graphics/command_encoder.hpp"
#include "erhe_graphics/gl/gl_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_gl/wrapper_enums.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Command_encoder::Command_encoder(Device& device)
    : m_device{device}
{
}

Command_encoder::~Command_encoder()
{
}

void Command_encoder::set_buffer(const Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Command_encoder::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

} // namespace erhe::graphics
