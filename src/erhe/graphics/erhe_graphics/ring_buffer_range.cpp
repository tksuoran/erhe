// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {
    
// Ring buffer

Ring_buffer_range::Ring_buffer_range()
    : m_ring_buffer                     {nullptr}
    , m_span                            {}
    , m_wrap_count                      {0}
    , m_byte_span_start_offset_in_buffer{0}
    , m_byte_write_position_in_span     {0}
    , m_usage                           {Ring_buffer_usage::None}
{
    SPDLOG_LOGGER_TRACE(log_gpu_ring_buffer, "Ring_buffer_range::Ring_buffer_range()");
}

Ring_buffer_range::Ring_buffer_range(
    Ring_buffer&         ring_buffer,
    Ring_buffer_usage    usage,
    std::span<std::byte> span,
    std::size_t          wrap_count,
    size_t               byte_start_offset_in_buffer
)
    : m_ring_buffer                     {&ring_buffer}
    , m_span                            {span}
    , m_wrap_count                      {wrap_count}
    , m_byte_span_start_offset_in_buffer{byte_start_offset_in_buffer}
    , m_byte_write_position_in_span     {0}
    , m_usage                           {usage}
{
    ERHE_VERIFY(usage != Ring_buffer_usage::None);
    SPDLOG_LOGGER_TRACE(
        log_gpu_ring_buffer,
        "Ring_buffer_range::Ring_buffer_range() buffer = {} start offset in buffer = {} span byte count = {}",
        ring_buffer.get_buffer().debug_label,
        byte_start_offset,
        span.size_bytes()
    );
}

Ring_buffer_range::Ring_buffer_range(Ring_buffer_range&& old) noexcept
    : m_ring_buffer                     {std::exchange(old.m_ring_buffer,                      nullptr)}
    , m_span                            {std::exchange(old.m_span,                             {}     )}
    , m_wrap_count                      {std::exchange(old.m_wrap_count,                       0      )}
    , m_byte_span_start_offset_in_buffer{std::exchange(old.m_byte_span_start_offset_in_buffer, 0      )}
    , m_byte_write_position_in_span     {std::exchange(old.m_byte_write_position_in_span,      0      )}
    , m_byte_flush_position_in_span     {std::exchange(old.m_byte_flush_position_in_span,      0      )}
    , m_usage                           {std::exchange(old.m_usage,                            Ring_buffer_usage::None)}
    , m_is_closed                       {std::exchange(old.m_is_closed,                        true   )}
    , m_is_released                     {std::exchange(old.m_is_released ,                     false  )}
    , m_is_cancelled                    {std::exchange(old.m_is_cancelled,                     true   )}
{
}

Ring_buffer_range& Ring_buffer_range::operator=(Ring_buffer_range&& old) noexcept
{
    m_ring_buffer                      = std::exchange(old.m_ring_buffer,                      nullptr);
    m_span                             = std::exchange(old.m_span,                             {}     );
    m_wrap_count                       = std::exchange(old.m_wrap_count,                       0      );
    m_byte_span_start_offset_in_buffer = std::exchange(old.m_byte_span_start_offset_in_buffer, 0      );
    m_byte_write_position_in_span      = std::exchange(old.m_byte_write_position_in_span,      0      );
    m_byte_flush_position_in_span      = std::exchange(old.m_byte_flush_position_in_span,      0      );
    m_usage                            = std::exchange(old.m_usage,                            Ring_buffer_usage::None);
    m_is_closed                        = std::exchange(old.m_is_closed,                        true   );
    m_is_released                      = std::exchange(old.m_is_released,                      false  );
    m_is_cancelled                     = std::exchange(old.m_is_cancelled,                     true   );
    return *this;
}

Ring_buffer_range::~Ring_buffer_range()
{
    ERHE_VERIFY((m_ring_buffer == nullptr) || m_is_released || m_is_cancelled);
}

void Ring_buffer_range::close()
{
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_ring_buffer->close(m_byte_span_start_offset_in_buffer, m_byte_write_position_in_span);
    m_is_closed = true;
}

void Ring_buffer_range::bytes_gpu_used(std::size_t byte_count)
{
    //ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    //// ERHE_VERIFY(m_byte_write_position_in_span + byte_count <= m_span.size_bytes());
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_byte_write_position_in_span += byte_count;
}

void Ring_buffer_range::bytes_written(std::size_t byte_count)
{
    //ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(m_byte_write_position_in_span + byte_count <= m_span.size_bytes());
    ERHE_VERIFY(m_ring_buffer != nullptr);
    m_byte_write_position_in_span += byte_count;
}

void Ring_buffer_range::flush(std::size_t byte_write_position_in_span)
{
    ERHE_VERIFY(!is_closed());
    ERHE_VERIFY(!m_is_released);
    ERHE_VERIFY(byte_write_position_in_span >= m_byte_write_position_in_span);
    m_byte_write_position_in_span = byte_write_position_in_span;
    ERHE_VERIFY(m_usage == Ring_buffer_usage::CPU_write);
    ERHE_VERIFY(m_ring_buffer != nullptr);
    const size_t flush_byte_count = byte_write_position_in_span - m_byte_flush_position_in_span;
    if (flush_byte_count > 0) {
        m_ring_buffer->flush(m_byte_flush_position_in_span, flush_byte_count);
    }
    m_byte_flush_position_in_span = byte_write_position_in_span;
}

void Ring_buffer_range::release()
{
    ERHE_PROFILE_FUNCTION();

    ERHE_VERIFY(!m_is_released);
    m_is_released = true;

    if (m_ring_buffer == nullptr) {
        ERHE_VERIFY(m_byte_write_position_in_span == 0);
        return;
    }
    ERHE_VERIFY(is_closed());
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    m_ring_buffer->make_sync_entry(
        m_wrap_count,
        m_byte_span_start_offset_in_buffer,
        m_byte_write_position_in_span
    );
}

void Ring_buffer_range::cancel()
{
    ERHE_PROFILE_FUNCTION();
    ERHE_VERIFY(!m_is_cancelled);
    m_is_cancelled = true;
    if (m_byte_write_position_in_span == 0) {
        return;
    }

    ERHE_VERIFY(m_ring_buffer != nullptr);
}

auto Ring_buffer_range::get_span() const -> std::span<std::byte>
{
    return m_span;
}

auto Ring_buffer_range::get_byte_start_offset_in_buffer() const -> std::size_t
{
    return m_byte_span_start_offset_in_buffer;
}

auto Ring_buffer_range::get_writable_byte_count() const -> std::size_t
{
    ERHE_VERIFY(m_span.size_bytes() >= m_byte_write_position_in_span);
    return m_span.size_bytes() - m_byte_write_position_in_span;
}

auto Ring_buffer_range::get_written_byte_count() const -> std::size_t
{
    return m_byte_write_position_in_span;
}

auto Ring_buffer_range::is_closed() const -> bool
{
    return m_is_closed;
}

auto Ring_buffer_range::get_buffer() const -> Ring_buffer*
{
    return m_ring_buffer;
}

} // namespace erhe::graphics
