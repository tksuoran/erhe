#include "erhe/raytrace/embree/embree_buffer.hpp"
#include "erhe/raytrace/embree/embree_device.hpp"
#include "erhe/raytrace/log.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::raytrace
{

auto IBuffer::create(const std::string_view debug_label, const std::size_t capacity_byte_count) -> IBuffer*
{
    return new Embree_buffer(debug_label, capacity_byte_count);
}

auto IBuffer::create_shared(const std::string_view debug_label, const std::size_t capacity_byte_count) -> std::shared_ptr<IBuffer>
{
    return std::make_shared<Embree_buffer>(debug_label, capacity_byte_count);
}

auto IBuffer::create_unique(const std::string_view debug_label, const std::size_t capacity_byte_count) -> std::unique_ptr<IBuffer>
{
    return std::make_unique<Embree_buffer>(debug_label, capacity_byte_count);
}

Embree_buffer::Embree_buffer(const std::string_view debug_label, const std::size_t capacity_byte_count)
    : m_debug_label{debug_label}
{
    m_buffer = rtcNewBuffer(
        Embree_device::get_instance().get_rtc_device(),
        capacity_byte_count
    );
    SPDLOG_LOGGER_TRACE(log_embree, "rtcNewBuffer(byteSize = {}) = {} {}", capacity_byte_count, debug_label, fmt::ptr(m_buffer));
    m_capacity_byte_count = capacity_byte_count;
}

Embree_buffer::~Embree_buffer() noexcept
{
    SPDLOG_LOGGER_TRACE(log_embree, "rtcReleaseBuffer(hbuffer = {} {)}", m_debug_label, fmt::ptr(m_buffer));
    rtcReleaseBuffer(m_buffer);
}

auto Embree_buffer::capacity_byte_count() const noexcept -> std::size_t
{
    return m_capacity_byte_count;
}

auto Embree_buffer::allocate_bytes(
    const std::size_t byte_count,
    const std::size_t alignment
) noexcept -> std::size_t
{
    while ((m_next_free_byte % alignment) != 0)
    {
        ++m_next_free_byte;
    }
    const auto offset = m_next_free_byte;
    m_next_free_byte += byte_count;
    ERHE_VERIFY(m_next_free_byte <= m_capacity_byte_count);

    return offset;
}

auto Embree_buffer::span() noexcept -> gsl::span<std::byte>
{
    auto* const ptr = reinterpret_cast<std::byte*>(rtcGetBufferData(m_buffer));
    SPDLOG_LOGGER_TRACE(log_embree, "rtcGetBufferData(hbuffer = {} {})", m_debug_label, fmt::ptr(ptr));
    return gsl::span<std::byte>{
        ptr,
        m_capacity_byte_count
    };
}

auto Embree_buffer::get_rtc_buffer() const -> RTCBuffer
{
    return m_buffer;
}

auto Embree_buffer::debug_label() const -> std::string_view
{
    return m_debug_label;
}

}
