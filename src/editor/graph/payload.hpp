#pragma once

#include "erhe_dataformat/dataformat.hpp"

#include <array>

namespace editor {

class Payload
{
public:
    auto operator+=(const Payload& rhs) -> Payload&;
    auto operator-=(const Payload& rhs) -> Payload&;
    auto operator*=(const Payload& rhs) -> Payload&;
    auto operator/=(const Payload& rhs) -> Payload&;

    erhe::dataformat::Format format;
    std::array<int, 4>       int_value;
    std::array<float, 4>     float_value;
};

auto operator+(const Payload& lhs, const Payload& rhs) -> Payload;
auto operator-(const Payload& lhs, const Payload& rhs) -> Payload;
auto operator*(const Payload& lhs, const Payload& rhs) -> Payload;
auto operator/(const Payload& lhs, const Payload& rhs) -> Payload;

} // namespace editor
