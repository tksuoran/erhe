#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_texture_heap.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_METAL)
# include "erhe_graphics/metal/metal_texture_heap.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_texture_heap.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_NONE)
# include "erhe_graphics/null/null_texture_heap.hpp"
#endif

namespace erhe::graphics {
    
Texture_heap::Texture_heap(
    Device&                    device,
    const Texture&             fallback_texture,
    const Sampler&             fallback_sampler,
    std::size_t                reserved_slot_count,
    const Bind_group_layout*   bind_group_layout,
    const Shader_resource*     default_uniform_block
)
    : m_impl{device, fallback_texture, fallback_sampler, reserved_slot_count, bind_group_layout, default_uniform_block}
{
    static_assert(sizeof(Texture_heap_impl) <= 512);
    static_assert(alignof(Texture_heap_impl) <= 16);
}

Texture_heap::~Texture_heap() noexcept = default;

void Texture_heap::reset_heap()
{
    m_impl->reset_heap();
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
