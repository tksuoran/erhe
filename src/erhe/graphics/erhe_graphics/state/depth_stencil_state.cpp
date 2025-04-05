#include "erhe_graphics/state/depth_stencil_state.hpp"
#include "erhe_gl/enum_base_zero_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

#define DISABLE_CACHE 0

namespace erhe::graphics {

auto reverse(const gl::Depth_function depth_function) -> gl::Depth_function
{
    switch (depth_function) {
        //using enum gl::Depth_function;
        case gl::Depth_function::always  : return gl::Depth_function::always  ;
        case gl::Depth_function::equal   : return gl::Depth_function::equal   ;
        case gl::Depth_function::gequal  : return gl::Depth_function::lequal  ;
        case gl::Depth_function::greater : return gl::Depth_function::less    ;
        case gl::Depth_function::lequal  : return gl::Depth_function::gequal  ;
        case gl::Depth_function::less    : return gl::Depth_function::greater ;
        case gl::Depth_function::never   : return gl::Depth_function::never   ;
        case gl::Depth_function::notequal: return gl::Depth_function::notequal;
        default: {
            ERHE_FATAL("bad gl::Depth_function");
        }
    }
}

auto Stencil_state_component_hash::operator()(const Stencil_op_state& stencil_state_component) const noexcept -> size_t
{
    static_assert(sizeof(size_t) >= 5u);
    return
        (gl::base_zero(stencil_state_component.stencil_fail_op)) | // 3 bits
        (gl::base_zero(stencil_state_component.z_fail_op) << 3u) | // 3 bits
        (gl::base_zero(stencil_state_component.z_pass_op) << 6u) | // 3 bits
        (gl::base_zero(stencil_state_component.function ) << 9u) | // 3 bits
        ((static_cast<uint64_t>(stencil_state_component.reference ) & 0xffU) << 12u) | // 8 bits
        ((static_cast<uint64_t>(stencil_state_component.test_mask ) & 0xffU) << 20u) | // 8 bits
        ((static_cast<uint64_t>(stencil_state_component.write_mask) & 0xffU) << 28u); // 8 bits
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
            (gl::base_zero(state.depth_compare_op) << 3) // 3 bits
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
    .depth_compare_op    = gl::Depth_function::less,
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_stencil_test_disabled_reverse_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = reverse(gl::Depth_function::less),
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_less_or_equal_stencil_test_disabled_forward_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = gl::Depth_function::lequal,
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_less_or_equal_stencil_test_disabled_reverse_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = reverse(gl::Depth_function::lequal),
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_greater_or_equal_stencil_test_disabled_forward_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = gl::Depth_function::gequal,
    .stencil_test_enable = false
};

Depth_stencil_state Depth_stencil_state::s_depth_test_enabled_greater_or_equal_stencil_test_disabled_reverse_depth = Depth_stencil_state{
    .depth_test_enable   = true,
    .depth_write_enable  = true,
    .depth_compare_op    = reverse(gl::Depth_function::gequal),
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
    .depth_compare_op    = gl::Depth_function::always,
    .stencil_test_enable = false,
    .stencil_front = {
        .stencil_fail_op = gl::Stencil_op::keep,
        .z_fail_op       = gl::Stencil_op::keep,
        .z_pass_op       = gl::Stencil_op::keep,
        .function        = gl::Stencil_function::always,
        .reference       = 0u,
        .test_mask       = 0x00u,
        .write_mask      = 0x00u
    },
    .stencil_back = {
        .stencil_fail_op = gl::Stencil_op::keep,
        .z_fail_op       = gl::Stencil_op::keep,
        .z_pass_op       = gl::Stencil_op::keep,
        .function        = gl::Stencil_function::always,
        .reference       = 0u,
        .test_mask       = 0x00u,
        .write_mask      = 0x00u
    },
};

void Depth_stencil_state_tracker::reset()
{
    gl::disable(gl::Enable_cap::depth_test);
    gl::depth_func(gl::Depth_function::less); // Not Maybe_reversed::less, this has to match default OpenGL state
    gl::depth_mask(GL_TRUE);

    gl::stencil_op(gl::Stencil_op::keep, gl::Stencil_op::keep, gl::Stencil_op::keep);
    gl::stencil_mask(0xffu);
    gl::stencil_func(gl::Stencil_function::always, 0, 0xffu);
    m_cache = Depth_stencil_state{};
}

void Depth_stencil_state_tracker::execute_component(
    gl::Stencil_face_direction face,
    const Stencil_op_state&    state,
    Stencil_op_state&          cache
)
{
#if DISABLE_CACHE
    static_cast<void>(cache);
    gl::stencil_op_separate(face, state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
    gl::stencil_mask_separate(face, state.write_mask);
    gl::stencil_func_separate(face, state.function, state.reference, state.test_mask);
#else
    if (
        (cache.stencil_fail_op != state.stencil_fail_op) ||
        (cache.z_fail_op       != state.z_fail_op) ||
        (cache.z_pass_op       != state.z_pass_op)
    ) {
        gl::stencil_op_separate(face, state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
        cache.stencil_fail_op = state.stencil_fail_op;
        cache.z_fail_op       = state.z_fail_op;
        cache.z_pass_op       = state.z_pass_op;
    }
    if (cache.write_mask != state.write_mask) {
        gl::stencil_mask_separate(face, state.write_mask);
        cache.write_mask = state.write_mask;
    }

    if (
        (cache.function  != state.function) ||
        (cache.reference != state.reference) ||
        (cache.test_mask != state.test_mask)
    ) {
        gl::stencil_func_separate(face, state.function, state.reference, state.test_mask);
        cache.function  = state.function;
        cache.reference = state.reference;
        cache.test_mask = state.test_mask;
    }
#endif
}

void Depth_stencil_state_tracker::execute_shared(const Stencil_op_state& state, Depth_stencil_state& cache)
{
#if DISABLE_CACHE
    static_cast<void>(cache);
    gl::stencil_op(state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
    gl::stencil_mask(state.write_mask);
    gl::stencil_func(state.function, state.reference, state.test_mask);
#else
    if (
        (cache.stencil_front.stencil_fail_op != state.stencil_fail_op) ||
        (cache.stencil_front.z_fail_op       != state.z_fail_op)       ||
        (cache.stencil_front.z_pass_op       != state.z_pass_op)       ||
        (cache.stencil_back.stencil_fail_op  != state.stencil_fail_op) ||
        (cache.stencil_back.z_fail_op        != state.z_fail_op)       ||
        (cache.stencil_back.z_pass_op        != state.z_pass_op)
    ) {
        gl::stencil_op(state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
        cache.stencil_front.stencil_fail_op = state.stencil_fail_op;
        cache.stencil_front.z_fail_op       = state.z_fail_op;
        cache.stencil_front.z_pass_op       = state.z_pass_op;
        cache.stencil_back.stencil_fail_op  = state.stencil_fail_op;
        cache.stencil_back.z_fail_op        = state.z_fail_op;
        cache.stencil_back.z_pass_op        = state.z_pass_op;
    }

    if (
        (cache.stencil_front.write_mask != state.write_mask) ||
        (cache.stencil_back.write_mask  != state.write_mask)
    ) {
        gl::stencil_mask(state.write_mask);
        cache.stencil_front.write_mask = state.write_mask;
        cache.stencil_back.write_mask  = state.write_mask;
    }

    if (
        (cache.stencil_front.function  != state.function)  ||
        (cache.stencil_front.reference != state.reference) ||
        (cache.stencil_front.test_mask != state.test_mask) ||
        (cache.stencil_back.function   != state.function)  ||
        (cache.stencil_back.reference  != state.reference) ||
        (cache.stencil_back.test_mask  != state.test_mask)
    ) {
        gl::stencil_func(state.function, state.reference, state.test_mask);
        cache.stencil_front.function  = state.function;
        cache.stencil_front.reference = state.reference;
        cache.stencil_front.test_mask = state.test_mask;
        cache.stencil_back.function   = state.function;
        cache.stencil_back.reference  = state.reference;
        cache.stencil_back.test_mask  = state.test_mask;
    }
#endif
}

void Depth_stencil_state_tracker::execute(const Depth_stencil_state& state)
{
#if DISABLE_CACHE
    if (state.depth_test_enable)
    {
        gl::enable(gl::Enable_cap::depth_test);
        gl::depth_func(state.depth_compare_op);
    } else {
        gl::disable(gl::Enable_cap::depth_test);
        gl::depth_func(state.depth_compare_op);
    }

    gl::depth_mask(state.depth_write_enable ? GL_TRUE : GL_FALSE);

    if (state.stencil_test_enable) {
        gl::enable(gl::Enable_cap::stencil_test);
        execute_component(gl::Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(gl::Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    } else {
        gl::disable(gl::Enable_cap::stencil_test);
        execute_component(gl::Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(gl::Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    }
#else
    if (state.depth_test_enable) {
        if (!m_cache.depth_test_enable) {
            gl::enable(gl::Enable_cap::depth_test);
            m_cache.depth_test_enable = true;
        }

        if (m_cache.depth_compare_op != state.depth_compare_op) {
            gl::depth_func(state.depth_compare_op);
            m_cache.depth_compare_op = state.depth_compare_op;
        }
    } else {
        if (m_cache.depth_test_enable) {
            gl::disable(gl::Enable_cap::depth_test);
            m_cache.depth_test_enable = false;
        }
    }

    if (m_cache.depth_write_enable != state.depth_write_enable) {
        gl::depth_mask(state.depth_write_enable ? GL_TRUE : GL_FALSE);
        m_cache.depth_write_enable = state.depth_write_enable;
    }

    if (state.stencil_test_enable) {
        if (!m_cache.stencil_test_enable) {
            gl::enable(gl::Enable_cap::stencil_test);
            m_cache.stencil_test_enable = true;
        }
        execute_component(gl::Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(gl::Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    } else {
        if (m_cache.stencil_test_enable) {
            gl::disable(gl::Enable_cap::stencil_test);
            m_cache.stencil_test_enable = false;
        }
        execute_component(gl::Stencil_face_direction::front, state.stencil_front, m_cache.stencil_front);
        execute_component(gl::Stencil_face_direction::back,  state.stencil_back,  m_cache.stencil_back);
    }
#endif
}

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
