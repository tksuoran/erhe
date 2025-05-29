#pragma once

namespace erhe::graphics {

inline auto align_offset(std::size_t offset, std::size_t alignment) -> std::size_t
{
    return (alignment == 0) 
        ? offset 
        : (offset + alignment - 1) & ~(alignment - 1);
}

} // namespace erhe::graphics
