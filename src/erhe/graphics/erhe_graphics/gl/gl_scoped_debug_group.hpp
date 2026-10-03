#pragma once

#include <string_view>

namespace erhe::graphics {

class Command_buffer;

class Scoped_debug_group_impl final
{
public:
    Scoped_debug_group_impl(Command_buffer& command_buffer, std::string_view debug_label);
    ~Scoped_debug_group_impl() noexcept;

    static bool s_enabled;              // set by Device_impl during init
    static int  s_max_message_length;   // value of GL_MAX_DEBUG_MESSAGE_LENGTH, queried at init
    static bool s_clamp_to_max_length;  // NVIDIA driver bug workaround; see .cpp

private:
};

class Device;

// GL has no queue concept either; the queue-level scope targets the
// active context exactly like the cb-level scope.
class Scoped_queue_debug_group_impl final
{
public:
    Scoped_queue_debug_group_impl(Device& device, std::string_view debug_label);
    ~Scoped_queue_debug_group_impl() noexcept;

private:
};

} // namespace erhe::graphics
