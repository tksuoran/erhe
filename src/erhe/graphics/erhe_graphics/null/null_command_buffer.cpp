#include "erhe_graphics/null/null_command_buffer.hpp"
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

void Command_buffer_impl::wait_for_fence(Command_buffer& /*other*/)
{
}

void Command_buffer_impl::wait_for_semaphore(Command_buffer& /*other*/)
{
}

void Command_buffer_impl::signal_semaphore(Command_buffer& /*other*/)
{
}

void Command_buffer_impl::signal_fence(Command_buffer& /*other*/)
{
}

void Command_buffer_impl::upload_to_buffer(const Buffer& /*buffer*/, std::size_t /*offset*/, const void* /*data*/, std::size_t /*length*/)
{
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

void Command_buffer_impl::cmd_texture_barrier(std::uint64_t /*usage_before*/, std::uint64_t /*usage_after*/)
{
}

void Command_buffer_impl::memory_barrier(Memory_barrier_mask /*barriers*/)
{
}

} // namespace erhe::graphics
