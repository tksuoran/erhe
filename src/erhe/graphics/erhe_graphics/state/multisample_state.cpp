#include "erhe_graphics/state/multisample_state.hpp"

#include <bitset>

namespace erhe::graphics {

auto Multisample_state_hash::operator()(const Multisample_state& multisample_state) noexcept -> std::size_t
{
    return
        (
            (multisample_state.sample_shading_enable    ? 1u : 0u) |
            (multisample_state.alpha_to_coverage_enable ? 2u : 0u) |
            (multisample_state.alpha_to_one_enable      ? 4u : 0u)
        ) ^
        (
            std::hash<float>{}(multisample_state.min_sample_shading)
        );
}

auto operator==(const Multisample_state& lhs, const Multisample_state& rhs) noexcept -> bool
{
    return
        (lhs.sample_shading_enable    == rhs.sample_shading_enable   ) &&
        (lhs.alpha_to_coverage_enable == rhs.alpha_to_coverage_enable) &&
        (lhs.alpha_to_one_enable      == rhs.alpha_to_one_enable     ) &&
        (lhs.min_sample_shading       == rhs.min_sample_shading);
}

auto operator!=(const Multisample_state& lhs, const Multisample_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
