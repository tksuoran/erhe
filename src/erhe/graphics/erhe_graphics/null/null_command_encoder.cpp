#include "erhe_graphics/null/null_command_encoder.hpp"

namespace erhe::graphics {

Command_encoder_impl::Command_encoder_impl(Device& device)
    : m_device{device}
{
}

Command_encoder_impl::~Command_encoder_impl() noexcept
{
}

void Command_encoder_impl::set_buffer(
    const Buffer_target  buffer_target,
    const Buffer* const  buffer,
    const std::uintptr_t offset,
    const std::uintptr_t length,
    const std::uintptr_t index
)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
    static_cast<void>(offset);
    static_cast<void>(length);
    static_cast<void>(index);
}

void Command_encoder_impl::set_buffer(const Buffer_target buffer_target, const Buffer* const buffer)
{
    static_cast<void>(buffer_target);
    static_cast<void>(buffer);
}

} // namespace erhe::graphics
