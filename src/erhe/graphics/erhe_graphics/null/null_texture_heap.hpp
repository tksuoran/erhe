#pragma once

#include "erhe_graphics/texture_heap.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace erhe::graphics {

class Texture_heap_impl final
{
public:
    Texture_heap_impl(
        Device&                    device,
        const Texture&             fallback_texture,
        const Sampler&             fallback_sampler,
        std::size_t                reserved_slot_count,
        const Bind_group_layout*   bind_group_layout = nullptr,
        const Shader_resource*     default_uniform_block = nullptr
    );
    ~Texture_heap_impl() noexcept;

    auto assign           (std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t;
    void reset_heap       ();
    auto allocate         (const Texture* texture, const Sampler* sampler) -> uint64_t;
    auto get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t;
    auto bind             () -> std::size_t;
    void unbind           ();

protected:
    Device&        m_device;
    const Texture& m_fallback_texture;
    const Sampler& m_fallback_sampler;
    std::size_t    m_reserved_slot_count{0};
    std::size_t    m_used_slot_count    {0};
};

} // namespace erhe::graphics
