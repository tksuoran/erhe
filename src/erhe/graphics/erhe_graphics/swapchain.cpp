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
    
Swapchain::Swapchain(std::unique_ptr<Swapchain_impl>&& swapchain_impl)
    : m_impl{std::move(swapchain_impl)}
{
}

Swapchain::~Swapchain() noexcept
{
}

void Swapchain::start_of_frame()
{
    m_impl->start_of_frame();
}

void Swapchain::present()
{
    m_impl->present();
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
