#include "erhe/graphics/state/rasterization_state.hpp"
#include "erhe/gl/gl.hpp"
#include "erhe/toolkit/verify.hpp"

// #define DISABLE_CACHE 1

namespace erhe::graphics
{

size_t Rasterization_state::s_serial{0};

Rasterization_state Rasterization_state::cull_mode_none           {false, gl::Cull_face_mode::back,           gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_front          {true,  gl::Cull_face_mode::front,          gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_back_cw        {true,  gl::Cull_face_mode::back,           gl::Front_face_direction::cw,  gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_back_ccw       {true,  gl::Cull_face_mode::back,           gl::Front_face_direction::ccw, gl::Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_front_and_back {true,  gl::Cull_face_mode::front_and_back, gl::Front_face_direction::ccw, gl::Polygon_mode::fill};

void Rasterization_state_tracker::reset()
{
    gl::disable(gl::Enable_cap::cull_face);
    gl::cull_face(gl::Cull_face_mode::back);
    gl::front_face(gl::Front_face_direction::ccw);
    gl::polygon_mode(gl::Material_face::front_and_back, gl::Polygon_mode::fill);
    m_cache = Rasterization_state{};
    m_last = 0;
}

void Rasterization_state_tracker::execute(Rasterization_state const* state)
{
    VERIFY(state != nullptr);

#if !DISABLE_CACHE
    if (m_last == state->serial)
    {
        return;
    }

#endif
    if (state->enabled)
    {
#if !DISABLE_CACHE
        if (!m_cache.enabled)
#endif
        {
            gl::enable(gl::Enable_cap::cull_face);
            m_cache.enabled = true;
        }
#if !DISABLE_CACHE
        if (m_cache.cull_face_mode != state->cull_face_mode)
#endif
        {
            gl::cull_face(state->cull_face_mode);
            m_cache.cull_face_mode = state->cull_face_mode;
        }
    }
    else
    {
#if !DISABLE_CACHE
        if (m_cache.enabled)
#endif
        {
            gl::disable(gl::Enable_cap::cull_face);
            m_cache.enabled = false;
        }
    }

#if !DISABLE_CACHE
    if (m_cache.front_face_direction != state->front_face_direction)
#endif
    {
        gl::front_face(state->front_face_direction);
        m_cache.front_face_direction = state->front_face_direction;
    }

#if !DISABLE_CACHE
    if (m_cache.polygon_mode != state->polygon_mode)
#endif
    {
        gl::polygon_mode(gl::Material_face::front_and_back, state->polygon_mode);
        m_cache.polygon_mode = state->polygon_mode;
    }
    m_last = state->serial;
}

auto operator==(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept
-> bool
{
    return (lhs.enabled              == rhs.enabled             ) &&
           (lhs.cull_face_mode       == rhs.cull_face_mode      ) &&
           (lhs.front_face_direction == rhs.front_face_direction) &&
           (lhs.polygon_mode         == rhs.polygon_mode        );
}

auto operator!=(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept
-> bool
{
    return !(lhs == rhs);
}

} // namespace erhe::graphics
