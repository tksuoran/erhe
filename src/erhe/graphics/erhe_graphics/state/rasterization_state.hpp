#pragma once

#include "erhe_graphics/enums.hpp"

#include <cstddef>

namespace erhe::graphics {

class Rasterization_state
{
public:
    // rasterizer_discard_enable
    // depth bias
    // line width

    bool                 depth_clamp_enable  {false};
    bool                 face_cull_enable    {true};
    Cull_face_mode       cull_face_mode      {Cull_face_mode::back};
    Front_face_direction front_face_direction{Front_face_direction::ccw};
    Polygon_mode         polygon_mode        {Polygon_mode::fill};
    // not implementing separate front and back polygon modes for now

    static Rasterization_state cull_mode_none_depth_clamp;
    static Rasterization_state cull_mode_none;
    static Rasterization_state cull_mode_front_cw;
    static Rasterization_state cull_mode_front_ccw;
    static Rasterization_state cull_mode_back_cw;
    static Rasterization_state cull_mode_back_ccw;
    static Rasterization_state cull_mode_front_and_back;
};

class Rasterization_state_hash
{
public:
    [[nodiscard]] auto operator()( const Rasterization_state& rasterization_state) noexcept -> std::size_t;
};

[[nodiscard]] auto operator==(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept -> bool;
[[nodiscard]] auto operator!=(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept -> bool;


} // namespace erhe::graphics
