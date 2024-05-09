#include "erhe_raytrace/null/null_buffer.hpp"
#include "erhe_verify/verify.hpp"

#include <mutex>

namespace erhe::raytrace
{

auto IBuffer::create(const std::string_view debug_label, const std::size_t capacity_bytes_count) -> IBuffer*
{
    return new Null_buffer(debug_label, capacity_bytes_count);
}

auto IBuffer::create_shared(const std::string_view debug_label, const std::size_t capacity_bytes_count) -> std::shared_ptr<IBuffer>
{
    return std::make_shared<Null_buffer>(debug_label, capacity_bytes_count);
}

auto IBuffer::create_unique(const std::string_view debug_label, const std::size_t capacity_bytes_count) -> std::unique_ptr<IBuffer>
{
    return std::make_unique<Null_buffer>(debug_label, capacity_bytes_count);
}

Null_buffer::Null_buffer(const std::string_view debug_label, const std::size_t capacity_bytes_count)
    : m_capacity_byte_count{capacity_bytes_count}
    , m_next_free_byte     {0}
    , m_debug_label        {debug_label}
{
    ERHE_VERIFY(capacity_bytes_count > 0);
    m_buffer.resize(capacity_bytes_count);
    m_span = std::span<std::byte>(m_buffer.data(), m_buffer.size());
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
    m_span                = std::span<std::byte>(m_buffer.data(), m_buffer.size());
    return *this;
}

Null_buffer::~Null_buffer() noexcept
{
}

auto Null_buffer::capacity_byte_count() const noexcept
-> std::size_t
{
    return m_capacity_byte_count;
}

auto Null_buffer::allocate_bytes(
    const std::size_t byte_count,
    const std::size_t alignment
) noexcept -> std::size_t
{
    const std::lock_guard<std::mutex> lock{m_allocate_mutex};

    while ((m_next_free_byte % alignment) != 0)
    {
        ++m_next_free_byte;
    }
    const auto offset = m_next_free_byte;
    m_next_free_byte += byte_count;
    ERHE_VERIFY(m_next_free_byte <= m_capacity_byte_count);

    return offset;
}

auto Null_buffer::span() noexcept -> std::span<std::byte>
{
    return m_span;
}

auto Null_buffer::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace
