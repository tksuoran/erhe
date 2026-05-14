#pragma once

#include "erhe_utility/pimpl_ptr.hpp"

#include <memory>

namespace erhe::graphics {

class Bind_group_layout;
class Command_buffer;
class Device;
class Render_command_encoder;
class Sampler;
class Shader_resource;
class Texture;

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
        const Bind_group_layout* bind_group_layout = nullptr
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
    void unbind           (Command_buffer& command_buffer);

private:
    erhe::utility::pimpl_ptr<Texture_heap_impl, 512, 16> m_impl;
};

} // namespace erhe::graphics
