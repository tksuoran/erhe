#include "erhe/gl/gl.hpp"
#include "erhe/graphics/state/color_blend_state.hpp"
#include "erhe/toolkit/verify.hpp"

// #define DISABLE_CACHE 1

namespace erhe::graphics
{

Color_blend_state::Color_blend_state()
    : serial{get_next_serial()}
{
}

Color_blend_state::Color_blend_state(
    bool                  enabled,
    Blend_state_component rgb,
    Blend_state_component alpha,
    glm::vec4             color,
    bool                  color_write_mask_red,
    bool                  color_write_mask_green,
    bool                  color_write_mask_blue,
    bool                  color_write_mask_alpha 
)
    : serial                {get_next_serial()}
    , enabled               {enabled}
    , rgb                   {rgb}
    , alpha                 {alpha}
    , color                 {color}
    , color_write_mask_red  {color_write_mask_red}
    , color_write_mask_green{color_write_mask_green}
    , color_write_mask_blue {color_write_mask_blue}
    , color_write_mask_alpha{color_write_mask_alpha}
{
}

void Color_blend_state::touch()
{
    serial = get_next_serial();
}

auto Color_blend_state::get_next_serial() -> size_t
{
    do
    {
        s_serial++;
    }
    while (s_serial == 0);
    return s_serial;
}

auto Blend_state_hash::operator()(
    const Color_blend_state& state
) const noexcept -> size_t
{
    return
        (
            (state.enabled ? 1u : 0u)                          | // 1 bit
            (Blend_state_component_hash{}(state.rgb  ) << 1u)  | // 13 bits
            (Blend_state_component_hash{}(state.alpha) << 14u)
        ) ^
        (
            std::hash<glm::vec4>{}(state.color)
        ) ^
        (
            (state.color_write_mask_red   ? 1u : 0u) |
            (state.color_write_mask_green ? 2u : 0u) |
            (state.color_write_mask_blue  ? 4u : 0u) |
            (state.color_write_mask_alpha ? 8u : 0u)
        );
}

size_t Color_blend_state::s_serial{0};

Color_blend_state Color_blend_state::color_blend_disabled;

Color_blend_state Color_blend_state::color_blend_premultiplied {
    true,
    {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
    true,
    true,
    true,
    true
};

Color_blend_state Color_blend_state::color_writes_disabled {
    false,
    {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    {
        .equation_mode      = gl::Blend_equation_mode::func_add,
        .source_factor      = gl::Blending_factor::one,
        .destination_factor = gl::Blending_factor::one_minus_src_alpha
    },
    glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
    false,
    false,
    false,
    false
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
    m_last = 0;
}

void Color_blend_state_tracker::execute(Color_blend_state const* state) noexcept
{
    ERHE_VERIFY(state != nullptr);

#if !DISABLE_CACHE
    if (m_last == state->serial)
    {
        return;
    }

#endif
    if (state->enabled)
    {
#if !DISABLE_CACHE
        if (!m_cache.enabled)
#endif
        {
            gl::enable(gl::Enable_cap::blend);
            m_cache.enabled = true;
        }
        auto color = state->color;
#if !DISABLE_CACHE
        if (m_cache.color != state->color)
#endif
        {
            gl::blend_color(color.x, color.y, color.z, color.w);
            m_cache.color = color;
        }
#if !DISABLE_CACHE
        if (
            (m_cache.rgb.equation_mode   != state->rgb.equation_mode) ||
            (m_cache.alpha.equation_mode != state->alpha.equation_mode)
        )
#endif
        {
            gl::blend_equation_separate(state->rgb.equation_mode, state->alpha.equation_mode);
            m_cache.rgb.equation_mode   = state->rgb.equation_mode;
            m_cache.alpha.equation_mode = state->alpha.equation_mode;
        }
#if !DISABLE_CACHE
        if ((m_cache.rgb.source_factor        != state->rgb.source_factor) ||
            (m_cache.rgb.destination_factor   != state->rgb.destination_factor) ||
            (m_cache.alpha.source_factor      != state->alpha.source_factor) ||
            (m_cache.alpha.destination_factor != state->alpha.destination_factor))
#endif
        {
            gl::blend_func_separate(
                state->rgb.source_factor,
                state->rgb.destination_factor,
                state->alpha.source_factor,
                state->alpha.destination_factor
            );
            m_cache.rgb.source_factor        = state->rgb.source_factor;
            m_cache.rgb.destination_factor   = state->rgb.destination_factor;
            m_cache.alpha.source_factor      = state->alpha.source_factor;
            m_cache.alpha.destination_factor = state->alpha.destination_factor;
        }
    }
    else
    {

#if !DISABLE_CACHE
        if (m_cache.enabled)
#endif
        {
            gl::disable(gl::Enable_cap::blend);
            m_cache.enabled = false;
        }
    }

#if !DISABLE_CACHE
    if (
        (m_cache.color_write_mask_red   != state->color_write_mask_red  ) ||
        (m_cache.color_write_mask_green != state->color_write_mask_green) ||
        (m_cache.color_write_mask_blue  != state->color_write_mask_blue ) ||
        (m_cache.color_write_mask_alpha != state->color_write_mask_alpha)
    )
#endif
    {
        gl::color_mask(
            state->color_write_mask_red   ? GL_TRUE : GL_FALSE,
            state->color_write_mask_green ? GL_TRUE : GL_FALSE,
            state->color_write_mask_blue  ? GL_TRUE : GL_FALSE,
            state->color_write_mask_alpha ? GL_TRUE : GL_FALSE
        );
        m_cache.color_write_mask_red   = state->color_write_mask_red;
        m_cache.color_write_mask_green = state->color_write_mask_green;
        m_cache.color_write_mask_blue  = state->color_write_mask_blue;
        m_cache.color_write_mask_alpha = state->color_write_mask_alpha;
    }

    m_last = state->serial;
}

auto operator==(const Blend_state_component& lhs, const Blend_state_component& rhs) noexcept
-> bool
{
    return
        (lhs.equation_mode      == rhs.equation_mode     ) &&
        (lhs.source_factor      == rhs.source_factor     ) &&
        (lhs.destination_factor == rhs.destination_factor);
}

auto operator!=(const Blend_state_component& lhs, const Blend_state_component& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

auto operator==(const Color_blend_state& lhs, const Color_blend_state& rhs) noexcept
-> bool
{
    return
        (lhs.enabled                == rhs.enabled               ) &&
        (lhs.rgb                    == rhs.rgb                   ) &&
        (lhs.alpha                  == rhs.alpha                 ) &&
        (lhs.color                  == rhs.color                 ) &&
        (lhs.color_write_mask_red   == rhs.color_write_mask_red  ) &&
        (lhs.color_write_mask_green == rhs.color_write_mask_green) &&
        (lhs.color_write_mask_blue  == rhs.color_write_mask_blue ) &&
        (lhs.color_write_mask_alpha == rhs.color_write_mask_alpha);
}

auto operator!=(const Color_blend_state& lhs, const Color_blend_state& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}


} // namespace erhe::graphics
