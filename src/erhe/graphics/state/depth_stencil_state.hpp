#pragma once

#include "erhe/gl/strong_gl_enums.hpp"
#include "erhe/graphics/configuration.hpp"

#include <functional>

namespace erhe::graphics
{

class Depth_stencil_state;

class Stencil_op_state
{
public:
    gl::Stencil_op       stencil_fail_op{gl::Stencil_op::keep};
    gl::Stencil_op       z_fail_op      {gl::Stencil_op::keep};
    gl::Stencil_op       z_pass_op      {gl::Stencil_op::keep};
    gl::Stencil_function function       {gl::Stencil_function::always};
    unsigned int         reference      {0u};
    unsigned int         test_mask      {0xffffU};
    unsigned int         write_mask     {0xffffU};
};

class Stencil_state_component_hash
{
public:
    auto operator()(const Stencil_op_state& stencil_state_component) const noexcept -> size_t;
};

auto operator==(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept
-> bool;

auto operator!=(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept
-> bool;

class Depth_stencil_state
{
public:
    Depth_stencil_state();
    Depth_stencil_state(
        bool               depth_test_enable,
        bool               depth_write_enable,
        gl::Depth_function depth_compare_op,
        bool               stencil_test_enable,
        Stencil_op_state   front,
        Stencil_op_state   back
    );
    void touch();

    size_t             serial;
    bool               depth_test_enable  {false};
    bool               depth_write_enable {true}; // OpenGL depth mask
    gl::Depth_function depth_compare_op   {gl::Depth_function::less}; // Not Maybe_reversed::less, this has to match default OpenGL state
    bool               stencil_test_enable{false};
    Stencil_op_state   front;
    Stencil_op_state   back;

    static auto get_next_serial() -> size_t;

    static size_t s_serial;

    static Depth_stencil_state depth_test_disabled_stencil_test_disabled;
    static Depth_stencil_state depth_test_always_stencil_test_disabled;

    static auto depth_test_enabled_stencil_test_disabled                 (bool reverse_depth) -> Depth_stencil_state*;
    static auto depth_test_enabled_greater_or_equal_stencil_test_disabled(bool reverse_depth) -> Depth_stencil_state*;

private:
    static Depth_stencil_state s_depth_test_enabled_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_stencil_test_disabled_reverse_depth;
    static Depth_stencil_state s_depth_test_enabled_greater_or_equal_stencil_test_disabled_forward_depth;
    static Depth_stencil_state s_depth_test_enabled_greater_or_equal_stencil_test_disabled_reverse_depth;
};

auto reverse(const gl::Depth_function depth_function) -> gl::Depth_function;

class Depth_stencil_state_hash
{
public:
    auto operator()(const Depth_stencil_state& state) const noexcept -> size_t;
};

auto operator==(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept
-> bool;

auto operator!=(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept
-> bool;

class Depth_stencil_state_tracker
{
public:
    void reset  ();
    void execute(const Depth_stencil_state* state);

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
    size_t              m_last{0};
    Depth_stencil_state m_cache;
};

} // namespace erhe::graphics
