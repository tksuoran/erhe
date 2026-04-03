#pragma once

#include "erhe_graphics/texture_heap.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MTL { class ArgumentEncoder; }
namespace MTL { class Buffer; }
namespace MTL { class Texture; }
namespace MTL { class SamplerState; }

namespace erhe::graphics {

class Texture_heap_impl final
{
public:
    Texture_heap_impl(
        Device&                device,
        const Texture&         fallback_texture,
        const Sampler&         fallback_sampler,
        std::size_t            reserved_slot_count,
        const Shader_resource* default_uniform_block
    );
    ~Texture_heap_impl() noexcept;

    auto assign           (std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t;
    void reset_heap       ();
    auto allocate         (const Texture* texture, const Sampler* sampler) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t;
    auto bind             () -> std::size_t;
    void unbind           ();

private:
    void encode_argument_buffer();

    // Slot layout entry: maps a texture heap slot to argument buffer [[id(N)]] values
    struct Slot_layout
    {
        uint32_t texture_arg_index;
        uint32_t sampler_arg_index;
    };

    Device&                          m_device;
    const Texture&                   m_fallback_texture;
    const Sampler&                   m_fallback_sampler;
    std::size_t                      m_reserved_slot_count{0};
    std::size_t                      m_used_slot_count    {0};
    std::vector<const Texture*>      m_textures;
    std::vector<const Sampler*>      m_samplers;
    std::vector<bool>                m_assigned;

    // Argument buffer state -- created once at construction, never changes
    MTL::ArgumentEncoder*            m_argument_encoder{nullptr};
    uint32_t                         m_argument_buffer_index{14}; // [[buffer(N)]]
    std::vector<Slot_layout>         m_slot_layouts; // per-slot [[id(N)]] mapping
    uint32_t                         m_array_base_texture_id{0};
    uint32_t                         m_array_base_sampler_id{0};

    // Per-frame state
    Ring_buffer_range                m_argument_buffer_range;
    bool                             m_dirty{true};
};

} // namespace erhe::graphics
