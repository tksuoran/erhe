#pragma once

namespace erhe::utility {

template <typename T>
auto test_bit_set(T lhs, T rhs) -> bool
{
    return (lhs & rhs) == rhs;
}

template <typename T>
auto test_all_rhs_bits_set(T lhs, T rhs) -> bool
{
    return (lhs & rhs) == rhs;
}

template <typename T>
auto test_any_rhs_bits_set(T lhs, T rhs) -> bool
{
    return (lhs & rhs) != T{0};
}

} // namespace erhe::utility
