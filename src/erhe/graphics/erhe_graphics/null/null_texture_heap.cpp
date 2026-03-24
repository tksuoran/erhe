#include "erhe_graphics/null/null_texture_heap.hpp"

namespace erhe::graphics {

Texture_heap_impl::Texture_heap_impl(
    Device&        device,
    const Texture& fallback_texture,
    const Sampler& fallback_sampler,
    std::size_t    reserved_slot_count
)
    : m_device             {device}
    , m_fallback_texture   {fallback_texture}
    , m_fallback_sampler   {fallback_sampler}
    , m_reserved_slot_count{reserved_slot_count}
    , m_used_slot_count    {0}
{
}

Texture_heap_impl::~Texture_heap_impl() noexcept = default;

auto Texture_heap_impl::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    static_cast<void>(slot);
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

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

auto Texture_heap_impl::bind() -> std::size_t
{
    return m_used_slot_count;
}

void Texture_heap_impl::unbind()
{
}

} // namespace erhe::graphics
