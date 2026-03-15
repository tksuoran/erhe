#pragma once

#include "erhe_utility/pimpl_ptr.hpp"

#include <memory>

namespace erhe::graphics {

class Device;
class Sampler;
class Texture;

// Unified API for bindless textures and texture unit cache emulating bindless textures
// using sampler arrays. Also candidate for future metal argument buffer / vulkan
// descriptor indexing based implementations
class Texture_heap_impl;
class Texture_heap final
{
public:
    Texture_heap(
        Device&        device,
        const Texture& fallback_texture,
        const Sampler& fallback_sampler,
        std::size_t    reserved_slot_count
    );
    ~Texture_heap() noexcept;

    auto assign           (std::size_t slot, const Texture* texture, const Sampler* sample) -> uint64_t;
    void reset_heap       ();
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t; // bindless ? handle : slot
    auto bind             () -> std::size_t;
    void unbind           ();

private:
    erhe::utility::pimpl_ptr<Texture_heap_impl, 512, 16> m_impl;
};

} // namespace erhe::graphics
