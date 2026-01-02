// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/command_encoder.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {
    
Ring_buffer_client::Ring_buffer_client(
    Device&                     graphics_device,
    Buffer_target               buffer_target,
    std::string_view            debug_label,
    std::optional<unsigned int> binding_point
)
    : m_graphics_device{graphics_device}
    , m_buffer_target  {buffer_target}
    , m_debug_label    {debug_label}
    , m_binding_point  {binding_point}
{
}

auto Ring_buffer_client::acquire(Ring_buffer_usage usage, std::size_t byte_count) -> Ring_buffer_range
{
    ERHE_VERIFY(byte_count > 0);
    return m_graphics_device.allocate_ring_buffer_entry(m_buffer_target, usage, byte_count);
}

auto Ring_buffer_client::bind(Command_encoder& command_encoder, const Ring_buffer_range& range) -> bool
{
    ERHE_PROFILE_FUNCTION();

    // sanity_check();

    const size_t offset     = range.get_byte_start_offset_in_buffer();
    const size_t byte_count = range.get_written_byte_count();
    if (byte_count == 0) {
        return false;
    }

    Ring_buffer* ring_buffer = range.get_buffer();
    ERHE_VERIFY(ring_buffer != nullptr);
    Buffer* buffer = ring_buffer->get_buffer();
    ERHE_VERIFY(buffer != nullptr);

    //SPDLOG_LOGGER_TRACE(
    //    log_gpu_ring_buffer,
    //    "binding {} {} buffer offset = {} byte count = {}",
    //    m_name,
    //    m_binding_point.has_value() ? "uses binding point" : "non-indexed binding",
    //    m_binding_point.has_value() ? m_binding_point.value() : 0,
    //    range.first_byte_offset,
    //    range.byte_count
    //);

    ERHE_VERIFY(
        (m_buffer_target != Buffer_target::uniform) ||
        (byte_count <= static_cast<std::size_t>(m_graphics_device.get_info().max_uniform_block_size))
    );
    ERHE_VERIFY(offset + byte_count <= buffer->get_capacity_byte_count());

    if (m_binding_point.has_value()) {
        ERHE_VERIFY(is_indexed(m_buffer_target));
        command_encoder.set_buffer(m_buffer_target, buffer, offset, byte_count, m_binding_point.value());
    } else {
        ERHE_VERIFY(!is_indexed(m_buffer_target));
        command_encoder.set_buffer(m_buffer_target, buffer);
    }
    return true;
}

} // namespace erhe::graphics
