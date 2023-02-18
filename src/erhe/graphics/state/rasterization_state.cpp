#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/gl/enum_base_zero_functions.hpp"
#include "erhe/gl/wrapper_functions.hpp"
#include "erhe/toolkit/verify.hpp"

#define DISABLE_CACHE 1

namespace erhe::graphics
{

auto Rasterization_state_hash::operator()(
    const Rasterization_state& rasterization_state
) noexcept -> std::size_t
{
    return
        (rasterization_state.face_cull_enable ? 1u : 0u)                |
        (gl::base_zero(rasterization_state.cull_face_mode      ) << 1u) | // 2 bits
        (gl::base_zero(rasterization_state.front_face_direction) << 3u) | // 1 bit
        (gl::base_zero(rasterization_state.polygon_mode        ) << 4u);  // 2 bits
}

Rasterization_state Rasterization_state::cull_mode_none       {false, gl::Cull_face_mode::back,  gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::s_cull_mode_front_cw {true,  gl::Cull_face_mode::front, gl::Front_face_direction::cw,  gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::s_cull_mode_front_ccw{true,  gl::Cull_face_mode::front, gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::s_cull_mode_back_cw  {true,  gl::Cull_face_mode::back,  gl::Front_face_direction::cw,  gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::s_cull_mode_back_ccw {true,  gl::Cull_face_mode::back,  gl::Front_face_direction::ccw, gl::Polygon_mode::fill};

auto Rasterization_state::cull_mode_front_cw(
    const bool reverse_depth
) -> const Rasterization_state&
{
    return reverse_depth
        ? s_cull_mode_front_ccw
        : s_cull_mode_front_cw;
}

auto Rasterization_state::cull_mode_front_ccw(
    const bool reverse_depth
) -> const Rasterization_state&
{
    return reverse_depth
        ? s_cull_mode_front_cw
        : s_cull_mode_front_ccw;
}

auto Rasterization_state::cull_mode_back_cw(
    const bool reverse_depth
) -> const Rasterization_state&
{
    return reverse_depth
        ? s_cull_mode_back_ccw
        : s_cull_mode_back_cw;
}

auto Rasterization_state::cull_mode_back_ccw(
    const bool reverse_depth
) -> const Rasterization_state&
{
    return reverse_depth
        ? s_cull_mode_back_cw
        : s_cull_mode_back_ccw;
}

void Rasterization_state_tracker::reset()
{
    gl::disable     (gl::Enable_cap::cull_face);
    gl::cull_face   (gl::Cull_face_mode::back);
    gl::front_face  (gl::Front_face_direction::ccw);
    gl::polygon_mode(gl::Material_face::front_and_back, gl::Polygon_mode::fill);
    m_cache = Rasterization_state{};
}

void Rasterization_state_tracker::execute(const Rasterization_state& state)
{
#if DISABLE_CACHE
    if (state.face_cull_enable) {
        gl::enable(gl::Enable_cap::cull_face);
        gl::cull_face(state.cull_face_mode);
    } else {
        gl::disable(gl::Enable_cap::cull_face);
    }

    gl::front_face(state.front_face_direction);
    gl::polygon_mode(gl::Material_face::front_and_back, state.polygon_mode);
#else
    if (state.face_cull_enable) {
        if (!m_cache.face_cull_enable) {
            gl::enable(gl::Enable_cap::cull_face);
            m_cache.face_cull_enable = true;
        }
        if (m_cache.cull_face_mode != state.cull_face_mode) {
            gl::cull_face(state.cull_face_mode);
            m_cache.cull_face_mode = state.cull_face_mode;
        }
    } else {
        if (m_cache.face_cull_enable) {
            gl::disable(gl::Enable_cap::cull_face);
            m_cache.face_cull_enable = false;
        }
    }

    if (m_cache.front_face_direction != state.front_face_direction) {
        gl::front_face(state.front_face_direction);
        m_cache.front_face_direction = state.front_face_direction;
    }

    if (m_cache.polygon_mode != state.polygon_mode) {
        gl::polygon_mode(gl::Material_face::front_and_back, state.polygon_mode);
        m_cache.polygon_mode = state.polygon_mode;
    }
#endif
}

auto operator==(
    const Rasterization_state& lhs,
    const Rasterization_state& rhs
) noexcept -> bool
{
    return
        (lhs.face_cull_enable     == rhs.face_cull_enable    ) &&
        (lhs.cull_face_mode       == rhs.cull_face_mode      ) &&
        (lhs.front_face_direction == rhs.front_face_direction) &&
        (lhs.polygon_mode         == rhs.polygon_mode        );
}

auto operator!=(
    const Rasterization_state& lhs,
    const Rasterization_state& rhs
) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
