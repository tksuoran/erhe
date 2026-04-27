#include "erhe_graphics/metal/metal_command_buffer.hpp"
#include "erhe_graphics/metal/metal_device.hpp"
#include "erhe_graphics/metal/metal_swapchain.hpp"
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
    // Stub: iteration target.
}

void Command_buffer_impl::end()
{
    // Stub: iteration target.
}

auto Command_buffer_impl::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
    static_cast<void>(m_device);
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
    return true;
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
        // Friend access: latch the drawable + flag for the legacy
        // end_frame submit branch.
        Device_impl& device_impl = m_device->get_impl();
        device_impl.m_pending_drawable    = swapchain->get_impl().get_current_drawable();
        device_impl.m_had_swapchain_frame = true;
    }
    return ok;
}

void Command_buffer_impl::end_swapchain(const Frame_end_info& /*frame_end_info*/)
{
    // No submit yet; legacy end_frame still drives present.
}

void Command_buffer_impl::wait_for_fence(Command_buffer& /*other*/)
{
    // Metal cross-cb sync uses MTLEvent / wait/signal scheduling; not
    // wired through this layer yet. Iteration target.
}

void Command_buffer_impl::wait_for_semaphore(Command_buffer& /*other*/)
{
    // Metal cross-cb sync uses MTLEvent / wait/signal scheduling; not
    // wired through this layer yet. Iteration target.
}

void Command_buffer_impl::signal_semaphore(Command_buffer& /*other*/)
{
    // Metal cross-cb sync uses MTLEvent / wait/signal scheduling; not
    // wired through this layer yet. Iteration target.
}

void Command_buffer_impl::signal_fence(Command_buffer& /*other*/)
{
    // Metal cross-cb sync uses MTLEvent / wait/signal scheduling; not
    // wired through this layer yet. Iteration target.
}

// Metal upload / clear / transition: forward to Device_impl which
// owns the legacy MTLBlitCommandEncoder path. When Metal's
// Command_buffer_impl gets a real MTLCommandBuffer the bodies should
// move here just like the Vulkan ones did.
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
