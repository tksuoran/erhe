// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/gl/gl_ring_buffer.hpp"

namespace erhe::graphics {
    
Ring_buffer::Ring_buffer(Device& device, const Ring_buffer_create_info& create_info)
    : m_impl{std::make_unique<Ring_buffer_impl>(device, *this, create_info)}
{
}
Ring_buffer::~Ring_buffer() = default;
void Ring_buffer::get_size_available_for_write(
    std::size_t  required_alignment,
    std::size_t& out_alignment_byte_count_without_wrap,
    std::size_t& out_available_byte_count_without_wrap,
    std::size_t& out_available_byte_count_with_wrap
) const
{
    m_impl->get_size_available_for_write(
        required_alignment,
        out_alignment_byte_count_without_wrap,
        out_available_byte_count_without_wrap,
        out_available_byte_count_with_wrap
    );
}
auto Ring_buffer::acquire(std::size_t required_alignment, Ring_buffer_usage ring_buffer_usage, std::size_t byte_count) -> Ring_buffer_range
{
    return m_impl->acquire(required_alignment, ring_buffer_usage, byte_count);
}
auto Ring_buffer::match(Ring_buffer_usage usage) const -> bool
{
    return m_impl->match(usage);
}
void Ring_buffer::flush(std::size_t byte_offset, std::size_t byte_count)
{
    return m_impl->flush(byte_offset, byte_count);
}
void Ring_buffer::close(std::size_t byte_offset, std::size_t byte_write_count)
{
    m_impl->close(byte_offset, byte_write_count);
}
void Ring_buffer::make_sync_entry(std::size_t wrap_count, std::size_t byte_offset, std::size_t byte_count)
{
    m_impl->make_sync_entry(wrap_count, byte_offset, byte_count);
}
auto Ring_buffer::get_buffer() -> Buffer*
{
    return m_impl->get_buffer();
}
void Ring_buffer::frame_completed(uint64_t frame)
{
    m_impl->frame_completed(frame);
}

} // namespace erhe::graphics
