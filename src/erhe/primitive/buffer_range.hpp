#pragma once

#include <cstddef>

namespace erhe::primitive
{

class Buffer_range
{
public:
    std::size_t count       {0};
    std::size_t element_size{0};
    std::size_t byte_offset {0};
};

} // namespace erhe::primitive
