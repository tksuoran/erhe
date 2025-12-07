#include "erhe_graphics/gl/gl_surface.hpp"
#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_window/window.hpp"

namespace erhe::graphics {

Surface_impl::Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info)
    : m_device_impl        {device_impl}
    , m_surface_create_info{create_info}
{
}

Surface_impl::~Surface_impl() noexcept
{
}

void Surface_impl::resize_swapchain_to_surface(uint32_t& width, uint32_t& height)
{
    if (!m_swapchain) {
        m_swapchain = std::make_unique<Swapchain>(
            std::make_unique<Swapchain_impl>(
                m_device_impl,
                *this,
                m_surface_create_info.context_window
            )
        );
    }
    if (m_surface_create_info.context_window != nullptr) {
        width  = static_cast<uint32_t>(m_surface_create_info.context_window->get_width());
        height = static_cast<uint32_t>(m_surface_create_info.context_window->get_height());
    } else {
        width  = 0;
        height = 0;
    }
}

[[nodiscard]] auto Surface_impl::get_swapchain() -> Swapchain*
{
    return m_swapchain.get();
}

} // namespace erhe::graphics
