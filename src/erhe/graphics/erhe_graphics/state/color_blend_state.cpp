#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_gl/enum_base_zero_functions.hpp"
#include "erhe_graphics/state/color_blend_state.hpp"

#define DISABLE_CACHE 0

namespace erhe::graphics
{

namespace
{

auto shuffle(
    const std::size_t x,
    const std::size_t m,
    const std::size_t shift
) -> std::size_t
{
    const std::size_t t = ((x >> shift) ^ x) & m;
    return (x ^ t) ^ (t << shift);
}

class Blend_state_component_hash
{
public:
    [[nodiscard]] auto operator()(const Blend_state_component& blend_state_component) const noexcept -> size_t
    {
        return
            (gl::base_zero(blend_state_component.equation_mode     ) << 0u) | // 3 bits
            (gl::base_zero(blend_state_component.source_factor     ) << 3u) | // 5 bits
            (gl::base_zero(blend_state_component.destination_factor) << 8u);  // 5 bits
    }
};

}

auto Blend_state_hash::operator()(
    const Color_blend_state& state
) const noexcept -> std::size_t
{
    return
        (
            (state.enabled ? 1u : 0u)                          | // 1 bit
            (Blend_state_component_hash{}(state.rgb  ) << 1u)  | // 13 bits
            (Blend_state_component_hash{}(state.alpha) << 14u)
        ) ^
        (
            std::hash<float>{}(state.constant[0])
        ) ^
        (
            shuffle(std::hash<float>{}(state.constant[1]), 0x2222222222222222ull, 1)
        ) ^
        (
            shuffle(std::hash<float>{}(state.constant[2]), 0x0c0c0c0c0c0c0c0cull, 2)
        ) ^
        (
            shuffle(std::hash<float>{}(state.constant[3]), 0x00f000f000f000f0ull, 4)
        ) ^
        (
            (state.write_mask.red   ? 1u : 0u) |
            (state.write_mask.green ? 2u : 0u) |
            (state.write_mask.blue  ? 4u : 0u) |
            (state.write_mask.alpha ? 8u : 0u)
        );
}

Color_blend_state Color_blend_state::color_blend_disabled;

Color_blend_state Color_blend_state::color_blend_premultiplied {
    .enabled = true,
    .rgb = {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    .constant = {0.0f, 0.0f, 0.0f, 0.0f},
    .write_mask = {
        .red   = true,
        .green = true,
        .blue  = true,
        .alpha = true
    }
};

Color_blend_state Color_blend_state::color_blend_premultiplied_alpha_replace {
    .enabled = true,
    .rgb = {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::zero
    },
    .constant = {0.0f, 0.0f, 0.0f, 0.0f},
    .write_mask = {
        .red   = true,
        .green = true,
        .blue  = true,
        .alpha = true
    }
};

Color_blend_state Color_blend_state::color_writes_disabled {
    .enabled = false,
    .rgb = {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    .constant = {0.0f, 0.0f, 0.0f, 0.0f},
    .write_mask = {
        .red   = false,
        .green = false,
        .blue  = false,
        .alpha = false
    }
};

void Color_blend_state_tracker::reset()
{
    gl::blend_color(0.0f, 0.0f, 0.0f, 0.0f);
    gl::blend_equation_separate(
        gl::Blend_equation_mode::func_add,
        gl::Blend_equation_mode::func_add
    );
    gl::blend_func_separate(
        gl::Blending_factor::one,
        gl::Blending_factor::zero,
        gl::Blending_factor::one,
        gl::Blending_factor::zero
    );
    gl::disable(gl::Enable_cap::blend);
    gl::color_mask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    m_cache = Color_blend_state{};
}

void Color_blend_state_tracker::execute(const Color_blend_state& state) noexcept
{
#if DISABLE_CACHE
    if (state.enabled) {
        gl::enable(gl::Enable_cap::blend);
        gl::blend_color(
            state.constant[0],
            state.constant[1],
            state.constant[2],
            state.constant[3]
        );
        gl::blend_equation_separate(state.rgb.equation_mode, state.alpha.equation_mode);
        gl::blend_func_separate(
            state.rgb.source_factor,
            state.rgb.destination_factor,
            state.alpha.source_factor,
            state.alpha.destination_factor
        );
    } else {
        gl::disable(gl::Enable_cap::blend);
    }

    gl::color_mask(
        state.write_mask.red   ? GL_TRUE : GL_FALSE,
        state.write_mask.green ? GL_TRUE : GL_FALSE,
        state.write_mask.blue  ? GL_TRUE : GL_FALSE,
        state.write_mask.alpha ? GL_TRUE : GL_FALSE
    );
#else
    if (state.enabled) {
        if (!m_cache.enabled) {
            gl::enable(gl::Enable_cap::blend);
            m_cache.enabled = true;
        }
        if (
            (m_cache.constant[0] != state.constant[0]) ||
            (m_cache.constant[1] != state.constant[1]) ||
            (m_cache.constant[2] != state.constant[2]) ||
            (m_cache.constant[3] != state.constant[3])
        ) {
            gl::blend_color(
                state.constant[0],
                state.constant[1],
                state.constant[2],
                state.constant[3]
            );
            m_cache.constant[0] = state.constant[0];
            m_cache.constant[1] = state.constant[1];
            m_cache.constant[2] = state.constant[2];
            m_cache.constant[3] = state.constant[3];
        }
        if (
            (m_cache.rgb.equation_mode   != state.rgb.equation_mode) ||
            (m_cache.alpha.equation_mode != state.alpha.equation_mode)
        ) {
            gl::blend_equation_separate(state.rgb.equation_mode, state.alpha.equation_mode);
            m_cache.rgb.equation_mode   = state.rgb.equation_mode;
            m_cache.alpha.equation_mode = state.alpha.equation_mode;
        }
        if (
            (m_cache.rgb.source_factor        != state.rgb.source_factor       ) ||
            (m_cache.rgb.destination_factor   != state.rgb.destination_factor  ) ||
            (m_cache.alpha.source_factor      != state.alpha.source_factor     ) ||
            (m_cache.alpha.destination_factor != state.alpha.destination_factor)
        ) {
            gl::blend_func_separate(
                state.rgb.source_factor,
                state.rgb.destination_factor,
                state.alpha.source_factor,
                state.alpha.destination_factor
            );
            m_cache.rgb.source_factor        = state.rgb.source_factor;
            m_cache.rgb.destination_factor   = state.rgb.destination_factor;
            m_cache.alpha.source_factor      = state.alpha.source_factor;
            m_cache.alpha.destination_factor = state.alpha.destination_factor;
        }
    } else {
        if (m_cache.enabled) {
            gl::disable(gl::Enable_cap::blend);
            m_cache.enabled = false;
        }
    }

    if (
        (m_cache.write_mask.red   != state.write_mask.red  ) ||
        (m_cache.write_mask.green != state.write_mask.green) ||
        (m_cache.write_mask.blue  != state.write_mask.blue ) ||
        (m_cache.write_mask.alpha != state.write_mask.alpha)
    ) {
        gl::color_mask(
            state.write_mask.red   ? GL_TRUE : GL_FALSE,
            state.write_mask.green ? GL_TRUE : GL_FALSE,
            state.write_mask.blue  ? GL_TRUE : GL_FALSE,
            state.write_mask.alpha ? GL_TRUE : GL_FALSE
        );
        m_cache.write_mask.red   = state.write_mask.red;
        m_cache.write_mask.green = state.write_mask.green;
        m_cache.write_mask.blue  = state.write_mask.blue;
        m_cache.write_mask.alpha = state.write_mask.alpha;
    }
#endif
}

auto operator==(
    const Blend_state_component& lhs,
    const Blend_state_component& rhs
) noexcept -> bool
{
    return
        (lhs.equation_mode      == rhs.equation_mode     ) &&
        (lhs.source_factor      == rhs.source_factor     ) &&
        (lhs.destination_factor == rhs.destination_factor);
}

auto operator!=(
    const Blend_state_component& lhs,
    const Blend_state_component& rhs
) noexcept -> bool
{
    return !(lhs == rhs);
}

auto operator==(
    const Color_blend_state& lhs,
    const Color_blend_state& rhs
) noexcept -> bool
{
    return
        (lhs.enabled          == rhs.enabled         ) &&
        (lhs.rgb              == rhs.rgb             ) &&
        (lhs.alpha            == rhs.alpha           ) &&
        (lhs.constant[0]      == rhs.constant[0]     ) &&
        (lhs.constant[1]      == rhs.constant[1]     ) &&
        (lhs.constant[2]      == rhs.constant[2]     ) &&
        (lhs.constant[3]      == rhs.constant[3]     ) &&
        (lhs.write_mask.red   == rhs.write_mask.red  ) &&
        (lhs.write_mask.green == rhs.write_mask.green) &&
        (lhs.write_mask.blue  == rhs.write_mask.blue ) &&
        (lhs.write_mask.alpha == rhs.write_mask.alpha);
}

auto operator!=(
    const Color_blend_state& lhs,
    const Color_blend_state& rhs
) noexcept -> bool
{
    return !(lhs == rhs);
}


} // namespace erhe::graphics
