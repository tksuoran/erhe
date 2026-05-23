// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/swapchain.hpp"

#if defined(ERHE_GRAPHICS_API_OPENGL)
# include "erhe_graphics/gl/gl_swapchain.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_VULKAN)
# include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_METAL)
# include "erhe_graphics/metal/metal_swapchain.hpp"
#endif
#if defined(ERHE_GRAPHICS_API_NONE)
# include "erhe_graphics/null/null_swapchain.hpp"
#endif

#include "erhe_graphics/device.hpp"

namespace erhe::graphics {
    
Swapchain::Swapchain(std::unique_ptr<Swapchain_impl>&& swapchain_impl)
    : m_impl{std::move(swapchain_impl)}
{
}

Swapchain::~Swapchain() noexcept
{
}

auto Swapchain::has_depth() const -> bool
{
    return m_impl->has_depth();
}
auto Swapchain::has_stencil() const -> bool
{
    return m_impl->has_stencil();
}

auto Swapchain::get_color_format() const -> erhe::dataformat::Format
{
    return m_impl->get_color_format();
}

auto Swapchain::get_depth_format() const -> erhe::dataformat::Format
{
    return m_impl->get_depth_format();
}

auto Swapchain::get_impl() -> Swapchain_impl&
{
    return *m_impl.get();
}

auto Swapchain::get_impl() const -> const Swapchain_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
