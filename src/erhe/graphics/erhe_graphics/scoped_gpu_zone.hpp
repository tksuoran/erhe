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
//  - Metal: a record on the command buffer's GPU timer samples; the zone's
//    GPU times are the boundaries of the encoders it brackets (Apple GPUs
//    sample timestamps only at encoder boundaries, metal_gpu_timer.hpp).
//  - null backend or Tracy disabled: a no-op.
// The name is copied by Tracy, so a temporary or string literal is fine; the
// source location defaults to the construction site.
// The zone ends before its command buffer is submitted (Metal hands the
// command buffer's zone records to the completion handler at submit).
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
