#pragma once

#include "erhe/gl/enum_base_zero_functions.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

namespace erhe::graphics
{

class Blend_state_component
{
public:
    gl::Blend_equation_mode equation_mode     {gl::Blend_equation_mode::func_add};
    gl::Blending_factor     source_factor     {gl::Blending_factor::one};
    gl::Blending_factor     destination_factor{gl::Blending_factor::zero};
};

class Blend_state_component_hash
{
public:
    [[nodiscard]] auto operator()(
        const Blend_state_component& blend_state_component
    ) const noexcept -> size_t
    {
        return
            (gl::base_zero(blend_state_component.equation_mode     ) << 0u) | // 3 bits
            (gl::base_zero(blend_state_component.source_factor     ) << 3u) | // 5 bits
            (gl::base_zero(blend_state_component.destination_factor) << 8u);  // 5 bits
    }
};

[[nodiscard]] auto operator==(
    const Blend_state_component& lhs,
    const Blend_state_component& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Blend_state_component& lhs,
    const Blend_state_component& rhs
) noexcept -> bool;

class Color_write_mask
{
public:
    bool red  {true};
    bool green{true};
    bool blue {true};
    bool alpha{true};
};

class Color_blend_state
{
public:
    bool                  enabled{false};
    Blend_state_component rgb;
    Blend_state_component alpha;
    float                 constant[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    Color_write_mask      write_mask;

    static Color_blend_state color_blend_disabled;
    static Color_blend_state color_blend_premultiplied;
    static Color_blend_state color_writes_disabled;
};

class Blend_state_hash
{
public:
    auto operator()(const Color_blend_state& state) const noexcept -> size_t;
};

[[nodiscard]] auto operator==(
    const Color_blend_state& lhs,
    const Color_blend_state& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Color_blend_state& lhs,
    const Color_blend_state& rhs
) noexcept -> bool;

class Color_blend_state_tracker
{
public:
    void reset  ();
    void execute(const Color_blend_state& state) noexcept;

private:
    size_t            m_last{0};
    Color_blend_state m_cache;
};

} // namespace erhe::graphics
