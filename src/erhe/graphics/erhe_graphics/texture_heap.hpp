#pragma once

#include "erhe_utility/pimpl_ptr.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace erhe::graphics {

class Bind_group_layout;
class Command_buffer;
class Compute_command_encoder;
class Device;
class Render_command_encoder;
class Sampler;
class Shader_resource;
class Texture;

// Requested descriptor count of the combined-image-sampler array every texture
// heap binds into (Vulkan: set 1, built in vulkan_device_init.cpp). The device
// bounds it by its descriptor limits (Vulkan: maxPerSetDescriptors; MoltenVK
// reports 1212) into Device_info::texture_heap_size, the upper bound on any
// one heap's max_textures, because a heap allocates a variable descriptor
// count out of that layout. Desktop: sized for texture-heavy scenes
// (niagara_bistro references 343 material textures) and well below the
// update-after-bind minimum limits (500k). Android: smaller, for the mobile
// drivers' per-set descriptor memory.
#if defined(ERHE_OS_ANDROID)
inline constexpr uint32_t max_texture_heap_size = 1024;
#else
inline constexpr uint32_t max_texture_heap_size = 4096;
#endif

// Unified API for bindless textures and texture unit cache emulating bindless textures
// using sampler arrays. Also candidate for future metal argument buffer / vulkan
// descriptor indexing based implementations
class Texture_heap_impl;
class Texture_heap final
{
public:
    Texture_heap(
        Device&                  device,
        const Texture&           fallback_texture,
        const Sampler&           fallback_sampler,
        const Bind_group_layout* bind_group_layout = nullptr,
        // Upper bound on how many distinct texture/sampler pairs this heap can
        // hold. Meaningful on Vulkan, where it sizes the variable descriptor
        // count each of this heap's descriptor sets is allocated with, and is
        // capped to Device_info::texture_heap_size, the descriptor count of
        // the set-1 pipeline layout. It does
        // NOT size the descriptor set LAYOUT: that has to stay identically
        // defined to the one in the pipeline layout or the set cannot be bound
        // at all. The OpenGL sampler-array path sizes itself from the device's
        // per-stage sampler limit and the bindless path grows on demand, so
        // there, as on Metal and null, it is accepted and ignored. Defaulted so
        // heaps that do not care keep their current calls.
        std::size_t              max_textures = max_texture_heap_size
    );
    ~Texture_heap() noexcept;

    // Command_buffer is the cb that bracketing Scoped_debug_groups
    // record into. On Vulkan the cb is load-bearing (cb-recorded
    // vkCmdBeginDebugUtilsLabelEXT). On GL the cb is a stub passed only
    // so the GL scoped debug groups around bindless make-resident /
    // make-non-resident sequences continue to bracket the same code
    // they always have.
    void reset_heap       (Command_buffer& command_buffer);
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t; // bindless ? handle : slot
    auto bind             (Render_command_encoder& encoder) -> std::size_t;
    // Compute variant: binds the heap for compute dispatches (Vulkan binds
    // descriptor set 1 at the compute pipeline bind point). Used by the ray
    // trace compute shader to sample material textures at hit points.
    auto bind             (Compute_command_encoder& encoder) -> std::size_t;
    void unbind           (Command_buffer& command_buffer);

private:
    erhe::utility::pimpl_ptr<Texture_heap_impl, 512, 16> m_impl;
};

} // namespace erhe::graphics
