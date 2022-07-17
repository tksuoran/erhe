#pragma once

#include "erhe/gl/wrapper_enums.hpp"

#include <cstddef>

namespace erhe::graphics
{

class Rasterization_state
{
public:
    // depth_clamp_enable
    // rasterizer_discard_enable
    // depth bias
    // line width

    bool                     face_cull_enable    {true};
    gl::Cull_face_mode       cull_face_mode      {gl::Cull_face_mode::back};
    gl::Front_face_direction front_face_direction{gl::Front_face_direction::ccw};
    gl::Polygon_mode         polygon_mode        {gl::Polygon_mode::fill};
    // not implementing separate front and back polygon modes for now

    static auto cull_mode_front_cw (const bool reverse_depth) -> const Rasterization_state&;
    static auto cull_mode_front_ccw(const bool reverse_depth) -> const Rasterization_state&;
    static auto cull_mode_back_cw  (const bool reverse_depth) -> const Rasterization_state&;
    static auto cull_mode_back_ccw (const bool reverse_depth) -> const Rasterization_state&;

    static Rasterization_state cull_mode_none;
    static Rasterization_state s_cull_mode_front_cw;
    static Rasterization_state s_cull_mode_front_ccw;
    static Rasterization_state s_cull_mode_back_cw;
    static Rasterization_state s_cull_mode_back_ccw;
    static Rasterization_state cull_mode_front_and_back;
};

class Rasterization_state_hash
{
public:
    [[nodiscard]]
    auto operator()(
        const Rasterization_state& rasterization_state
    ) noexcept -> std::size_t;
};

[[nodiscard]] auto operator==(
    const Rasterization_state& lhs,
    const Rasterization_state& rhs
) noexcept -> bool;

[[nodiscard]] auto operator!=(
    const Rasterization_state& lhs,
    const Rasterization_state& rhs
) noexcept -> bool;

class Rasterization_state_tracker
{
public:
    void reset  ();
    void execute(const Rasterization_state& state);

private:
    Rasterization_state m_cache;
};

} // namespace erhe::graphics
