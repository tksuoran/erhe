#pragma once

#include <array>

namespace erhe::graphics
{

class Texture;
class Sampler;

struct Texture_unit_bindings
{
    Texture* texture{nullptr};
    Sampler* sampler{nullptr};
};

auto operator==(const Texture_unit_bindings& lhs, const Texture_unit_bindings& rhs) noexcept
-> bool;

auto operator!=(const Texture_unit_bindings& lhs, const Texture_unit_bindings& rhs) noexcept
-> bool;

struct Texture_bindings
{
    std::array<Texture_unit_bindings, 16> texture_units;
};

struct Texture_bindings_hash
{
    auto operator()(const Texture_bindings& texture_bindings) const noexcept
    -> size_t;
};

auto operator==(const Texture_bindings& lhs, const Texture_bindings& rhs) noexcept
-> bool;

auto operator!=(const Texture_bindings& lhs, const Texture_bindings& rhs) noexcept
-> bool;

} // namespace erhe::graphics
