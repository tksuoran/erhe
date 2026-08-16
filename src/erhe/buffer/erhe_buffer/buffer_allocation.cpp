#include "erhe_buffer/buffer_allocation.hpp"

namespace erhe::buffer {

Buffer_allocation::Buffer_allocation()
    : m_owner      {nullptr}
    , m_byte_offset{0}
    , m_byte_count {0}
{
}

Buffer_allocation::Buffer_allocation(
    Buffer_allocation_owner& owner,
    std::size_t              byte_offset,
    std::size_t              byte_count
)
    : m_owner      {&owner}
    , m_byte_offset{byte_offset}
    , m_byte_count {byte_count}
{
}

Buffer_allocation::~Buffer_allocation()
{
    release();
}

Buffer_allocation::Buffer_allocation(Buffer_allocation&& other) noexcept
    : m_owner      {other.m_owner}
    , m_byte_offset{other.m_byte_offset}
    , m_byte_count {other.m_byte_count}
{
    other.m_owner       = nullptr;
    other.m_byte_offset = 0;
    other.m_byte_count  = 0;
}

Buffer_allocation& Buffer_allocation::operator=(Buffer_allocation&& other) noexcept
{
    if (this != &other) {
        release();
        m_owner             = other.m_owner;
        m_byte_offset       = other.m_byte_offset;
        m_byte_count        = other.m_byte_count;
        other.m_owner       = nullptr;
        other.m_byte_offset = 0;
        other.m_byte_count  = 0;
    }
    return *this;
}

void Buffer_allocation::release()
{
    if ((m_owner != nullptr) && (m_byte_count > 0)) {
        m_owner->release_allocation(m_byte_offset, m_byte_count);
        m_owner = nullptr;
    }
}

auto Buffer_allocation::get_byte_offset() const -> std::size_t
{
    return m_byte_offset;
}

auto Buffer_allocation::get_byte_count() const -> std::size_t
{
    return m_byte_count;
}

auto Buffer_allocation::is_valid() const -> bool
{
    return m_owner != nullptr;
}

} // namespace erhe::buffer
