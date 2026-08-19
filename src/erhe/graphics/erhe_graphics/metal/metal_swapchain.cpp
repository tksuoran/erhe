#include "erhe_graphics/metal/metal_swapchain.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_surface.hpp"

#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

namespace erhe::graphics {

Swapchain_impl::Swapchain_impl(
    Device_impl&  device_impl,
    Surface_impl& surface_impl
)
    : m_device_impl {device_impl}
    , m_surface_impl{surface_impl}
{
}

Swapchain_impl::~Swapchain_impl() noexcept
{
    clear_current_drawable();
}

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

    CA::MetalLayer* layer = m_surface_impl.get_metal_layer();
    if (layer == nullptr) {
        return false;
    }

    // nextDrawable() is autoreleased. Take a reference: the drawable is
    // used later in the frame (render pass setup) and at submit time
    // (presentDrawable), and holding it explicitly keeps it alive
    // independently of which autorelease pool happens to be open.
    clear_current_drawable();
    m_current_drawable = layer->nextDrawable();
    if (m_current_drawable != nullptr) {
        m_current_drawable->retain();
    }
    return m_current_drawable != nullptr;
}

void Swapchain_impl::clear_current_drawable()
{
    if (m_current_drawable != nullptr) {
        m_current_drawable->release();
        m_current_drawable = nullptr;
    }
}

auto Swapchain_impl::end_frame(const Frame_end_info& frame_end_info) -> bool
{
    static_cast<void>(frame_end_info);
    clear_current_drawable();
    return true;
}

auto Swapchain_impl::has_depth() const -> bool { return false; }
auto Swapchain_impl::has_stencil() const -> bool { return false; }

auto Swapchain_impl::get_color_format() const -> erhe::dataformat::Format
{
    return erhe::dataformat::Format::format_8_vec4_bgra_srgb;
}

auto Swapchain_impl::get_depth_format() const -> erhe::dataformat::Format
{
    return erhe::dataformat::Format::format_undefined; // swapchain has no depth
}

auto Swapchain_impl::get_current_drawable() const -> CA::MetalDrawable*
{
    return m_current_drawable;
}

} // namespace erhe::graphics
