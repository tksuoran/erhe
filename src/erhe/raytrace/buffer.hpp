#pragma once

#include <embree3/rtcore.h>

#include <gsl/span>

#include <mutex>

namespace erhe::raytrace
{

class Buffer
{
public:
    explicit Buffer(const size_t capacity_bytes_count);
    Buffer(const Buffer&)             = delete;
    explicit Buffer(Buffer&& other);
    Buffer& operator=(const Buffer&)  = delete;
    Buffer& operator=(Buffer&& other);
    ~Buffer();

    auto capacity_byte_count() const noexcept -> size_t;
    auto allocate_bytes     (const size_t byte_count, const size_t alignment = 64) noexcept -> size_t;
    auto span               () noexcept -> gsl::span<std::byte>;

private:
    //RTCBuffer  m_buffer{nullptr};
    std::mutex             m_allocate_mutex;
    size_t                 m_capacity_byte_count{0};
    size_t                 m_next_free_byte     {0};

    std::vector<std::byte> m_buffer;
    gsl::span<std::byte>   m_span;
};

} // namespace erhe::raytrace
