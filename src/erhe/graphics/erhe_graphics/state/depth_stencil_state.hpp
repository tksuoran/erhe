#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstddef>

namespace erhe::graphics {

class Depth_stencil_state;

class Stencil_op_state
{
public:
    Stencil_op        stencil_fail_op{Stencil_op::keep};
    Stencil_op        z_fail_op      {Stencil_op::keep};
    Stencil_op        z_pass_op      {Stencil_op::keep};
    Compare_operation function       {Compare_operation::always};
    unsigned int      reference      {0u};
    unsigned int      test_mask      {0xffU}; // 0xffffu ?
    unsigned int      write_mask     {0xffU}; // 0xffffu ?
};

class Stencil_state_component_hash
{
public:
    [[nodiscard]]
    auto operator()(const Stencil_op_state& stencil_state_component) const noexcept -> std::size_t;
};

[[nodiscard]] auto operator==(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept -> bool;

class Depth_stencil_state
{
public:
    bool              depth_test_enable  {false};
    bool              depth_write_enable {true}; // OpenGL depth mask
    Compare_operation depth_compare_op   {Compare_operation::less}; // Not Maybe_reversed::less, this has to match default OpenGL state
    bool              stencil_test_enable{false};
    Stencil_op_state  stencil_front      {};
    Stencil_op_state  stencil_back       {};

    static Depth_stencil_state depth_test_disabled_stencil_test_disabled;
    static Depth_stencil_state depth_test_always_stencil_test_disabled;

    static auto depth_test_enabled_stencil_test_disabled                 (bool reverse_depth = true) -> const Depth_stencil_state&;
    static auto depth_test_enabled_less_or_equal_stencil_test_disabled   (bool reverse_depth = true) -> const Depth_stencil_state&;
    static auto depth_test_enabled_greater_or_equal_stencil_test_disabled(bool reverse_depth = true) -> const Depth_stencil_state&;

private:
    static Depth_stencil_state s_depth_test_enabled_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_stencil_test_disabled_reverse_depth;
    static Depth_stencil_state s_depth_test_enabled_less_or_equal_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_less_or_equal_stencil_test_disabled_reverse_depth;
    static Depth_stencil_state s_depth_test_enabled_greater_or_equal_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_greater_or_equal_stencil_test_disabled_reverse_depth;
};

auto reverse(Compare_operation compare_operation) -> Compare_operation;

class Depth_stencil_state_hash
{
public:
    auto operator()(const Depth_stencil_state& state) const noexcept -> size_t;
};

auto operator==(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept -> bool;
auto operator!=(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept -> bool;

} // namespace erhe::graphics
