#pragma once

#include <cstddef>

namespace erhe::graphics {

class Multisample_state
{
public:
    bool  sample_shading_enable   {false};
    bool  alpha_to_coverage_enable{false};
    bool  alpha_to_one_enable     {false};
    float min_sample_shading      {1.0f};
};

class Multisample_state_hash
{
public:
    [[nodiscard]] auto operator()(const Multisample_state& multisample_state) noexcept -> std::size_t;
};

[[nodiscard]] auto operator==(const Multisample_state& lhs, const Multisample_state& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Multisample_state& lhs, const Multisample_state& rhs) noexcept -> bool;

} // namespace erhe::graphics
