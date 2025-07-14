#pragma once

#include "erhe_verify/verify.hpp"

namespace erhe::utility {

inline auto align_offset_power_of_two(std::size_t offset, std::size_t alignment) -> std::size_t
{
    ERHE_VERIFY((alignment & (alignment - 1)) == 0);
    return (alignment == 0) ? offset : (offset + alignment - 1) & ~(alignment - 1);
}

inline auto align_offset_non_power_of_two(std::size_t offset, std::size_t alignment) -> std::size_t
{
    // Most basic code:
    //
    // std::size_t a = offset;
    // while ((offset % alignment) != 0) {
    //     ++offset;
    // }
    // return offset;

    // NOTE: This only works for power of two alignments
    // return (alignment == 0) ? offset : (offset + alignment - 1) & ~(alignment - 1);

    if (alignment == 0) {
        return offset;
    }
    const std::size_t remainder = offset % alignment;
    if (remainder == 0) {
        return offset;
    }
    return offset + alignment - remainder;
}

[[nodiscard]] inline auto next_power_of_two(uint32_t x) -> uint32_t
{
    x--;
    x |= x >> 1u;  // handle  2 bit numbers
    x |= x >> 2u;  // handle  4 bit numbers
    x |= x >> 4u;  // handle  8 bit numbers
    x |= x >> 8u;  // handle 16 bit numbers
    x |= x >> 16u; // handle 32 bit numbers
    x++;

    return x;
}

[[nodiscard]] inline auto next_power_of_two_16bit(std::size_t x) -> std::size_t
{
    ERHE_VERIFY(x < 65536);
    x--;
    x |= x >> 1u;  // handle  2 bit numbers
    x |= x >> 2u;  // handle  4 bit numbers
    x |= x >> 4u;  // handle  8 bit numbers
    x |= x >> 8u;  // handle 16 bit numbers
    x++;
    return x;
}

} // namespace erhe::utility
