#pragma once

#include <array>

namespace erhe::graphics
{

class Texture;
class Sampler;

class Texture_unit_bindings
{
public:
    Texture* texture{nullptr};
    Sampler* sampler{nullptr};
};

auto operator==(const Texture_unit_bindings& lhs, const Texture_unit_bindings& rhs) noexcept
-> bool;

auto operator!=(const Texture_unit_bindings& lhs, const Texture_unit_bindings& rhs) noexcept
-> bool;

class Texture_bindings
{
public:
    std::array<Texture_unit_bindings, 16> texture_units;
};

class Texture_bindings_hash
{
public:
    auto operator()(const Texture_bindings& texture_bindings) const noexcept
    -> size_t;
};

auto operator==(const Texture_bindings& lhs, const Texture_bindings& rhs) noexcept
-> bool;

auto operator!=(const Texture_bindings& lhs, const Texture_bindings& rhs) noexcept
-> bool;

} // namespace erhe::graphics
