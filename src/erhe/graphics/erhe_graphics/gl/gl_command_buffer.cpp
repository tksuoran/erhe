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

auto Command_buffer_impl::get_frame_id() const noexcept -> std::uint64_t
{
    return m_frame_id;
}

void Command_buffer_impl::set_frame_id(const std::uint64_t frame_id) noexcept
{
    m_frame_id = frame_id;
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

void Command_buffer_impl::signal_gpu_completion_to(Command_buffer& /*consumer*/)
{
    // GL has no equivalent of Vulkan binary semaphores in this layer.
}

void Command_buffer_impl::wait_for_gpu_completion_signal()
{
    // GL has no equivalent of Vulkan binary semaphores in this layer.
}

void Command_buffer_impl::signal_cpu_completion()
{
    // GL has no implicit per-cb fence; iteration target if needed.
}

void Command_buffer_impl::wait_for_cpu_completion_of(Command_buffer& /*producer*/)
{
    // GL serializes through the driver-managed command stream; explicit
    // cross-cb CPU waits are a no-op for now.
}

// GL has no native command buffer, so the recording state lives on
// Device_impl (m_staging_buffer, the per-context caches, etc.). The
// Command_buffer_impl just forwards: the public Command_buffer is
// the user-facing API but the GL device still does the work.
void Command_buffer_impl::upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length)
{
    ERHE_VERIFY(m_device != nullptr);
    m_device->get_impl().upload_to_buffer(buffer, offset, data, length);
}

auto Command_buffer_impl::try_upload_to_buffer(const Buffer& buffer, const std::size_t offset, const void* data, const std::size_t length, const std::size_t staging_capacity_byte_count) -> bool
{
    ERHE_VERIFY(m_device != nullptr);
    return m_device->get_impl().try_upload_to_buffer(buffer, offset, data, length, staging_capacity_byte_count);
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

namespace {

// glMemoryBarrier bits for the accesses of a texture_barrier's usage_after.
// OpenGL has no image layouts and orders everything but incoherent shader
// writes (image stores, SSBO writes, atomics) itself; the barrier bits are
// the consumer side of those writes.
[[nodiscard]] auto get_texture_consumer_barrier_bits(const std::uint64_t usage_after) -> unsigned int
{
    unsigned int bits = 0;
    if ((usage_after & Image_usage_flag_bit_mask::sampled) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::texture_fetch_barrier_bit);
    }
    if ((usage_after & Image_usage_flag_bit_mask::input_attachment) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::texture_fetch_barrier_bit);
    }
    if ((usage_after & Image_usage_flag_bit_mask::storage) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::shader_image_access_barrier_bit);
    }
    if ((usage_after & (Image_usage_flag_bit_mask::color_attachment | Image_usage_flag_bit_mask::depth_stencil_attachment)) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::framebuffer_barrier_bit);
    }
    if ((usage_after & (Image_usage_flag_bit_mask::transfer_src | Image_usage_flag_bit_mask::transfer_dst | Image_usage_flag_bit_mask::host_transfer)) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::texture_update_barrier_bit);
        bits |= static_cast<unsigned int>(Memory_barrier_mask::pixel_buffer_barrier_bit);
    }
    return bits;
}

// glMemoryBarrier bits for the accesses of a buffer_barrier's usage_after.
[[nodiscard]] auto get_buffer_consumer_barrier_bits(const std::uint64_t usage_after) -> unsigned int
{
    unsigned int bits = 0;
    if ((usage_after & Buffer_usage_flag_bit_mask::vertex) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::vertex_attrib_array_barrier_bit);
    }
    if ((usage_after & Buffer_usage_flag_bit_mask::index) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::element_array_barrier_bit);
    }
    if ((usage_after & Buffer_usage_flag_bit_mask::indirect) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::command_barrier_bit);
    }
    if ((usage_after & Buffer_usage_flag_bit_mask::uniform) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::uniform_barrier_bit);
    }
    if ((usage_after & (Buffer_usage_flag_bit_mask::storage_read | Buffer_usage_flag_bit_mask::storage_write)) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::shader_storage_barrier_bit);
        bits |= static_cast<unsigned int>(Memory_barrier_mask::atomic_counter_barrier_bit);
    }
    if ((usage_after & (Buffer_usage_flag_bit_mask::transfer_src | Buffer_usage_flag_bit_mask::transfer_dst)) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::buffer_update_barrier_bit);
        bits |= static_cast<unsigned int>(Memory_barrier_mask::pixel_buffer_barrier_bit);
    }
    if ((usage_after & (Buffer_usage_flag_bit_mask::host_read | Buffer_usage_flag_bit_mask::host_write)) != 0) {
        bits |= static_cast<unsigned int>(Memory_barrier_mask::client_mapped_buffer_barrier_bit);
        bits |= static_cast<unsigned int>(Memory_barrier_mask::buffer_update_barrier_bit);
    }
    // acceleration_structure_*: OpenGL has no ray tracing.
    return bits;
}

} // anonymous namespace

void Command_buffer_impl::texture_barrier(
    const Texture&                   /*texture*/,
    const Texture_subresource_range& /*range*/,
    const std::uint64_t              /*usage_before*/,
    const std::uint64_t              usage_after,
    const Barrier_scope              /*scope*/
)
{
    // One GL context executes in issue order, so the scope does not matter.
    ERHE_VERIFY(m_device != nullptr);
    const unsigned int bits = get_texture_consumer_barrier_bits(usage_after);
    if (bits != 0) {
        m_device->get_impl().memory_barrier(static_cast<Memory_barrier_mask>(bits));
    }
}

void Command_buffer_impl::buffer_barrier(const Buffer& /*buffer*/, const std::uint64_t /*usage_before*/, const std::uint64_t usage_after, const Barrier_scope /*scope*/)
{
    ERHE_VERIFY(m_device != nullptr);
    const unsigned int bits = get_buffer_consumer_barrier_bits(usage_after);
    if (bits != 0) {
        m_device->get_impl().memory_barrier(static_cast<Memory_barrier_mask>(bits));
    }
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
