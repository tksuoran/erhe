#include "erhe_graphics/state/rasterization_state.hpp"
#include "erhe_gl/enum_base_zero_functions.hpp"
#include "erhe_gl/wrapper_functions.hpp"

// #define DISABLE_CACHE 1

namespace erhe::graphics {

auto Rasterization_state_hash::operator()(const Rasterization_state& rasterization_state) noexcept -> std::size_t
{
    return
        (rasterization_state.depth_clamp_enable ? 1u : 0u)              |
        (rasterization_state.face_cull_enable ? 2u : 0u)                |
        (gl::base_zero(rasterization_state.cull_face_mode      ) << 2u) | // 2 bits
        (gl::base_zero(rasterization_state.front_face_direction) << 4u) | // 1 bit
        (gl::base_zero(rasterization_state.polygon_mode        ) << 5u);  // 2 bits
}

Rasterization_state Rasterization_state::cull_mode_none_depth_clamp{true,  false, gl::Cull_face_mode::back,  gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_none            {false, false, gl::Cull_face_mode::back,  gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_front_cw        {false, true,  gl::Cull_face_mode::front, gl::Front_face_direction::cw,  gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_front_ccw       {false, true,  gl::Cull_face_mode::front, gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_back_cw         {false, true,  gl::Cull_face_mode::back,  gl::Front_face_direction::cw,  gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_back_ccw        {false, true,  gl::Cull_face_mode::back,  gl::Front_face_direction::ccw, gl::Polygon_mode::fill};

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
    if (state.depth_clamp_enable) {
        gl::enable(gl::Enable_cap::depth_clamp);
    } else {
        gl::disable(gl::Enable_cap::depth_clamp);
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

    if (state.depth_clamp_enable) {
        gl::enable(gl::Enable_cap::depth_clamp);
        m_cache.depth_clamp_enable = true;
    } else {
        gl::disable(gl::Enable_cap::depth_clamp);
        m_cache.depth_clamp_enable = false;
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

auto operator==(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept -> bool
{
    return
        (lhs.face_cull_enable     == rhs.face_cull_enable    ) &&
        (lhs.depth_clamp_enable   == rhs.depth_clamp_enable  ) &&
        (lhs.cull_face_mode       == rhs.cull_face_mode      ) &&
        (lhs.front_face_direction == rhs.front_face_direction) &&
        (lhs.polygon_mode         == rhs.polygon_mode        );
}

auto operator!=(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept -> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
