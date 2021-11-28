#include "erhe/raytrace/null/null_buffer.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/assert>

#include <mutex>

namespace erhe::raytrace
{

auto IBuffer::create(const size_t capacity_bytes_count) -> IBuffer*
{
    return new Null_buffer(capacity_bytes_count);
}

auto IBuffer::create_shared(const size_t capacity_bytes_count) -> std::shared_ptr<IBuffer>
{
    return std::make_shared<Null_buffer>(capacity_bytes_count);
}

Null_buffer::Null_buffer(const size_t capacity_bytes_count)
{
    Expects(capacity_bytes_count > 0);
    m_buffer.resize(capacity_bytes_count);
    m_span = gsl::span<std::byte>(m_buffer.data(), m_buffer.size());
    //rtcNewBuffer(device.get_rtc_device(), capacity_bytes_count);
    //rtcNewSharedBuffer(device.get_rtc_device(), data, byte_count);
}

Null_buffer::Null_buffer(Null_buffer&& other) noexcept
    : m_capacity_byte_count{other.m_capacity_byte_count}
    , m_next_free_byte     {other.m_next_free_byte}
    , m_buffer             {std::move(other.m_buffer)}
    , m_span               {m_buffer.data(), m_buffer.size()}
{
}

Null_buffer& Null_buffer::operator=(Null_buffer&& other) noexcept
{
    m_capacity_byte_count = other.m_capacity_byte_count;
    m_next_free_byte      = other.m_next_free_byte;
    m_buffer              = std::move(other.m_buffer);
    m_span                = gsl::span<std::byte>(m_buffer.data(), m_buffer.size());
    return *this;
}

Null_buffer::~Null_buffer() = default;

auto Null_buffer::capacity_byte_count() const noexcept
-> size_t
{
    return m_capacity_byte_count;
}

auto Null_buffer::allocate_bytes(
    const size_t byte_count,
    const size_t alignment
) noexcept -> size_t
{
    std::lock_guard<std::mutex> lock(m_allocate_mutex);

    while ((m_next_free_byte % alignment) != 0)
    {
        ++m_next_free_byte;
    }
    const auto offset = m_next_free_byte;
    m_next_free_byte += byte_count;
    VERIFY(m_next_free_byte <= m_capacity_byte_count);

    return offset;
}

auto Null_buffer::span() noexcept -> gsl::span<std::byte>
{
    return m_span;
}

} // namespace
