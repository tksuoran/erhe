#include "erhe_graphics/state/rasterization_state.hpp"

namespace erhe::graphics {

auto Rasterization_state_hash::operator()(const Rasterization_state& rasterization_state) noexcept -> std::size_t
{
    return
        (rasterization_state.depth_clamp_enable ? 1u : 0u)                    |
        (rasterization_state.face_cull_enable   ? 2u : 0u)                    |
        (static_cast<size_t>(rasterization_state.cull_face_mode      ) << 2u) | // 2 bits
        (static_cast<size_t>(rasterization_state.front_face_direction) << 4u) | // 1 bit
        (static_cast<size_t>(rasterization_state.polygon_mode        ) << 5u);  // 2 bits
}

Rasterization_state Rasterization_state::cull_mode_none_depth_clamp{true,  false, Cull_face_mode::back,  Front_face_direction::ccw, Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_none            {false, false, Cull_face_mode::back,  Front_face_direction::ccw, Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_front_cw        {false, true,  Cull_face_mode::front, Front_face_direction::cw,  Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_front_ccw       {false, true,  Cull_face_mode::front, Front_face_direction::ccw, Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_back_cw         {false, true,  Cull_face_mode::back,  Front_face_direction::cw,  Polygon_mode::fill};
Rasterization_state Rasterization_state::cull_mode_back_ccw        {false, true,  Cull_face_mode::back,  Front_face_direction::ccw, Polygon_mode::fill};

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
