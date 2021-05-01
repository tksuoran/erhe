#include "erhe/graphics_experimental/texture_bindings.hpp"

#include "erhe/graphics/texture.hpp"
#include "erhe/graphics/sampler.hpp"

namespace erhe::graphics
{

auto operator==(const Texture_unit_bindings& lhs, const Texture_unit_bindings& rhs) noexcept
-> bool
{
    return (lhs.texture == rhs.texture) &&
           (lhs.sampler == rhs.sampler);
}

auto operator!=(const Texture_unit_bindings& lhs, const Texture_unit_bindings& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

auto Texture_bindings_hash::operator()(const Texture_bindings& texture_bindings) const noexcept
-> size_t
{
    size_t result = 40000000000001;
    for (auto entry : texture_bindings.texture_units)
    {
        size_t texture_hash = (entry.texture != nullptr) ? erhe::graphics::Texture_hash{}(*entry.texture) : 0u;
        size_t sampler_hash = (entry.sampler != nullptr) ? erhe::graphics::Sampler_hash{}(*entry.sampler) : 0u;
        result ^= (texture_hash +  700000001u) *       7000000001u;
        result ^= (sampler_hash + 6000000001u) * 6000000000000001u;
    }
    return result;
}

auto operator==(const Texture_bindings& lhs, const Texture_bindings& rhs) noexcept
-> bool
{
    return lhs.texture_units == rhs.texture_units;
}

auto operator!=(const Texture_bindings& lhs, const Texture_bindings& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
