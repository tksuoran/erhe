#include "erhe_graphics/state/viewport_state.hpp"
#include "erhe_gl/wrapper_functions.hpp"
#include "erhe_verify/verify.hpp"

//#define DISABLE_CACHE 1

namespace erhe::graphics
{

size_t Viewport_state::s_serial{0};

void Viewport_state_tracker::reset()
{
    gl::depth_range(0.0f, 1.0f);
    m_cache = Viewport_state{};
    m_last = 0;
}

void Viewport_state_tracker::execute(Viewport_state const* state)
{
    ERHE_VERIFY(state != nullptr);

#if !DISABLE_CACHE
    if (m_last == state->serial) {
        return;
    }
#endif

#if !DISABLE_CACHE
    if (
        (m_cache.x      != state->x)      ||
        (m_cache.y      != state->y)      ||
        (m_cache.width  != state->width)  ||
        (m_cache.height != state->height)
    )
#endif
    {
        gl::viewport_indexed_f(0, state->x, state->y, state->width, state->height);
        m_cache.x      = state->x;
        m_cache.y      = state->y;
        m_cache.width  = state->width;
        m_cache.height = state->height;
    }

#if !DISABLE_CACHE
    if (
        (m_cache.min_depth != state->min_depth) ||
        (m_cache.max_depth != state->max_depth)
    )
#endif
    {
        gl::depth_range_f(state->min_depth, state->max_depth);
        m_cache.min_depth = state->min_depth;
        m_cache.max_depth = state->max_depth;
    }

    m_last = state->serial;
}

auto operator==(
    const Viewport_state& lhs,
    const Viewport_state& rhs
) noexcept -> bool
{
    return
        (lhs.min_depth == rhs.min_depth) &&
        (lhs.max_depth == rhs.max_depth);
}

auto operator!=(
    const Viewport_state& lhs,
    const Viewport_state& rhs
) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
