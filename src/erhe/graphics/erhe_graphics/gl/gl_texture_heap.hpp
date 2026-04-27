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
        Device&                    device,
        const Texture&             fallback_texture,
        const Sampler&             fallback_sampler,
        const Bind_group_layout*   bind_group_layout = nullptr
    );
    ~Texture_heap_impl() noexcept;

    void reset_heap       ();
    auto allocate         (const Texture* texture, const Sampler* sample) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sample) -> uint64_t; // bindless ? handle : slot
    auto bind             (Render_command_encoder& encoder) -> std::size_t;
    void unbind           ();

protected:
    Device&                     m_device;
    const Texture&              m_fallback_texture;
    const Sampler&              m_fallback_sampler;
    // Sampler-array path only: first texture unit owned by the heap's
    // material array, derived at construction from the GL Bind_group_layout_impl.
    // Zero on bindless and on layouts with no dedicated combined_image_samplers.
    std::size_t                 m_sampler_array_offset{0};
    std::vector<uint64_t>       m_gl_bindless_texture_handles;
    std::vector<bool>           m_gl_bindless_texture_resident;
    std::vector<const Texture*> m_textures;
    std::vector<const Sampler*> m_samplers;
    std::vector<unsigned int>   m_gl_textures;
    std::vector<unsigned int>   m_gl_samplers;
    std::vector<unsigned int>   m_zero_vector;
    std::size_t                 m_used_slot_count{0};
};

} // namespace erhe::graphics
