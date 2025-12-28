#include "erhe_graphics/gl/gl_surface.hpp"
#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_window/window.hpp"

namespace erhe::graphics {

Surface_impl::Surface_impl(Device_impl& device_impl, const Surface_create_info& create_info)
    : m_device_impl        {device_impl}
    , m_surface_create_info{create_info}
    , m_swapchain{
        std::make_unique<Swapchain>(
            std::make_unique<Swapchain_impl>(
                m_device_impl,
                *this,
                m_surface_create_info.context_window
            )
        )
    }
{
}

Surface_impl::~Surface_impl() noexcept
{
}

[[nodiscard]] auto Surface_impl::get_swapchain() -> Swapchain*
{
    return m_swapchain.get();
}

} // namespace erhe::graphics
