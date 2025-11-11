// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/vulkan/vulkan_texture_heap.hpp"
#include "erhe_graphics/vulkan/vulkan_device.hpp"
#include "erhe_graphics/vulkan/vulkan_sampler.hpp"
#include "erhe_graphics/vulkan/vulkan_texture.hpp"

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
{
    static_cast<void>(reserved_slot_count);
}

Texture_heap_impl::~Texture_heap_impl()
{
}

void Texture_heap_impl::reset()
{
}

auto Texture_heap_impl::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Texture_heap_impl::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    static_cast<void>(slot);
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

auto Texture_heap_impl::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    static_cast<void>(texture);
    static_cast<void>(sampler);
    return 0;
}

void Texture_heap_impl::unbind()
{
}

auto Texture_heap_impl::bind() -> std::size_t
{
    return 0;
}

} // namespace erhe::graphics
