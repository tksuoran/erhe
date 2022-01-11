#pragma once

#include <cstddef>

namespace erhe::primitive
{

class Buffer_range
{
public:
    size_t count       {0};
    size_t element_size{0};
    size_t byte_offset {0};
};

} // namespace erhe::primitive
