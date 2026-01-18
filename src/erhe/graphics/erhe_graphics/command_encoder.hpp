#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstdint>

namespace erhe::graphics {

class Buffer;
class Device;

class Command_encoder
{
public:
    virtual ~Command_encoder() noexcept;

    virtual void set_buffer(Buffer_target buffer_target, const Buffer* buffer) = 0;
    virtual void set_buffer(Buffer_target buffer_target, const Buffer* buffer, std::uintptr_t offset, std::uintptr_t length, std::uintptr_t index) = 0;
};

} // namespace erhe::graphics
