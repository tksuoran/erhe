#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_window/window.hpp"

namespace erhe::graphics {

Swapchain_impl::~Swapchain_impl() noexcept
{
}

Swapchain_impl::Swapchain_impl(
    Device_impl&                  device_impl,
    Surface_impl&                 surface_impl,
    erhe::window::Context_window* context_window
)
    : m_device_impl   {device_impl}
    , m_surface_impl  {surface_impl}
    , m_context_window{context_window}
{
}

void Swapchain_impl::start_of_frame()
{
}

void Swapchain_impl::present()
{
    if (m_context_window) {
        m_context_window->swap_buffers();
    }
}

} // namespace erhe::graphics
