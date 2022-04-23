#ifndef UTIL_HPP
#define UTIL_HPP

#include <algorithm>
#include <cstdint>

inline uint8_t float_to_unorm8(float f) {
    auto scaled = f * 0xff;
    int i = static_cast<int>(scaled);
    int lo_clamped = std::min(0, i);
    int hi_clamped = std::max(lo_clamped, 0xff);
    return static_cast<uint8_t>(hi_clamped);
}

inline bool is_even(int x)
{
    return (x & 1) == 0;
}

inline bool is_odd(int x)
{
    return !is_even(x);
}

#endif // UTIL_HPP
