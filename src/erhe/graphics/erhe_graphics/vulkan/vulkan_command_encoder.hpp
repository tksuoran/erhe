#pragma once

#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

class Command_buffer;

class Command_encoder_impl
{
public:
    Command_encoder_impl(Device& device, Command_buffer& command_buffer);
    ~Command_encoder_impl() noexcept;

    void set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer(Buffer_target buffer_target, const Buffer* buffer);

    [[nodiscard]] auto get_command_buffer() -> Command_buffer& { return m_command_buffer; }

protected:
    Device&         m_device;
    Command_buffer& m_command_buffer;
};

} // namespace erhe::graphics