#include "erhe_graphics/surface.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_surface.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_surface.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_surface.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_surface.hpp"
#endif

namespace erhe::graphics {

Surface::Surface(std::unique_ptr<Surface_impl>&& surface_impl)
    : m_impl{std::move(surface_impl)}
{
}

Surface::~Surface() noexcept
{
}

auto Surface::get_swapchain() -> Swapchain*
{
    return m_impl->get_swapchain();
}

auto Surface::get_surface_transform() const -> Surface_transform
{
#if defined(ERHE_GRAPHICS_API_VULKAN)
    return m_impl->get_surface_transform();
#else
    // Only the Vulkan backend tracks a presentation pre-rotation; on every
    // other backend (and on platforms without a rotating display) the surface
    // is presented as-rendered.
    return Surface_transform::identity;
#endif
}

#if defined(ERHE_GRAPHICS_API_VULKAN)
auto Surface::get_color_format() -> erhe::dataformat::Format
{
    return m_impl->get_color_format();
}

auto Surface::get_depth_format() -> erhe::dataformat::Format
{
    return m_impl->get_depth_format();
}
#endif

auto Surface::get_impl() -> Surface_impl&
{
    return *m_impl;
}

auto Surface::get_impl() const -> const Surface_impl&
{
    return *m_impl;
}

} // namespace erhe::graphics
