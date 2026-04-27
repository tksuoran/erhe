#include "erhe_graphics/command_buffer.hpp"

#if defined(ERHE_GRAPHICS_LIBRARY_OPENGL)
# include "erhe_graphics/gl/gl_command_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_VULKAN)
# include "erhe_graphics/vulkan/vulkan_command_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_METAL)
# include "erhe_graphics/metal/metal_command_buffer.hpp"
#endif
#if defined(ERHE_GRAPHICS_LIBRARY_NONE)
# include "erhe_graphics/null/null_command_buffer.hpp"
#endif

namespace erhe::graphics {

Command_buffer::Command_buffer(Device& device, erhe::utility::Debug_label debug_label)
    : m_impl{std::make_unique<Command_buffer_impl>(device, std::move(debug_label))}
{
}

Command_buffer::~Command_buffer() noexcept = default;
Command_buffer::Command_buffer(Command_buffer&& other) noexcept = default;
auto Command_buffer::operator=(Command_buffer&& other) noexcept -> Command_buffer& = default;

auto Command_buffer::get_debug_label() const noexcept -> erhe::utility::Debug_label
{
    return m_impl->get_debug_label();
}

void Command_buffer::begin()
{
    m_impl->begin();
    m_recording = true;
}

void Command_buffer::end()
{
    m_impl->end();
    m_recording = false;
}

auto Command_buffer::is_recording() const noexcept -> bool
{
    return m_recording;
}

auto Command_buffer::wait_for_swapchain(Frame_state& out_frame_state) -> bool
{
    return m_impl->wait_for_swapchain(out_frame_state);
}

auto Command_buffer::begin_swapchain(const Frame_begin_info& frame_begin_info, Frame_state& out_frame_state) -> bool
{
    return m_impl->begin_swapchain(frame_begin_info, out_frame_state);
}

void Command_buffer::end_swapchain(const Frame_end_info& frame_end_info)
{
    m_impl->end_swapchain(frame_end_info);
}

void Command_buffer::wait_for_fence(Command_buffer& other)
{
    m_impl->wait_for_fence(other);
}

void Command_buffer::wait_for_semaphore(Command_buffer& other)
{
    m_impl->wait_for_semaphore(other);
}

void Command_buffer::signal_semaphore(Command_buffer& other)
{
    m_impl->signal_semaphore(other);
}

void Command_buffer::signal_fence(Command_buffer& other)
{
    m_impl->signal_fence(other);
}

void Command_buffer::upload_to_buffer(const Buffer& buffer, std::size_t offset, const void* data, std::size_t length)
{
    m_impl->upload_to_buffer(buffer, offset, data, length);
}

void Command_buffer::upload_to_texture(
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
    m_impl->upload_to_texture(texture, level, x, y, width, height, pixelformat, data, row_stride);
}

void Command_buffer::clear_texture(const Texture& texture, std::array<double, 4> clear_value)
{
    m_impl->clear_texture(texture, clear_value);
}

void Command_buffer::transition_texture_layout(const Texture& texture, Image_layout new_layout)
{
    m_impl->transition_texture_layout(texture, new_layout);
}

void Command_buffer::cmd_texture_barrier(std::uint64_t usage_before, std::uint64_t usage_after)
{
    m_impl->cmd_texture_barrier(usage_before, usage_after);
}

void Command_buffer::memory_barrier(Memory_barrier_mask barriers)
{
    m_impl->memory_barrier(barriers);
}

auto Command_buffer::get_impl() -> Command_buffer_impl&
{
    return *m_impl.get();
}
auto Command_buffer::get_impl() const -> const Command_buffer_impl&
{
    return *m_impl.get();
}

} // namespace erhe::graphics
