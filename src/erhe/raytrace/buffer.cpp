#include "erhe/raytrace/buffer.hpp"
#include "erhe/raytrace/device.hpp"
#include "erhe/toolkit/verify.hpp"

//#include <embree3/rtcore.h>
#include <mutex>

#include <gsl/gsl_assert>

namespace erhe::raytrace
{

class Buffer
    : public IBuffer
{
public:
    explicit Buffer(const size_t capacity_bytes_count);
    Buffer(const Buffer&)             = delete;
    explicit Buffer(Buffer&& other) noexcept;
    Buffer& operator=(const Buffer&)  = delete;
    Buffer& operator=(Buffer&& other) noexcept;
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

auto IBuffer::create(const size_t capacity_bytes_count) -> IBuffer*
{
    return new Buffer(capacity_bytes_count);
}

auto IBuffer::create_shared(const size_t capacity_bytes_count) -> std::shared_ptr<IBuffer>
{
    return std::make_shared<Buffer>(capacity_bytes_count);
}

Buffer::Buffer(const size_t capacity_bytes_count)
{
    Expects(capacity_bytes_count > 0);
    m_buffer.resize(capacity_bytes_count);
    m_span = gsl::span<std::byte>(m_buffer.data(), m_buffer.size());
    //rtcNewBuffer(device.get_rtc_device(), capacity_bytes_count);
    //rtcNewSharedBuffer(device.get_rtc_device(), data, byte_count);
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_capacity_byte_count{other.m_capacity_byte_count}
    , m_next_free_byte     {other.m_next_free_byte}
    , m_buffer             {std::move(other.m_buffer)}
    , m_span               {m_buffer.data(), m_buffer.size()}
{
}

Buffer& Buffer::operator=(Buffer&& other) noexcept
{
    m_capacity_byte_count = other.m_capacity_byte_count;
    m_next_free_byte      = other.m_next_free_byte;
    m_buffer              = std::move(other.m_buffer);
    m_span                = gsl::span<std::byte>(m_buffer.data(), m_buffer.size());
    return *this;
}

Buffer::~Buffer()
{
    //rtcReleaseBuffer(m_buffer);
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

auto Buffer::span() noexcept -> gsl::span<std::byte>
{
    return m_span;
}

// void* rtcGetBufferData(RTCBuffer buffer);


} // namespace
