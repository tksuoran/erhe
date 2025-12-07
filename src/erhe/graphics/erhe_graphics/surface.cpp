#include "erhe_graphics/surface.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_surface.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_surface.hpp"
#endif

namespace erhe::graphics {

Surface::Surface(std::unique_ptr<Surface_impl>&& surface_impl)
    : m_impl{std::move(surface_impl)}
{
}

Surface::~Surface()
{
}

auto Surface::get_swapchain() -> Swapchain*
{
    return m_impl->get_swapchain();
}

void Surface::resize_swapchain_to_surface(uint32_t& width, uint32_t& height)
{
    m_impl->resize_swapchain_to_surface(width, height);
}

auto Surface::get_impl() -> Surface_impl&
{
    return *m_impl;
}

auto Surface::get_impl() const -> const Surface_impl&
{
    return *m_impl;
}

} // namespace erhe::graphics
