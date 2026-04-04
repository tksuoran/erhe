#include "erhe_graphics/bind_group_layout.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_bind_group_layout.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_bind_group_layout.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_METAL)
# include "erhe_graphics/metal/metal_bind_group_layout.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_NONE)
# include "erhe_graphics/null/null_bind_group_layout.hpp"
#endif

namespace erhe::graphics {

Bind_group_layout::Bind_group_layout(Device& device, const Bind_group_layout_create_info& create_info)
    : m_impl{std::make_unique<Bind_group_layout_impl>(device, create_info)}
{
}

Bind_group_layout::~Bind_group_layout() noexcept = default;

Bind_group_layout::Bind_group_layout(Bind_group_layout&& other) noexcept = default;
auto Bind_group_layout::operator=(Bind_group_layout&& other) noexcept -> Bind_group_layout& = default;

auto Bind_group_layout::get_impl() -> Bind_group_layout_impl&
{
    return *m_impl.get();
}

auto Bind_group_layout::get_impl() const -> const Bind_group_layout_impl&
{
    return *m_impl.get();
}

auto Bind_group_layout::get_debug_label() const -> erhe::utility::Debug_label
{
    return m_impl->get_debug_label();
}

auto Bind_group_layout::get_sampler_binding_offset() const -> uint32_t
{
    return m_impl->get_sampler_binding_offset();
}

} // namespace erhe::graphics
