// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/swapchain.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_swapchain.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_swapchain.hpp"
#endif

#include "erhe_graphics/device.hpp"

namespace erhe::graphics {
    

Swapchain::Swapchain(Device& device, const Swapchain_create_info& create_info)
    : m_impl{std::make_unique<Swapchain_impl>(device, create_info)}
{
}

Swapchain::Swapchain(Swapchain&&) noexcept
{
}

Swapchain::~Swapchain() noexcept
{
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
