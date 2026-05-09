#pragma once

#include "erhe_utility/debug_label.hpp"

namespace erhe::graphics {

class Command_buffer;

class Scoped_debug_group_impl final
{
public:
    explicit Scoped_debug_group_impl(erhe::utility::Debug_label debug_label);
    Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label);
    ~Scoped_debug_group_impl() noexcept;

    static bool s_enabled; // set by Device_impl during init

private:
    erhe::utility::Debug_label m_debug_label;
};

} // namespace erhe::graphics
