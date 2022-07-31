#pragma once

namespace erhe::toolkit
{

template <typename T>
auto test_all_rhs_bits_set(T lhs, T rhs) -> bool
{
    return (lhs & rhs) == rhs;
}

}