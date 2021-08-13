#pragma once

#include <embree3/rtcore.h>

#include <mutex>

namespace erhe::raytrace
{

class Device;

class Buffer
{
public:
    Buffer(const Device& device, const size_t capacity_bytes_count);
    Buffer(const Buffer&)            = delete;
    Buffer(Buffer&&)                 = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer& operator=(Buffer&&)      = delete;
    ~Buffer();

    auto capacity_byte_count() const noexcept -> size_t;
    auto allocate_bytes     (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t;

private:
    RTCBuffer  m_buffer{nullptr};
    std::mutex m_allocate_mutex;
    size_t     m_capacity_byte_count{0};
    size_t     m_next_free_byte     {0};
};

} // namespace erhe::raytrace
