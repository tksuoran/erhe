#pragma once

#include "erhe_gl/wrapper_enums.hpp"

#include <cstddef>

namespace erhe::graphics {

class Depth_stencil_state;

class Stencil_op_state
{
public:
    gl::Stencil_op       stencil_fail_op{gl::Stencil_op::keep};
    gl::Stencil_op       z_fail_op      {gl::Stencil_op::keep};
    gl::Stencil_op       z_pass_op      {gl::Stencil_op::keep};
    gl::Stencil_function function       {gl::Stencil_function::always};
    unsigned int         reference      {0u};
    unsigned int         test_mask      {0xffU}; // 0xffffu ?
    unsigned int         write_mask     {0xffU}; // 0xffffu ?
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
    bool               depth_test_enable  {false};
    bool               depth_write_enable {true}; // OpenGL depth mask
    gl::Depth_function depth_compare_op   {gl::Depth_function::less}; // Not Maybe_reversed::less, this has to match default OpenGL state
    bool               stencil_test_enable{false};
    Stencil_op_state   stencil_front      {};
    Stencil_op_state   stencil_back       {};

    static Depth_stencil_state depth_test_disabled_stencil_test_disabled;
    static Depth_stencil_state depth_test_always_stencil_test_disabled;

    static auto depth_test_enabled_stencil_test_disabled                 (bool reverse_depth) -> const Depth_stencil_state&;
    static auto depth_test_enabled_less_or_equal_stencil_test_disabled   (bool reverse_depth) -> const Depth_stencil_state&;
    static auto depth_test_enabled_greater_or_equal_stencil_test_disabled(bool reverse_depth) -> const Depth_stencil_state&;

private:
    static Depth_stencil_state s_depth_test_enabled_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_stencil_test_disabled_reverse_depth;
    static Depth_stencil_state s_depth_test_enabled_less_or_equal_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_less_or_equal_stencil_test_disabled_reverse_depth;
    static Depth_stencil_state s_depth_test_enabled_greater_or_equal_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_greater_or_equal_stencil_test_disabled_reverse_depth;
};

auto reverse(const gl::Depth_function depth_function) -> gl::Depth_function;

class Depth_stencil_state_hash
{
public:
    auto operator()(const Depth_stencil_state& state) const noexcept -> size_t;
};

auto operator==(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept -> bool;
auto operator!=(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept -> bool;

class Depth_stencil_state_tracker
{
public:
    void reset  ();
    void execute(const Depth_stencil_state& state);

private:
    static void execute_component(
        gl::Stencil_face_direction face,
        const Stencil_op_state&    state,
        Stencil_op_state&          cache
    );

    static void execute_shared(
        const Stencil_op_state& state,
        Depth_stencil_state&    cache
    );

private:
    Depth_stencil_state m_cache;
};

} // namespace erhe::graphics
