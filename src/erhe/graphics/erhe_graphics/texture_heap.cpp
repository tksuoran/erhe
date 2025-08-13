// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/texture_heap.hpp"
#include "erhe_graphics/gl/gl_texture_heap.hpp"

namespace erhe::graphics {
    
Texture_heap::Texture_heap(
    Device&        device,
    const Texture& fallback_texture,
    const Sampler& fallback_sampler,
    std::size_t    reserved_slot_count
)
    : m_impl{std::make_unique<Texture_heap_impl>(device, fallback_texture, fallback_sampler, reserved_slot_count)}
{
}

Texture_heap::~Texture_heap() = default;

void Texture_heap::reset()
{
    m_impl->reset();
}

auto Texture_heap::get_shader_handle(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    return m_impl->get_shader_handle(texture, sampler);
}

auto Texture_heap::assign(std::size_t slot, const Texture* texture, const Sampler* sampler) -> uint64_t
{
    return m_impl->assign(slot, texture, sampler);
}

auto Texture_heap::allocate(const Texture* texture, const Sampler* sampler) -> uint64_t
{
    return m_impl->allocate(texture, sampler);
}

// TODO Maybe this should use Render_command_encoder?
void Texture_heap::unbind()
{
    m_impl->unbind();
}

// TODO Maybe this should use Render_command_encoder?
auto Texture_heap::bind() -> std::size_t
{
    return m_impl->bind();
}


} // namespace erhe::graphics
