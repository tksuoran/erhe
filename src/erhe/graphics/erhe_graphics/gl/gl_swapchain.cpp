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

void Swapchain_impl::wait_frame(Frame_state& out_frame_state)
{
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
}

void Swapchain_impl::begin_frame(const Frame_begin_info& frame_begin_info)
{
    static_cast<void>(frame_begin_info);
}

void Swapchain_impl::end_frame(const Frame_end_info& frame_end_info)
{
    static_cast<void>(frame_end_info);
    if (m_context_window) {
        m_context_window->swap_buffers();
    }
}

} // namespace erhe::graphics
