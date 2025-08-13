#pragma once

#include "erhe_graphics/enums.hpp"

namespace erhe::graphics {

class Command_encoder;
class Device;
class Ring_buffer_range;

class Ring_buffer_client
{
public:
    Ring_buffer_client(
        Device&                     graphics_device,
        Buffer_target               buffer_target,
        std::string_view            debug_label, 
        std::optional<unsigned int> binding_point = {}
    );

    auto acquire(Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range;
    auto bind   (Command_encoder& command_encoder, const Ring_buffer_range& range) -> bool;

protected:
    Device&                     m_graphics_device;

private:
    Buffer_target               m_buffer_target;
    std::string                 m_debug_label;
    std::optional<unsigned int> m_binding_point;
};

} // namespace erhe::graphics
