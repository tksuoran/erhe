#include "erhe_net/ring_buffer.hpp"

#include <cstddef>
#include <cstring>

namespace erhe::net {

Ring_buffer::Ring_buffer(const std::size_t capacity)
{
    m_buffer.resize(capacity);
    m_max_size = capacity;
    reset();
}

Ring_buffer::Ring_buffer(Ring_buffer&& other) noexcept
    : m_buffer      {std::move(other.m_buffer)}
    , m_read_offset {other.m_read_offset}
    , m_write_offset{other.m_write_offset}
    , m_max_size    {other.m_max_size}
    , m_full        {other.m_full}
{
}

auto Ring_buffer::operator=(Ring_buffer&& other) noexcept -> Ring_buffer&
{
    m_buffer       = std::move(other.m_buffer);
    m_read_offset  = other.m_read_offset;
    m_write_offset = other.m_write_offset;
    m_max_size     = other.m_max_size;
    m_full         = other.m_full;
    return *this;
}

void Ring_buffer::reset()
{
    m_write_offset = 0;
    m_read_offset  = 0;
    m_full         = false;
}

auto Ring_buffer::empty() const -> bool
{
    return !m_full && (m_read_offset == m_write_offset);
}

auto Ring_buffer::full() const -> bool
{
    return m_full;
}

auto Ring_buffer::max_size() const -> std::size_t
{
    return m_max_size;
}

auto Ring_buffer::size() const -> std::size_t
{
    if (full()) {
        return m_max_size;
    }
    if (m_write_offset >= m_read_offset) {
        return m_write_offset - m_read_offset;
    }
    return m_max_size + m_write_offset - m_read_offset;
}

void Ring_buffer::rotate()
{
    rotate(m_read_offset);
}

void Ring_buffer::rotate(std::size_t rotate_amount)
{
    size_t count  = 0;
    size_t offset = 0;

    while (count < m_max_size) {
        size_t  to_index   = offset;
        uint8_t tmp        = m_buffer[to_index];
        size_t  from_index = (to_index + rotate_amount) % m_max_size;

        while (from_index != offset) {
            m_buffer[to_index] = m_buffer[from_index];
            to_index   = from_index;
            from_index = (to_index + rotate_amount) % m_max_size;
            ++count;
        }

        m_buffer[to_index] = tmp;
        ++count;
        ++offset;
    }
}

auto Ring_buffer::size_available_for_write() const -> std::size_t
{
    return m_max_size - size();
}

auto Ring_buffer::size_available_for_read() const -> std::size_t
{
    return size();
}

auto Ring_buffer::begin_produce(
    std::size_t& writable_byte_count_before_wrap,
    std::size_t& writable_byte_count_after_wrap
) -> uint8_t*
{
    const std::size_t can_write_count = size_available_for_write();
    if (can_write_count == 0) {
        writable_byte_count_before_wrap = 0;
        writable_byte_count_after_wrap = 0;
        return 0;
    }
    const std::size_t max_count_before_wrap = m_max_size - m_write_offset;
    writable_byte_count_before_wrap = (std::min)(can_write_count, max_count_before_wrap);
    writable_byte_count_after_wrap  = (writable_byte_count_before_wrap < can_write_count)
        ? can_write_count - writable_byte_count_before_wrap
        : 0;

    return &m_buffer[m_write_offset];
}

void Ring_buffer::end_produce(const std::size_t write_byte_count)
{
    m_write_offset = (m_write_offset + write_byte_count) % m_max_size;
    m_full = (m_write_offset == m_read_offset);
}

auto Ring_buffer::write(const uint8_t* src, const std::size_t byte_count) -> std::size_t
{
    const std::size_t can_write_count = (std::min)(size_available_for_write(), byte_count);
    if (can_write_count == 0) {
        return 0;
    }
    const std::size_t max_count_before_wrap = m_max_size - m_write_offset;
    const std::size_t count_before_wrap     = (std::min)(can_write_count, max_count_before_wrap);
    const std::size_t count_after_wrap      = (count_before_wrap < can_write_count) ? (can_write_count - count_before_wrap) : 0;
    memcpy(&m_buffer[m_write_offset], src, count_before_wrap);
    if (count_after_wrap > 0) {
        memcpy(&m_buffer[0], src + count_before_wrap, count_after_wrap);
    }

    m_write_offset = (m_write_offset + can_write_count) % m_max_size;
    m_full = (m_write_offset == m_read_offset);
    return can_write_count;
}

auto Ring_buffer::begin_consume(
    std::size_t& readable_byte_count_before_wrap,
    std::size_t& readable_byte_count_after_wrap
) -> const uint8_t*
{
    const std::size_t can_read_count = size_available_for_read();
    if (can_read_count == 0) {
        readable_byte_count_before_wrap = 0;
        readable_byte_count_after_wrap = 0;
        return nullptr;
    }
    const std::size_t max_count_before_wrap = m_max_size - m_read_offset;
    readable_byte_count_before_wrap = std::min(can_read_count, max_count_before_wrap);
    readable_byte_count_after_wrap  = (readable_byte_count_before_wrap < can_read_count)
        ? (can_read_count - readable_byte_count_before_wrap)
        : 0;

    return &m_buffer[m_read_offset];
}

void Ring_buffer::end_consume(std::size_t byte_count)
{
    m_read_offset = (m_read_offset + byte_count) % m_max_size;
    m_full        = false;
}

auto Ring_buffer::read(uint8_t* dst, const std::size_t byte_count) -> std::size_t
{
    const std::size_t can_read_count = std::min(size_available_for_read(), byte_count);
    if (can_read_count == 0) {
        return 0;
    }
    const std::size_t max_count_before_wrap = m_max_size - m_read_offset;
    const std::size_t count_before_wrap     = std::min(can_read_count, max_count_before_wrap);
    const std::size_t count_after_wrap      = (count_before_wrap < can_read_count) ? (can_read_count - count_before_wrap) : 0;
    memcpy(dst, &m_buffer[m_read_offset], count_before_wrap);
    if (count_after_wrap > 0) {
        memcpy(dst + count_before_wrap, &m_buffer[0], count_after_wrap);
    }

    m_read_offset = (m_read_offset + can_read_count) % m_max_size;
    m_full        = false;
    return can_read_count;
}

auto Ring_buffer::peek(uint8_t* dst, const std::size_t byte_count) -> std::size_t
{
    const std::size_t can_read_count = std::min(size_available_for_read(), byte_count);
    if (can_read_count == 0) {
        return 0;
    }
    const std::size_t max_count_before_wrap = m_max_size - m_read_offset;
    const std::size_t count_before_wrap     = std::min(can_read_count, max_count_before_wrap);
    const std::size_t count_after_wrap      = (count_before_wrap < can_read_count) ? (can_read_count - count_before_wrap) : 0;
    memcpy(dst, &m_buffer[m_read_offset], count_before_wrap);
    if (count_after_wrap > 0) {
        memcpy(dst + count_before_wrap, &m_buffer[0], count_after_wrap);
    }

    return can_read_count;
}

auto Ring_buffer::discard(const std::size_t byte_count) -> std::size_t
{
    const std::size_t can_discard_count = std::min(size_available_for_read(), byte_count);
    if (can_discard_count == 0) {
        return 0;
    }
    m_read_offset = (m_read_offset + can_discard_count) % m_max_size;
    m_full        = false;
    return can_discard_count;
}

} // namespace erhe::net
