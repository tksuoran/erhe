#include "erhe_graphics/null/null_swapchain.hpp"

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(
    Device_impl&  device_impl,
    Surface_impl& surface_impl
)
    : m_device_impl {device_impl}
    , m_surface_impl{surface_impl}
{
}

Swapchain_impl::~Swapchain_impl() noexcept = default;

auto Swapchain_impl::wait_frame(Frame_state& out_frame_state) -> bool
{
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
    return true;
}

auto Swapchain_impl::begin_frame(const Frame_begin_info& frame_begin_info) -> bool
{
    static_cast<void>(frame_begin_info);
    return true;
}

auto Swapchain_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    static_cast<void>(frame_end_info);
    return true;
}

auto Swapchain_impl::has_depth() const -> bool
{
    return false;
}

auto Swapchain_impl::has_stencil() const -> bool
{
    return false;
}

} // namespace erhe::graphics
