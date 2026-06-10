#pragma once

#include "erhe_utility/pimpl_ptr.hpp"

#include <source_location>

namespace erhe::graphics {

class Command_buffer;
class Scoped_gpu_zone_impl;

// RAII GPU profiling zone (Tracy). Like Scoped_debug_group, the begin / end
// GPU timestamp writes must land on the SAME command buffer as the bracketed
// work, so the constructor takes a Command_buffer:
//  - Vulkan: opens a Tracy zone on the device's TracyVkCtx and the command
//    buffer's VkCommandBuffer (TracyVkZone is explicit about both).
//  - OpenGL: the Tracy GL GPU context is thread-local, so the command buffer
//    argument is unused (the zone still brackets the same GL command stream).
//  - Other backends (Metal, null) or Tracy disabled: a no-op.
// The name is copied by Tracy, so a temporary or string literal is fine; the
// source location defaults to the construction site.
class Scoped_gpu_zone final
{
public:
    Scoped_gpu_zone(
        Command_buffer&            command_buffer,
        const char*                name,
        const std::source_location location = std::source_location::current()
    );
    ~Scoped_gpu_zone() noexcept;

    Scoped_gpu_zone           (const Scoped_gpu_zone&) = delete;
    Scoped_gpu_zone& operator=(const Scoped_gpu_zone&) = delete;
    Scoped_gpu_zone           (Scoped_gpu_zone&&)      = delete;
    Scoped_gpu_zone& operator=(Scoped_gpu_zone&&)      = delete;

private:
    erhe::utility::pimpl_ptr<Scoped_gpu_zone_impl, 128, 16> m_impl;
};

} // namespace erhe::graphics
