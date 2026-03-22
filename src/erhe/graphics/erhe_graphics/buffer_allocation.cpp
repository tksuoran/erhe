#include "erhe_graphics/buffer_allocation.hpp"
#include "erhe_graphics/free_list_allocator.hpp"

namespace erhe::graphics {

Buffer_allocation::Buffer_allocation()
    : m_allocator  {nullptr}
    , m_byte_offset{0}
    , m_byte_count {0}
{
}

Buffer_allocation::Buffer_allocation(
    Free_list_allocator& allocator,
    std::size_t          byte_offset,
    std::size_t          byte_count
)
    : m_allocator  {&allocator}
    , m_byte_offset{byte_offset}
    , m_byte_count {byte_count}
{
}

Buffer_allocation::~Buffer_allocation()
{
    release();
}

Buffer_allocation::Buffer_allocation(Buffer_allocation&& other) noexcept
    : m_allocator  {other.m_allocator}
    , m_byte_offset{other.m_byte_offset}
    , m_byte_count {other.m_byte_count}
{
    other.m_allocator   = nullptr;
    other.m_byte_offset = 0;
    other.m_byte_count  = 0;
}

Buffer_allocation& Buffer_allocation::operator=(Buffer_allocation&& other) noexcept
{
    if (this != &other) {
        release();
        m_allocator         = other.m_allocator;
        m_byte_offset       = other.m_byte_offset;
        m_byte_count        = other.m_byte_count;
        other.m_allocator   = nullptr;
        other.m_byte_offset = 0;
        other.m_byte_count  = 0;
    }
    return *this;
}

void Buffer_allocation::release()
{
    if ((m_allocator != nullptr) && (m_byte_count > 0)) {
        m_allocator->free(m_byte_offset, m_byte_count);
        m_allocator = nullptr;
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
    return m_allocator != nullptr;
}

} // namespace erhe::graphics
