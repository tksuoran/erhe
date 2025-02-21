#include "ibuffer.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::buffer {

Cpu_buffer::Cpu_buffer(const std::string_view debug_label, const std::size_t capacity_bytes_count)
    : m_capacity_byte_count{capacity_bytes_count}
    , m_next_free_byte     {0}
    , m_debug_label        {debug_label}
{
    ERHE_VERIFY(capacity_bytes_count > 0);
    m_buffer.resize(capacity_bytes_count);
    m_span = std::span<std::byte>(m_buffer.data(), m_buffer.size());
}

Cpu_buffer::Cpu_buffer(Cpu_buffer&& other) noexcept
    : m_capacity_byte_count{other.m_capacity_byte_count}
    , m_next_free_byte     {other.m_next_free_byte}
    , m_buffer             {std::move(other.m_buffer)}
    , m_span               {m_buffer.data(), m_buffer.size()}
{
}

Cpu_buffer& Cpu_buffer::operator=(Cpu_buffer&& other) noexcept
{
    m_capacity_byte_count = other.m_capacity_byte_count;
    m_next_free_byte      = other.m_next_free_byte;
    m_buffer              = std::move(other.m_buffer);
    m_span                = std::span<std::byte>(m_buffer.data(), m_buffer.size());
    return *this;
}

Cpu_buffer::~Cpu_buffer() noexcept = default;

auto Cpu_buffer::capacity_byte_count() const noexcept -> std::size_t
{
    return m_capacity_byte_count;
}

auto Cpu_buffer::allocate_bytes(const std::size_t byte_count, const std::size_t alignment) noexcept -> std::size_t
{
    ERHE_VERIFY(alignment > 0);

    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_allocate_mutex};

    while ((m_next_free_byte % alignment) != 0) {
        ++m_next_free_byte;
    }
    const auto offset = m_next_free_byte;
    m_next_free_byte += byte_count;
    ERHE_VERIFY(m_next_free_byte <= m_capacity_byte_count);

    return offset;
}

auto Cpu_buffer::span() noexcept -> std::span<std::byte>
{
    return m_span;
}

auto Cpu_buffer::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace
