#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_graphics/gl/gl_binding_state.hpp"
#include "erhe_graphics/gl/gl_context_index.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_window/window.hpp"

#include <algorithm>

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
    return present();
}

auto Swapchain_impl::present() -> bool
{
    if (m_context_window) {
        record_capture();
        m_context_window->swap_buffers();
    }
    return true;
}

void Swapchain_impl::request_capture()
{
    m_capture_requested = true;
}

void Swapchain_impl::record_capture()
{
    if (!m_capture_requested) {
        return;
    }
    m_capture_requested = false;
    ERHE_VERIFY_GL_THREAD_MAIN_CONTEXT();

    const int width  = m_context_window->get_width();
    const int height = m_context_window->get_height();
    if ((width <= 0) || (height <= 0)) {
        return;
    }
    const std::size_t row_size   = static_cast<std::size_t>(width) * 4u;
    const std::size_t byte_count = row_size * static_cast<std::size_t>(height);
    m_capture_pixels.resize(byte_count);

    // Read into client memory: no pixel pack buffer, default pack layout.
    gl::bind_buffer (gl::Buffer_target::pixel_pack_buffer, 0);
    gl::pixel_store_i(gl::Pixel_store_parameter::pack_alignment,    1);
    gl::pixel_store_i(gl::Pixel_store_parameter::pack_row_length,   0);
    gl::pixel_store_i(gl::Pixel_store_parameter::pack_image_height, 0);
    {
        const Framebuffer_binding_guard read_framebuffer_guard =
            m_device_impl.get_binding_state().push_framebuffer(gl::Framebuffer_target::read_framebuffer, 0);
        gl::named_framebuffer_read_buffer(0, gl::Color_buffer::back);
        gl::readn_pixels(
            0, 0, width, height,
            gl::Pixel_format::rgba, gl::Pixel_type::unsigned_byte,
            static_cast<GLsizei>(byte_count), m_capture_pixels.data()
        );
    }

    // GL rows run bottom-up; the capture contract is top-down and opaque.
    for (int y = 0; y < (height / 2); ++y) {
        std::byte* const top    = m_capture_pixels.data() + (static_cast<std::size_t>(y) * row_size);
        std::byte* const bottom = m_capture_pixels.data() + (static_cast<std::size_t>(height - 1 - y) * row_size);
        std::swap_ranges(top, top + row_size, bottom);
    }
    for (std::size_t i = 3; i < byte_count; i += 4) {
        m_capture_pixels[i] = std::byte{0xffu};
    }

    m_capture_width       = static_cast<uint32_t>(width);
    m_capture_height      = static_cast<uint32_t>(height);
    m_capture_frame_index = m_device_impl.get_frame_index();
    m_capture_ready       = true;
}

auto Swapchain_impl::read_back_capture(uint32_t& out_width, uint32_t& out_height, std::vector<std::byte>& out_rgba8) -> bool
{
    if (!m_capture_ready) {
        return false;
    }
    m_capture_ready = false;
    // Reject a stale capture whose collector went away: the caller re-arms
    // and gets a current frame instead. The normal arm -> present -> collect
    // cycle sees an age of 1.
    const uint64_t age = m_device_impl.get_frame_index() - m_capture_frame_index;
    if (age > 2) {
        return false;
    }
    out_rgba8  = m_capture_pixels;
    out_width  = m_capture_width;
    out_height = m_capture_height;
    return true;
}

auto Swapchain_impl::has_depth() const -> bool
{
    if (m_context_window) {
        return m_context_window->get_window_configuration().use_depth;
    }
    return false;
}

auto Swapchain_impl::has_stencil() const -> bool
{
    if (m_context_window) {
        return m_context_window->get_window_configuration().use_stencil;
    }
    return false;
}

auto Swapchain_impl::get_color_format() const -> erhe::dataformat::Format
{
    return erhe::dataformat::Format::format_8_vec4_srgb; // GL default framebuffer is sRGB
}

auto Swapchain_impl::get_depth_format() const -> erhe::dataformat::Format
{
    return erhe::dataformat::Format::format_undefined; // GL default framebuffer depth is implicit
}

} // namespace erhe::graphics
