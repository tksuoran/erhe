#pragma once

#include <cstddef>

namespace erhe::primitive
{

class Buffer_range
{
public:
    Buffer_range();
    Buffer_range(size_t count, size_t element_size, size_t byte_offset);
    Buffer_range(const Buffer_range& other);
    Buffer_range& operator=(const Buffer_range& other);
    Buffer_range(const Buffer_range&& other) noexcept;
    Buffer_range& operator=(const Buffer_range&& other) noexcept;

    size_t element_size{0};
    size_t count       {0};
    size_t byte_offset {0};
};

} // namespace erhe::primitive
