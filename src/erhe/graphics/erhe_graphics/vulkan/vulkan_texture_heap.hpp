#pragma once

#include "erhe_graphics/texture_heap.hpp"
#include <vector>

namespace erhe::graphics {

// Unified API for bindless textures and texture unit cache emulating bindless textures
// using sampler arrays. Also candidate for future metal argument buffer / vulkan
// descriptor indexing based implementations
class Texture_heap_impl final
{
public:
    Texture_heap_impl(
        Device&        device,
        const Texture& fallback_texture,
        const Sampler& fallback_sampler,
        std::size_t    reserved_slot_count
    );
    ~Texture_heap_impl();

    auto assign           (std::size_t slot, const Texture* texture, const Sampler* sample) -> uint64_t;
    void reset            ();
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t; // bindless ? handle : slot
    auto bind             () -> std::size_t;
    void unbind           ();

protected:
    Device&        m_device;
    const Texture& m_fallback_texture;
    const Sampler& m_fallback_sampler;
};

} // namespace erhe::graphics
