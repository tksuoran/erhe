#include "erhe/graphics/state/depth_stencil_state.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/toolkit/verify.hpp"

// #define DISABLE_CACHE 1

namespace erhe::graphics
{

size_t Depth_stencil_state::s_serial{0};

Depth_stencil_state Depth_stencil_state::depth_test_disabled_stencil_test_disabled = Depth_stencil_state{};
Depth_stencil_state Depth_stencil_state::depth_test_enabled_stencil_test_disabled = Depth_stencil_state{
    true,
    true,
    Maybe_reversed::less,
    false,
    {
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_function::always,
        0u,
        0xffffu,
        0xffffu
    },
    {
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_function::always,
        0u,
        0xffffu,
        0xffffu
    },
};
Depth_stencil_state Depth_stencil_state::depth_test_enabled_greater_or_equal_stencil_test_disabled = Depth_stencil_state{
    true,
    true,
    Maybe_reversed::gequal,
    false,
    {
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_function::always,
        0u,
        0xffffu,
        0xffffu
    },
    {
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_function::always,
        0u,
        0xffffu,
        0xffffu
    },
};
Depth_stencil_state Depth_stencil_state::depth_test_always_stencil_test_disabled = Depth_stencil_state{
    true,
    true,
    gl::Depth_function::always,
    false,
    {
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_function::always,
        0u,
        0xffffu,
        0xffffu
    },
    {
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_op::keep,
        gl::Stencil_function::always,
        0u,
        0xffffu,
        0xffffu
    },
};

void Depth_stencil_state_tracker::reset()
{
    gl::disable(gl::Enable_cap::depth_test);
    gl::depth_func(gl::Depth_function::less); // Not Maybe_reversed::less, this has to match default OpenGL state
    gl::depth_mask(GL_TRUE);

    gl::stencil_op(gl::Stencil_op::keep, gl::Stencil_op::keep, gl::Stencil_op::keep);
    gl::stencil_mask(0xffffU);
    gl::stencil_func(gl::Stencil_function::always, 0, 0xffffU);
    m_cache = Depth_stencil_state{};
    m_last = 0;
}

void Depth_stencil_state_tracker::execute_component(gl::Stencil_face_direction face,
                                                    const Stencil_op_state&    state,
                                                    Stencil_op_state&          cache)
{
#if !DISABLE_CACHE
    if ((cache.stencil_fail_op != state.stencil_fail_op) ||
        (cache.z_fail_op       != state.z_fail_op) ||
        (cache.z_pass_op       != state.z_pass_op))
#endif
    {
        gl::stencil_op_separate(face, state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
        cache.stencil_fail_op = state.stencil_fail_op;
        cache.z_fail_op       = state.z_fail_op;
        cache.z_pass_op       = state.z_pass_op;
    }
#if !DISABLE_CACHE
    if (cache.write_mask != state.write_mask)
#endif
    {
        gl::stencil_mask_separate(face, state.write_mask);
        cache.write_mask = state.write_mask;
    }

#if !DISABLE_CACHE
    if ((cache.function  != state.function) ||
        (cache.reference != state.reference) ||
        (cache.test_mask != state.test_mask))
#endif
    {
        gl::stencil_func_separate(face, state.function, state.reference, state.test_mask);
        cache.function  = state.function;
        cache.reference = state.reference;
        cache.test_mask = state.test_mask;
    }
}

void Depth_stencil_state_tracker::execute_shared(const Stencil_op_state& state,
                                                 Depth_stencil_state&    cache)
{
#if !DISABLE_CACHE
    if ((cache.front.stencil_fail_op != state.stencil_fail_op) ||
        (cache.front.z_fail_op       != state.z_fail_op) ||
        (cache.front.z_pass_op       != state.z_pass_op) ||
        (cache.back.stencil_fail_op  != state.stencil_fail_op) ||
        (cache.back.z_fail_op        != state.z_fail_op) ||
        (cache.back.z_pass_op        != state.z_pass_op))
#endif
    {
        gl::stencil_op(state.stencil_fail_op, state.z_fail_op, state.z_pass_op);
        cache.front.stencil_fail_op = state.stencil_fail_op;
        cache.front.z_fail_op       = state.z_fail_op;
        cache.front.z_pass_op       = state.z_pass_op;
        cache.back.stencil_fail_op  = state.stencil_fail_op;
        cache.back.z_fail_op        = state.z_fail_op;
        cache.back.z_pass_op        = state.z_pass_op;
    }

#if !DISABLE_CACHE
    if ((cache.front.write_mask != state.write_mask) ||
        (cache.back.write_mask  != state.write_mask))
#endif
    {
        gl::stencil_mask(state.write_mask);
        cache.front.write_mask = state.write_mask;
        cache.back.write_mask  = state.write_mask;
    }

#if !DISABLE_CACHE
    if ((cache.front.function  != state.function)  ||
        (cache.front.reference != state.reference) ||
        (cache.front.test_mask != state.test_mask) ||
        (cache.back.function   != state.function)  ||
        (cache.back.reference  != state.reference) ||
        (cache.back.test_mask  != state.test_mask))
#endif
    {
        gl::stencil_func(state.function, state.reference, state.test_mask);
        cache.front.function  = state.function;
        cache.front.reference = state.reference;
        cache.front.test_mask = state.test_mask;
        cache.back.function   = state.function;
        cache.back.reference  = state.reference;
        cache.back.test_mask  = state.test_mask;
    }
}

void Depth_stencil_state_tracker::execute(Depth_stencil_state const* state)
{
    VERIFY(state != nullptr);

#if !DISABLE_CACHE
    if (m_last == state->serial)
    {
        return;
    }

#endif

    if (state->depth_test_enable)
    {
#if !DISABLE_CACHE
        if (!m_cache.depth_test_enable)
#endif
        {
            gl::enable(gl::Enable_cap::depth_test);
            m_cache.depth_test_enable = true;
        }

#if !DISABLE_CACHE
        if (m_cache.depth_compare_op != state->depth_compare_op)
#endif
        {
            gl::depth_func(state->depth_compare_op);
            m_cache.depth_compare_op = state->depth_compare_op;
        }
    }
    else
    {
#if !DISABLE_CACHE
        if (m_cache.depth_test_enable)
#endif
        {
            gl::disable(gl::Enable_cap::depth_test);
            m_cache.depth_test_enable = false;
        }
    }

#if !DISABLE_CACHE
    if (m_cache.depth_write_enable != state->depth_write_enable)
#endif
    {
        gl::depth_mask(state->depth_write_enable ? GL_TRUE : GL_FALSE);
        m_cache.depth_write_enable = state->depth_write_enable;
    }

    if (state->stencil_test_enable)
    {
#if !DISABLE_CACHE
        if (!m_cache.stencil_test_enable)
#endif
        {
            gl::enable(gl::Enable_cap::stencil_test);
            m_cache.stencil_test_enable = true;
        }
        execute_component(gl::Stencil_face_direction::front, state->front, m_cache.front);
        execute_component(gl::Stencil_face_direction::back,  state->back,  m_cache.back);
    }
    else
    {
#if !DISABLE_CACHE
        if (m_cache.stencil_test_enable)
#endif
        {
            gl::disable(gl::Enable_cap::stencil_test);
            m_cache.stencil_test_enable = false;
        }
    }
    m_last = state->serial;
}

auto operator==(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept
-> bool
{
    return (lhs.stencil_fail_op == rhs.stencil_fail_op) &&
           (lhs.z_fail_op       == rhs.z_fail_op      ) &&
           (lhs.z_pass_op       == rhs.z_pass_op      ) &&
           (lhs.function        == rhs.function       ) &&
           (lhs.reference       == rhs.reference      ) &&
           (lhs.test_mask       == rhs.test_mask      ) &&
           (lhs.write_mask      == rhs.write_mask     );
}

auto operator!=(const Stencil_op_state& lhs, const Stencil_op_state& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

auto operator==(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept
-> bool
{
    return (lhs.depth_test_enable   == rhs.depth_test_enable  ) &&
           (lhs.depth_compare_op    == rhs.depth_compare_op   ) &&
           (lhs.depth_write_enable  == rhs.depth_write_enable ) &&
           (lhs.stencil_test_enable == rhs.stencil_test_enable) &&
           (lhs.front               == rhs.front              ) &&
           (lhs.back                == rhs.back               );
}

auto operator!=(const Depth_stencil_state& lhs, const Depth_stencil_state& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
