#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstdint>

namespace erhe::graphics {

class Buffer;
class Device;

class Command_encoder
{
public:
    explicit Command_encoder(Device& device);
    Command_encoder(const Command_encoder&) = delete;
    Command_encoder& operator=(const Command_encoder&) = delete;
    Command_encoder(Command_encoder&&) = delete;
    Command_encoder& operator=(Command_encoder&&) = delete;
    virtual ~Command_encoder();

    void set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index);
    void set_buffer(Buffer_target buffer_target, const Buffer* buffer);

protected:
    Device& m_device;
};

} // namespace erhe::graphics
