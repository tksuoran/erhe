#pragma once

#include <string_view>

namespace erhe::graphics {

class Command_buffer;

class Scoped_debug_group_impl final
{
public:
    Scoped_debug_group_impl(Command_buffer& command_buffer, std::string_view debug_label);
    ~Scoped_debug_group_impl() noexcept;

    static bool s_enabled; // set by Device_impl during init

private:
};

class Device;

class Scoped_queue_debug_group_impl final
{
public:
    Scoped_queue_debug_group_impl(Device& device, std::string_view debug_label);
    ~Scoped_queue_debug_group_impl() noexcept;

private:
};

} // namespace erhe::graphics
