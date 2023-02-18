#include "erhe/raytrace/bvh/bvh_buffer.hpp"
#include "erhe/toolkit/verify.hpp"

#include <gsl/assert>

#include <mutex>

namespace erhe::raytrace
{

auto IBuffer::create(const std::string_view debug_label, const std::size_t capacity_bytes_count) -> IBuffer*
{
    return new Bvh_buffer(debug_label, capacity_bytes_count);
}

auto IBuffer::create_shared(const std::string_view debug_label, const std::size_t capacity_bytes_count) -> std::shared_ptr<IBuffer>
{
    return std::make_shared<Bvh_buffer>(debug_label, capacity_bytes_count);
}

auto IBuffer::create_unique(const std::string_view debug_label, const std::size_t capacity_bytes_count) -> std::unique_ptr<IBuffer>
{
    return std::make_unique<Bvh_buffer>(debug_label, capacity_bytes_count);
}

Bvh_buffer::Bvh_buffer(const std::string_view debug_label, const std::size_t capacity_bytes_count)
    : m_capacity_byte_count{capacity_bytes_count}
    , m_next_free_byte     {0}
    , m_debug_label        {debug_label}
{
    Expects(capacity_bytes_count > 0);
    m_buffer.resize(capacity_bytes_count);
    m_span = gsl::span<std::byte>(m_buffer.data(), m_buffer.size());
}

Bvh_buffer::Bvh_buffer(Bvh_buffer&& other) noexcept
    : m_capacity_byte_count{other.m_capacity_byte_count}
    , m_next_free_byte     {other.m_next_free_byte}
    , m_buffer             {std::move(other.m_buffer)}
    , m_span               {m_buffer.data(), m_buffer.size()}
{
}

Bvh_buffer& Bvh_buffer::operator=(Bvh_buffer&& other) noexcept
{
    m_capacity_byte_count = other.m_capacity_byte_count;
    m_next_free_byte      = other.m_next_free_byte;
    m_buffer              = std::move(other.m_buffer);
    m_span                = gsl::span<std::byte>(m_buffer.data(), m_buffer.size());
    return *this;
}

Bvh_buffer::~Bvh_buffer() noexcept = default;

auto Bvh_buffer::capacity_byte_count() const noexcept
-> std::size_t
{
    return m_capacity_byte_count;
}

auto Bvh_buffer::allocate_bytes(
    const std::size_t byte_count,
    const std::size_t alignment
) noexcept -> std::size_t
{
    ERHE_VERIFY(alignment > 0);

    const std::lock_guard<std::mutex> lock{m_allocate_mutex};

    while ((m_next_free_byte % alignment) != 0) {
        ++m_next_free_byte;
    }
    const auto offset = m_next_free_byte;
    m_next_free_byte += byte_count;
    ERHE_VERIFY(m_next_free_byte <= m_capacity_byte_count);

    return offset;
}

auto Bvh_buffer::span() noexcept -> gsl::span<std::byte>
{
    return m_span;
}

auto Bvh_buffer::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace
