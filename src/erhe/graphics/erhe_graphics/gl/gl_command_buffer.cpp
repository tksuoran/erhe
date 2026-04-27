#include "erhe_graphics/gl/gl_command_buffer.hpp"
#include "erhe_graphics/gl/gl_device.hpp"
#include "erhe_graphics/gl/gl_swapchain.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/surface.hpp"
#include "erhe_graphics/swapchain.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_dataformat/dataformat.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

Command_buffer_impl::Command_buffer_impl(Device& device, erhe::utility::Debug_label debug_label)
    : m_device     {&device}
    , m_debug_label{std::move(debug_label)}
{
}

Command_buffer_impl::~Command_buffer_impl() noexcept = default;
Command_buffer_impl::Command_buffer_impl(Command_buffer_impl&& other) noexcept = default;
auto Command_buffer_impl::operator=(Command_buffer_impl&& other) noexcept -> Command_buffer_impl& = default;

auto Command_buffer_impl::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_debug_label;
}

void Command_buffer_impl::begin()
{
    // Stub: GL has no native command buffer; iteration target.
}

void Command_buffer_impl::end()
{
    // Stub: GL has no native command buffer; iteration target.
}

auto Command_buffer_impl::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
    Surface* surface = m_device->get_surface();
    if (surface != nullptr) {
        Swapchain* swapchain = surface->get_swapchain();
        if (swapchain != nullptr) {
            if (swapchain->get_impl().wait_frame(out_frame_state)) {
                return true;
            }
        }
    }
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = false;
    return false;
}

auto Command_buffer_impl::begin_swapchain(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
    Surface* surface = m_device->get_surface();
    if (surface == nullptr) {
        out_frame_state.should_render = false;
        return false;
    }
    Swapchain* swapchain = surface->get_swapchain();
    if (swapchain == nullptr) {
        out_frame_state.should_render = false;
        return false;
    }

    const bool ok = swapchain->get_impl().begin_frame(frame_begin_info);
    out_frame_state.should_render = ok;
    if (ok) {
        m_swapchain_used = &swapchain->get_impl();
        // Friend access -- see Vulkan equivalent. Kept for now while
        // the legacy single-cb path is being removed.
        m_device->get_impl().m_had_swapchain_frame = true;
    }
    return ok;
}

auto Command_buffer_impl::take_swapchain_used() noexcept -> Swapchain_impl*
{
    Swapchain_impl* swapchain = m_swapchain_used;
    m_swapchain_used = nullptr;
    return swapchain;
}

void Command_buffer_impl::end_swapchain(const Frame_end_info& /*frame_end_info*/)
{
    // No-op on GL: legacy end_frame still drives the swap_buffers via
    // Swapchain_impl::end_frame.
}

void Command_buffer_impl::wait_for_fence(Command_buffer& /*other*/)
{
    // GL serializes through the driver-managed command stream; explicit
    // cross-cb fence waits are a no-op for now.
}

void Command_buffer_impl::wait_for_semaphore(Command_buffer& /*other*/)
{
    // GL has no equivalent of Vulkan binary semaphores in this layer.
}

void Command_buffer_impl::signal_semaphore(Command_buffer& /*other*/)
{
    // GL has no equivalent of Vulkan binary semaphores in this layer.
}

void Command_buffer_impl::signal_fence(Command_buffer& /*other*/)
{
    // GL has no implicit per-cb fence; iteration target if needed.
}

// GL has no native command buffer, so the recording state lives on
// Device_impl (m_staging_buffer, m_gl_binding_state, etc.). The
// Command_buffer_impl just forwards: the public Command_buffer is
// the user-facing API but the GL device still does the work.
void Command_buffer_impl::upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length)
{
    ERHE_VERIFY(m_device != nullptr);
    m_device->get_impl().upload_to_buffer(buffer, offset, data, length);
}

void Command_buffer_impl::upload_to_texture(
    const Texture&           texture,
    int                      level,
    int                      x,
    int                      y,
    int                      width,
    int                      height,
    erhe::dataformat::Format pixelformat,
    const void*              data,
    int                      row_stride
)
{
    ERHE_VERIFY(m_device != nullptr);
    m_device->get_impl().upload_to_texture(texture, level, x, y, width, height, pixelformat, data, row_stride);
}

void Command_buffer_impl::clear_texture(const Texture& texture, std::array<double, 4> clear_value)
{
    ERHE_VERIFY(m_device != nullptr);
    m_device->get_impl().clear_texture(texture, clear_value);
}

void Command_buffer_impl::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    ERHE_VERIFY(m_device != nullptr);
    m_device->get_impl().transition_texture_layout(texture, new_layout);
}

void Command_buffer_impl::cmd_texture_barrier(std::uint64_t usage_before, std::uint64_t usage_after)
{
    ERHE_VERIFY(m_device != nullptr);
    m_device->get_impl().cmd_texture_barrier(usage_before, usage_after);
}

void Command_buffer_impl::memory_barrier(Memory_barrier_mask barriers)
{
    ERHE_VERIFY(m_device != nullptr);
    m_device->get_impl().memory_barrier(barriers);
}

} // namespace erhe::graphics
