#pragma once

#include "erhe_utility/debug_label.hpp"

#include "volk.h"

namespace erhe::graphics {

class Command_buffer;
class Command_buffer_impl;
class Device;

class Scoped_debug_group_impl final
{
public:
    Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label);
    ~Scoped_debug_group_impl() noexcept;

    static bool s_enabled; // set by Device_impl during init

private:
    erhe::utility::Debug_label m_debug_label;
    // Impl of the Command_buffer the label region was opened on, null
    // when nothing was opened. The begin/end calls route through the
    // impl so it can drop the end label when the region was already
    // auto-closed by Command_buffer_impl::end() (the XR fan-out ends
    // and submits the rendergraph cb while scopes are still open).
    // Lifetime contract: Scoped_debug_group_impl must not outlive the
    // Command_buffer that produced this pointer; wrappers live in the
    // device's per-(frame-in-flight, thread-slot) pool until that
    // slot's reset, which strictly outlasts any frame-local scope.
    Command_buffer_impl*       m_command_buffer_impl;
};

class Scoped_queue_debug_group_impl final
{
public:
    Scoped_queue_debug_group_impl(Device& device, erhe::utility::Debug_label debug_label);
    ~Scoped_queue_debug_group_impl() noexcept;

private:
    erhe::utility::Debug_label m_debug_label;
    // Graphics queue the begin label was recorded on; VK_NULL_HANDLE
    // when nothing was opened (debug utils disabled / no device).
    VkQueue                    m_queue;
};

} // namespace erhe::graphics
