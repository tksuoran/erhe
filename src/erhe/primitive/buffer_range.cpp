#include "erhe/primitive/buffer_range.hpp"

namespace erhe::primitive
{

Buffer_range::Buffer_range() = default;

Buffer_range::Buffer_range(size_t count, size_t element_size, size_t byte_offset)
    : count       {count}
    , element_size{element_size}
    , byte_offset {byte_offset}
{
}

Buffer_range::Buffer_range(const Buffer_range& other)
    : count       {other.count}
    , element_size{other.element_size}
    , byte_offset {other.byte_offset}
{
}

auto Buffer_range::operator=(const Buffer_range& other) -> Buffer_range&
{
    count        = other.count;
    element_size = other.element_size;
    byte_offset  = other.byte_offset;
    return *this;
}

Buffer_range::Buffer_range(const Buffer_range&& other) noexcept
    : count       {other.count}
    , element_size{other.element_size}
    , byte_offset {other.byte_offset}
{
}

auto Buffer_range::operator=(const Buffer_range&& other) noexcept -> Buffer_range& 
{
    byte_offset  = other.byte_offset;
    element_size = other.element_size;
    count        = other.count;
    return *this;
}

} // namespace erhe::primitive
