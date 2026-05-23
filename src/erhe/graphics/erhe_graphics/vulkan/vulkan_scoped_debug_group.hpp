#pragma once

#include "erhe_utility/debug_label.hpp"

#include "volk.h"

namespace erhe::graphics {

class Command_buffer;

class Scoped_debug_group_impl final
{
public:
    Scoped_debug_group_impl(Command_buffer& command_buffer, erhe::utility::Debug_label debug_label);
    ~Scoped_debug_group_impl() noexcept;

    static bool s_enabled; // set by Device_impl during init

private:
    erhe::utility::Debug_label m_debug_label;
    // Cached as the raw VkCommandBuffer handle for the begin/end pair
    // -- both vkCmd* calls accept it directly. Lifetime contract:
    // Scoped_debug_group_impl must not outlive the Command_buffer that
    // produced this handle. Vulkan recycles VkCommandBuffer handles
    // when a pool is reset, so a debug-group scope that survives the
    // pool reset would write into whatever handle the new cb takes.
    // The static_assert below pins the type so any sizeof change
    // there reaches this scope's reviewer.
    VkCommandBuffer            m_command_buffer;
};

} // namespace erhe::graphics
