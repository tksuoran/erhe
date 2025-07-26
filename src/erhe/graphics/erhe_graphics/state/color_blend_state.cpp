#include "erhe_graphics/state/color_blend_state.hpp"

namespace erhe::graphics {

namespace {

auto shuffle(const std::size_t x, const std::size_t m, const std::size_t shift) -> std::size_t
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
            (static_cast<size_t>(blend_state_component.equation_mode     ) << 0u) | // 3 bits
            (static_cast<size_t>(blend_state_component.source_factor     ) << 3u) | // 5 bits
            (static_cast<size_t>(blend_state_component.destination_factor) << 8u);  // 5 bits
    }
};

}

auto Blend_state_hash::operator()(const Color_blend_state& state) const noexcept -> std::size_t
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
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::one,
        .destination_factor = Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::one,
        .destination_factor = Blending_factor::one_minus_src_alpha
    },
    .constant = {0.0f, 0.0f, 0.0f, 0.0f},
    .write_mask = {
        .red   = true,
        .green = true,
        .blue  = true,
        .alpha = true
    }
};

Color_blend_state Color_blend_state::color_blend_alpha {
    .enabled = true,
    .rgb = {
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::src_alpha,
        .destination_factor = Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::src_alpha,
        .destination_factor = Blending_factor::one_minus_src_alpha
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
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::one,
        .destination_factor = Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::one,
        .destination_factor = Blending_factor::zero
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
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::one,
        .destination_factor = Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = Blend_equation_mode::func_add,
        .source_factor      = Blending_factor::one,
        .destination_factor = Blending_factor::one_minus_src_alpha
    },
    .constant = {0.0f, 0.0f, 0.0f, 0.0f},
    .write_mask = {
        .red   = false,
        .green = false,
        .blue  = false,
        .alpha = false
    }
};

auto operator==(const Blend_state_component& lhs, const Blend_state_component& rhs) noexcept -> bool
{
    return
        (lhs.equation_mode      == rhs.equation_mode     ) &&
        (lhs.source_factor      == rhs.source_factor     ) &&
        (lhs.destination_factor == rhs.destination_factor);
}

auto operator!=(const Blend_state_component& lhs, const Blend_state_component& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

auto operator==(const Color_blend_state& lhs, const Color_blend_state& rhs) noexcept -> bool
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

auto operator!=(const Color_blend_state& lhs, const Color_blend_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}


} // namespace erhe::graphics
