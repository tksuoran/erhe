#pragma once

#include "erhe/gl/enum_base_zero_functions.hpp"

namespace erhe::graphics
{

struct Rasterization_state
{
    Rasterization_state();
    Rasterization_state(bool                     enabled,
                        gl::Cull_face_mode       cull_face_mode,
                        gl::Front_face_direction front_face_direction,
                        gl::Polygon_mode         polygon_mode);

    void touch();

    // depth_clamp_enable
    // rasterizer_discard_enable
    // depth bias
    // line width

    size_t                   serial;
    bool                     enabled             {false};
    gl::Cull_face_mode       cull_face_mode      {gl::Cull_face_mode::back};
    gl::Front_face_direction front_face_direction{gl::Front_face_direction::ccw};
    gl::Polygon_mode         polygon_mode        {gl::Polygon_mode::fill};
    // not implementing separate front and back polygon modes for now

    static auto get_next_serial() -> size_t;

    static size_t              s_serial;
    static Rasterization_state cull_mode_none;
    static Rasterization_state cull_mode_front;
    static Rasterization_state cull_mode_back_cw;
    static Rasterization_state cull_mode_back_ccw;
    static Rasterization_state cull_mode_front_and_back;
};

struct Rasterization_state_hash
{
    auto operator()(const Rasterization_state& rasterization_state) noexcept -> std::size_t;
};

auto operator==(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept
-> bool;

auto operator!=(const Rasterization_state& lhs, const Rasterization_state& rhs) noexcept
-> bool;

class Rasterization_state_tracker
{
public:
    void reset  ();
    void execute(const Rasterization_state* state);

private:
    size_t              m_last{0};
    Rasterization_state m_cache;
};

} // namespace erhe::graphics
