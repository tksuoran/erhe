#pragma once

#include "erhe_physics/iconstraint.hpp"
#include "erhe_physics/transform.hpp"

#include <glm/glm.hpp>

#include <array>

namespace erhe::physics {

// Which side of a six-DOF joint (Six_dof_constraint_settings rigid_body_a /
// rigid_body_b) the moving body is. The other side is a fixed anchor frame:
// the world, or a body that does not move while the reach is used.
enum class Joint_side : unsigned int {
    a = 0,
    b = 1
};

// The set of world positions a point rigidly attached to the moving body can
// take while the joint holds.
enum class Joint_reach_shape : unsigned int {
    unprojected = 0, // the combination of free axes is not handled; project() returns the target
    point       = 1, // nothing moves the point: every axis fixed, or the point lies on the free rotation axis / at the ball center
    circle      = 2, // one rotation axis free or ranged, translation fixed; the angular range is applied
    sphere      = 3, // two or three rotation axes free or ranged, translation fixed; angular limits are NOT applied
    box         = 4  // rotation fixed, one or more translation axes free or ranged; per-axis ranges applied
};

[[nodiscard]] auto c_str(Joint_reach_shape shape) -> const char*;

// Projection of a requested target position onto the positions a point of a
// jointed body can reach, for interactive drags that must not pull against
// the joint.
//
// Joint convention (the one Node_joint builds Six_dof_constraint_settings
// with and erhe::physics backends solve): with anchor frames F_a (on body A)
// and F_b (on body B) in world space, the joint coordinates are the offset
// t = F_a.basis^T * (F_b.origin - F_a.origin) and the rotation R with
// F_b.basis = F_a.basis * R; limits 0..2 constrain t, limits 3..5 the
// rotation vector of R. An axis is fixed when limited with max - min below
// c_fixed_axis_epsilon.
//
// With every translation axis fixed (t constant) the point's position in the
// fixed anchor frame is:
//   moving side a: q = R^T * (p - t)
//   moving side b: q = t + R * p
// where p is the point in the moving anchor frame. With every rotation axis
// fixed (R = I):
//   moving side a: q = p - t,   side b: q = p + t
// so each translation range becomes a per-axis clamp of q.
class Joint_reach
{
public:
    static constexpr float c_fixed_axis_epsilon = 1.0e-6f;
    static constexpr float c_radius_epsilon     = 1.0e-5f;

    // world_from_fixed_anchor: the fixed side's anchor frame in world space.
    // point_in_moving_anchor:  the projected point in the moving side's anchor
    //                          frame (constant while the joint holds).
    // point_in_world_now:      where the point is now; seeds the last
    //                          projected point (used when a target has no
    //                          unique nearest reachable point).
    void configure(
        const Transform&                            world_from_fixed_anchor,
        Joint_side                                  moving_side,
        const std::array<Constraint_axis_limit, 6>& limits,
        glm::vec3                                   point_in_moving_anchor,
        glm::vec3                                   point_in_world_now
    );

    // Nearest reachable position to target_in_world (world space). A target
    // with no unique nearest position (on the free rotation axis, at the ball
    // center) returns the last projected position.
    auto project(glm::vec3 target_in_world) -> glm::vec3;

    [[nodiscard]] auto get_shape      () const -> Joint_reach_shape;
    [[nodiscard]] auto get_axis       () const -> int;       // rotation axis 0..2 of a circle, -1 otherwise
    [[nodiscard]] auto get_radius     () const -> float;     // circle / sphere radius
    [[nodiscard]] auto get_center     () const -> glm::vec3; // circle / sphere center, in world space
    [[nodiscard]] auto get_last_projected() const -> glm::vec3;

private:
    [[nodiscard]] auto project_local(glm::vec3 target_in_fixed_anchor, glm::vec3 fallback) const -> glm::vec3;

    Transform         m_world_from_fixed_anchor{};
    Transform         m_fixed_anchor_from_world{};
    Joint_reach_shape m_shape       {Joint_reach_shape::unprojected};
    int               m_axis        {-1};
    float             m_sign        {1.0f};     // rotation of the point per joint angle: -1 moving side a, +1 side b
    glm::vec3         m_center      {0.0f};     // fixed anchor frame
    glm::vec3         m_vector      {0.0f};     // rotated vector from the center, fixed anchor frame at R = I
    glm::vec3         m_box_min     {0.0f};     // fixed anchor frame
    glm::vec3         m_box_max     {0.0f};
    float             m_angle_min   {0.0f};
    float             m_angle_max   {0.0f};
    bool              m_angle_limited{false};
    glm::vec3         m_last_projected{0.0f};   // world space
};

} // namespace erhe::physics
