#include "erhe_graphics/null/null_command_buffer.hpp"
#include "erhe_graphics/enums.hpp"
#include "erhe_graphics/swapchain.hpp"

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
    // Stub: iteration target.
}

void Command_buffer_impl::end()
{
    // Stub: iteration target.
}

auto Command_buffer_impl::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = true;
    return true;
}

auto Command_buffer_impl::begin_swapchain(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    static_cast<void>(frame_begin_info);
    out_frame_state.predicted_display_time   = 0;
    out_frame_state.predicted_display_period = 0;
    out_frame_state.should_render            = false;
    return false;
}

void Command_buffer_impl::end_swapchain(const Frame_end_info& frame_end_info)
{
    static_cast<void>(frame_end_info);
}

void Command_buffer_impl::signal_gpu_completion_to(Command_buffer& /*consumer*/)
{
}

void Command_buffer_impl::wait_for_gpu_completion_signal()
{
}

void Command_buffer_impl::signal_cpu_completion()
{
}

void Command_buffer_impl::wait_for_cpu_completion_of(Command_buffer& /*producer*/)
{
}

void Command_buffer_impl::upload_to_buffer(const Buffer& /*buffer*/, std::size_t /*offset*/, const void* /*data*/, std::size_t /*length*/)
{
}

auto Command_buffer_impl::try_upload_to_buffer(const Buffer& /*buffer*/, std::size_t /*offset*/, const void* /*data*/, std::size_t /*length*/, std::size_t /*staging_capacity_byte_count*/) -> bool
{
    return true;
}

void Command_buffer_impl::upload_to_texture(
    const Texture&           /*texture*/,
    int                      /*level*/,
    int                      /*x*/,
    int                      /*y*/,
    int                      /*width*/,
    int                      /*height*/,
    erhe::dataformat::Format /*pixelformat*/,
    const void*              /*data*/,
    int                      /*row_stride*/
)
{
}

void Command_buffer_impl::clear_texture(const Texture& /*texture*/, std::array<double, 4> /*clear_value*/)
{
}

void Command_buffer_impl::transition_texture_layout(const Texture& /*texture*/, Image_layout /*new_layout*/)
{
}

void Command_buffer_impl::texture_barrier(
    const Texture&                   /*texture*/,
    const Texture_subresource_range& /*range*/,
    std::uint64_t                    /*usage_before*/,
    std::uint64_t                    /*usage_after*/,
    Barrier_scope                    /*scope*/
)
{
}

void Command_buffer_impl::buffer_barrier(const Buffer& /*buffer*/, std::uint64_t /*usage_before*/, std::uint64_t /*usage_after*/, Barrier_scope /*scope*/)
{
}

void Command_buffer_impl::cmd_texture_barrier(std::uint64_t /*usage_before*/, std::uint64_t /*usage_after*/)
{
}

void Command_buffer_impl::memory_barrier(Memory_barrier_mask /*barriers*/)
{
}

} // namespace erhe::graphics
