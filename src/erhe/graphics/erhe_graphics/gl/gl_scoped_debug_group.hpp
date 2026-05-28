#pragma once

#include "erhe_utility/debug_label.hpp"

namespace erhe::graphics {

class Command_buffer;

class Scoped_debug_group_impl final
{
public:
    Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label);
    ~Scoped_debug_group_impl() noexcept;

    static bool s_enabled;              // set by Device_impl during init
    static int  s_max_message_length;   // value of GL_MAX_DEBUG_MESSAGE_LENGTH, queried at init
    static bool s_clamp_to_max_length;  // NVIDIA driver bug workaround; see .cpp

private:
    erhe::utility::Debug_label m_debug_label;
};

} // namespace erhe::graphics
