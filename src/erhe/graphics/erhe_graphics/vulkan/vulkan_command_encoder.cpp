#include "erhe_graphics/vulkan/vulkan_command_encoder.hpp"
#include "erhe_graphics/vulkan/vulkan_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Command_encoder_impl::Command_encoder_impl(Device& device)
    : m_device{device}
{
}

Command_encoder_impl::~Command_encoder_impl()
{
}

void Command_encoder_impl::set_buffer(const Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Command_encoder_impl::set_buffer(Buffer_target buffer_target, const Buffer* buffer)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

} // namespace erhe::graphics
