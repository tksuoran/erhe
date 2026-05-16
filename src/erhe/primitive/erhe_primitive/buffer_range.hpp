#pragma once

#include <cstddef>

namespace erhe::graphics { class Buffer; }

namespace erhe::primitive {

class Buffer_range
{
public:
    [[nodiscard]] auto get_byte_size() const -> std::size_t { return count * element_size; }

    std::size_t count       {0};
    std::size_t element_size{0};
    std::size_t byte_offset {0};
    std::size_t pool_id     {0};
    std::size_t buffer_id   {0};
};

} // namespace erhe::primitive
