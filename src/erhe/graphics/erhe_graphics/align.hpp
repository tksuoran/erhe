#pragma once

namespace erhe::graphics {

inline auto align_offset_(std::size_t offset, std::size_t alignment) -> std::size_t
{
    return (alignment == 0) 
        ? offset 
        : (offset + alignment - 1) & ~(alignment - 1);
}

inline auto align_offset(std::size_t offset, std::size_t alignment) -> std::size_t
{
    std::size_t aligned_offset = align_offset_(offset, alignment);
    if (aligned_offset < offset) {
        static int counter = 0;
        ++counter;
    }
    if (alignment > 1024) {
        static int counter2 = 0;
        ++counter2;
    }
    return aligned_offset;
}

} // namespace erhe::graphics
