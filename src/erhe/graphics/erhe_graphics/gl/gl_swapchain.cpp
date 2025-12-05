#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_graphics/gl/gl_surface.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_window/window.hpp"

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(Swapchain_impl&& old) noexcept
    : m_device        {old.m_device}
    , m_surface       {old.m_surface}
    , m_context_window{std::exchange(old.m_context_window, nullptr)}
{
}

Swapchain_impl::~Swapchain_impl() noexcept
{
}

Swapchain_impl::Swapchain_impl(Device& device, const Swapchain_create_info& create_info)
    : m_device        {device}
    , m_surface       {create_info.surface}
    , m_context_window{create_info.surface.get_impl().get_context_window()}
{
}

void Swapchain_impl::resize_to_window()
{
}

void Swapchain_impl::present()
{
    m_context_window->swap_buffers();
}

} // namespace erhe::graphics
