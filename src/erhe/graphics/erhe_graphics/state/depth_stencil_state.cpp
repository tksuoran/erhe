#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::graphics {

auto reverse(const Compare_operation depth_function) -> Compare_operation
{
    switch (depth_function) {
        //using enum Compare_operation;
        case Compare_operation::always          : return Compare_operation::always;
        case Compare_operation::equal           : return Compare_operation::equal;
        case Compare_operation::greater_or_equal: return Compare_operation::less_or_equal;
        case Compare_operation::greater         : return Compare_operation::less;
        case Compare_operation::less_or_equal   : return Compare_operation::greater_or_equal;
        case Compare_operation::less            : return Compare_operation::greater;
        case Compare_operation::never           : return Compare_operation::never;
        case Compare_operation::not_equal       : return Compare_operation::not_equal;
        default: {
            ERHE_FATAL("bad gl::Depth_function");
        }
    }
}

auto Stencil_state_component_hash::operator()(const Stencil_op_state& stencil_state_component) const noexcept -> size_t
{
    static_assert(sizeof(size_t) >= 5u);
    return
        ( (static_cast<size_t>(stencil_state_component.stencil_fail_op)        )       ) | // 3 bits
        ( (static_cast<size_t>(stencil_state_component.z_fail_op      )        ) <<  3u) | // 3 bits
        ( (static_cast<size_t>(stencil_state_component.z_pass_op      )        ) <<  6u) | // 3 bits
        ( (static_cast<size_t>(stencil_state_component.function       )        ) <<  9u) | // 3 bits
        ( (static_cast<size_t>(stencil_state_component.reference      ) & 0xffU) << 12u) | // 8 bits
        ( (static_cast<size_t>(stencil_state_component.test_mask      ) & 0xffU) << 20u) | // 8 bits
        ( (static_cast<size_t>(stencil_state_component.write_mask     ) & 0xffU) << 28u); // 8 bits
}


auto Depth_stencil_state_hash::operator()(const Depth_stencil_state& state) const noexcept -> size_t
{
    // Since front might match with back, apply std::hash()
    // to one of them to avoid them perfectly canceling out.
    return
        (
            (state.depth_test_enable   ? 1u : 0u) |
            (state.depth_write_enable  ? 2u : 0u) |
            (state.stencil_test_enable ? 3u : 0u) |
            (static_cast<size_t>(state.depth_compare_op) << 3) // 3 bits
        )
        ^
        (
            Stencil_state_component_hash{}(state.stencil_front)
        )
        ^
        (
            Stencil_state_component_hash{}(state.stencil_back)
        );
}

Depth_stencil_state Depth_stencil_state::depth_test_disabled_stencil_test_disabled = Depth_stencil_state{};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_stencil_test_disabled_forward_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = Compare_operation::less,
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_stencil_test_disabled_reverse_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = reverse(Compare_operation::less),
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_less_or_equal_stencil_test_disabled_forward_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = Compare_operation::less_or_equal,
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_less_or_equal_stencil_test_disabled_reverse_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = reverse(Compare_operation::less_or_equal),
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_greater_or_equal_stencil_test_disabled_forward_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = Compare_operation::greater_or_equal,
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_greater_or_equal_stencil_test_disabled_reverse_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = reverse(Compare_operation::greater_or_equal),
    .stencil_test_enable = false
};

auto Depth_stencil_state::depth_test_enabled_stencil_test_disabled(bool reverse_depth) -> const Depth_stencil_state&
{
    return reverse_depth
        ? s_depth_test_enabled_stencil_test_disabled_reverse_depth
        : s_depth_test_enabled_stencil_test_disabled_forward_depth;
}

auto Depth_stencil_state::depth_test_enabled_less_or_equal_stencil_test_disabled(bool reverse_depth) -> const Depth_stencil_state&
{
    return reverse_depth
        ? s_depth_test_enabled_less_or_equal_stencil_test_disabled_reverse_depth
        : s_depth_test_enabled_less_or_equal_stencil_test_disabled_forward_depth;
}

auto Depth_stencil_state::depth_test_enabled_greater_or_equal_stencil_test_disabled(bool reverse_depth) -> const Depth_stencil_state&
{
    return reverse_depth
        ? s_depth_test_enabled_greater_or_equal_stencil_test_disabled_reverse_depth
        : s_depth_test_enabled_greater_or_equal_stencil_test_disabled_forward_depth;
}

Depth_stencil_state Depth_stencil_state::depth_test_always_stencil_test_disabled = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = Compare_operation::always,
    .stencil_test_enable = false,
    .stencil_front = {
        .stencil_fail_op = Stencil_op::keep,
        .z_fail_op       = Stencil_op::keep,
        .z_pass_op       = Stencil_op::keep,
        .function        = Compare_operation::always,
        .reference       = 0u,
        .test_mask       = 0b00000000u,
        .write_mask      = 0b00000000u,
    },
    .stencil_back = {
        .stencil_fail_op = Stencil_op::keep,
        .z_fail_op       = Stencil_op::keep,
        .z_pass_op       = Stencil_op::keep,
        .function        = Compare_operation::always,
        .reference       = 0u,
        .test_mask       = 0b00000000u,
        .write_mask      = 0b00000000u,
    },
};


auto operator==(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept -> bool
{
    return
        (lhs.stencil_fail_op == rhs.stencil_fail_op) &&
        (lhs.z_fail_op       == rhs.z_fail_op      ) &&
        (lhs.z_pass_op       == rhs.z_pass_op      ) &&
        (lhs.function        == rhs.function       ) &&
        (lhs.reference       == rhs.reference      ) &&
        (lhs.test_mask       == rhs.test_mask      ) &&
        (lhs.write_mask      == rhs.write_mask     );
}

auto operator!=(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

auto operator==(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept -> bool
{
    return
        (lhs.depth_test_enable   == rhs.depth_test_enable  ) &&
        (lhs.depth_write_enable  == rhs.depth_write_enable ) &&
        (lhs.depth_compare_op    == rhs.depth_compare_op   ) &&
        (lhs.stencil_test_enable == rhs.stencil_test_enable) &&
        (lhs.stencil_front       == rhs.stencil_front      ) &&
        (lhs.stencil_back        == rhs.stencil_back       );
}

auto operator!=(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
