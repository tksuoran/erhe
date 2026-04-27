#include "erhe_graphics/null/null_texture_heap.hpp"

namespace erhe::graphics {

Texture_heap_impl::Texture_heap_impl(
    Device&                    device,
    const Texture&             fallback_texture,
    const Sampler&             fallback_sampler,
    const Bind_group_layout*   /*bind_group_layout*/
)
    : m_device          {device}
    , m_fallback_texture{fallback_texture}
    , m_fallback_sampler{fallback_sampler}
    , m_used_slot_count {0}
{
}

Texture_heap_impl::~Texture_heap_impl() noexcept = default;

void Texture_heap_impl::reset_heap()
{
    m_used_slot_count = 0;
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Texture_heap_impl::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Texture_heap_impl::bind(Render_command_encoder& /*encoder*/) -> std::size_t
{
    return m_used_slot_count;
}

void Texture_heap_impl::unbind()
{
}

} // namespace erhe::graphics
