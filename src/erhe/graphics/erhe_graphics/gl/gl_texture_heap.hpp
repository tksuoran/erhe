#pragma once

#include "erhe_graphics/texture_heap.hpp"

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
    Device&                     m_device;
    const Texture&              m_fallback_texture;
    const Sampler&              m_fallback_sampler;
    std::size_t                 m_reserved_slot_count;
    std::vector<bool>           m_assigned;
    std::vector<uint64_t>       m_gl_bindless_texture_handles;
    std::vector<bool>           m_gl_bindless_texture_resident;
    std::vector<bool>           m_reserved;
    std::vector<const Texture*> m_textures;
    std::vector<const Sampler*> m_samplers;
    std::vector<unsigned int>   m_gl_textures;
    std::vector<unsigned int>   m_gl_samplers;
    std::vector<unsigned int>   m_zero_vector;
    std::size_t                 m_used_slot_count{0};
};

} // namespace erhe::graphics
