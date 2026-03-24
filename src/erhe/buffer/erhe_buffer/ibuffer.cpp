#include "erhe_buffer/ibuffer.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::buffer {

IBuffer::~IBuffer() noexcept = default;

Cpu_buffer::Cpu_buffer(const std::string_view debug_label, const std::size_t capacity_bytes_count, const std::size_t tail_padding)
    : m_allocator  {capacity_bytes_count}
    , m_debug_label{debug_label}
{
    ERHE_VERIFY(capacity_bytes_count > 0);
    m_buffer.resize(capacity_bytes_count + tail_padding);
    m_span = std::span<std::byte>(m_buffer.data(), capacity_bytes_count);
}

Cpu_buffer::Cpu_buffer(Cpu_buffer&& other) noexcept
    : m_allocator  {std::move(other.m_allocator)}
    , m_buffer     {std::move(other.m_buffer)}
    , m_span       {m_buffer.data(), m_buffer.size()}
    , m_debug_label{std::move(other.m_debug_label)}
{
}

Cpu_buffer& Cpu_buffer::operator=(Cpu_buffer&& other) noexcept
{
    m_allocator   = std::move(other.m_allocator);
    m_buffer      = std::move(other.m_buffer);
    m_span        = std::span<std::byte>(m_buffer.data(), m_buffer.size());
    m_debug_label = std::move(other.m_debug_label);
    return *this;
}

Cpu_buffer::~Cpu_buffer() noexcept = default;

auto Cpu_buffer::get_capacity_byte_count() const noexcept -> std::size_t
{
    return m_allocator.get_capacity();
}

auto Cpu_buffer::allocate_bytes(const std::size_t byte_count, const std::size_t alignment) noexcept -> std::optional<std::size_t>
{
    return m_allocator.allocate(byte_count, alignment);
}

void Cpu_buffer::free_bytes(const std::size_t byte_offset, const std::size_t byte_count) noexcept
{
    m_allocator.free(byte_offset, byte_count);
}

auto Cpu_buffer::get_span() noexcept -> std::span<std::byte>
{
    return m_span;
}

auto Cpu_buffer::get_debug_label() const -> std::string_view
{
    return m_debug_label;
}

auto Cpu_buffer::get_used_byte_count() const -> std::size_t
{
    return m_allocator.get_used();
}

auto Cpu_buffer::get_available_byte_count(const std::size_t alignment) const -> std::size_t
{
    static_cast<void>(alignment);
    return m_allocator.get_free();
}

auto Cpu_buffer::get_allocator() -> Free_list_allocator&
{
    return m_allocator;
}

} // namespace erhe::buffer
