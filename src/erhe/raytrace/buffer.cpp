#include "erhe/raytrace/buffer.hpp"
#include "erhe/raytrace/device.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::raytrace
{

Buffer::Buffer(const Device& device, const size_t capacity_bytes_count)
{
    rtcNewBuffer(device.get_rtc_device(), capacity_bytes_count);
    //rtcNewSharedBuffer(device.get_rtc_device(), data, byte_count);
}

Buffer::~Buffer()
{
    rtcReleaseBuffer(m_buffer);
}

auto Buffer::capacity_byte_count() const noexcept
-> size_t
{
    return m_capacity_byte_count;
}

auto Buffer::allocate_bytes(const size_t byte_count, const size_t alignment) noexcept
-> size_t
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

// void* rtcGetBufferData(RTCBuffer buffer);


} // namespace
