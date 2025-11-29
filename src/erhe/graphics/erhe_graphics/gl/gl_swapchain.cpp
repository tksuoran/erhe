// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(Swapchain_impl&& old) noexcept
    : m_device { old.m_device }
    , m_surface{ old.m_surface }
{
}

Swapchain_impl::~Swapchain_impl() noexcept
{
}

Swapchain_impl::Swapchain_impl(Device& device, const Swapchain_create_info& create_info)
    : m_device {device}
    , m_surface{create_info.surface}
{
}

} // namespace erhe::graphics
