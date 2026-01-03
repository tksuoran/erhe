#pragma once

#include "erhe_graphics/device.hpp"

namespace erhe::graphics {

class Command_encoder_impl
{
public:
    explicit Command_encoder_impl(Device& device);
    ~Command_encoder_impl() noexcept;

    void set_buffer(const Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer(Buffer_target buffer_target, const Buffer* buffer);

protected:
    Device& m_device;
};

} // namespace erhe::graphics