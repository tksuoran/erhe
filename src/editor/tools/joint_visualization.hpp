#pragma once

#include "transform/ik_solver.hpp"

#include "erhe_physics/joint_limits.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <vector>

namespace editor {

// Joint constraint visualization geometry
// (doc/editor/tools.md "Debug_visualizations"): pure builders with no
// scene, physics world or renderer access, so they are unit testable; the
// drawing owner (Debug_visualizations) gathers the inputs and hands the lines
// to erhe::renderer::Primitive_renderer.

// One world-space line segment with its color and width (negative = pixels).
class Joint_line
{
public:
    glm::vec3 p0   {0.0f};
    glm::vec3 p1   {0.0f};
    glm::vec4 color{1.0f};
    float     width{-1.0f};
};

// Caller-owned, cleared and refilled at the point of use, so a steady-state
// frame allocates nothing once the high-water mark is reached (AGENTS.md
// "Run-time Memory Allocation Discipline"). The builders append.
class Joint_line_buffer
{
public:
    std::vector<Joint_line> lines;

    void clear() { lines.clear(); } // keeps capacity
};

// The colors and widths of the joint visuals, copied from
// Debug_visualizations_style by the drawing owner.
class Joint_line_style
{
public:
    glm::vec4 limit_color       {1.0f, 0.6f, 0.1f, 1.0f};
    glm::vec4 free_color        {0.6f, 0.6f, 0.6f, 0.6f};
    glm::vec4 value_color       {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 violation_color   {1.0f, 0.1f, 0.1f, 1.0f};
    glm::vec4 pending_color     {0.5f, 0.5f, 0.5f, 0.5f};
    glm::vec4 approximated_color{1.0f, 0.2f, 1.0f, 1.0f};
    glm::vec4 body_link_color   {0.4f, 0.8f, 1.0f, 0.8f};
    glm::vec4 swing_color       {0.2f, 0.9f, 0.6f, 1.0f};
    glm::vec4 twist_color       {1.0f, 0.9f, 0.2f, 1.0f};
    float     line_width        {-2.0f};
    float     thin_line_width   {-1.0f};
    int       arc_segments      {24};
};

// Everything one physics joint's visual needs, in world space
// (doc/editor/tools.md "Debug_visualizations").
class Physics_joint_line_input
{
public:
    erhe::physics::Transform           frame_a{};         // anchor frame on body A (orthonormal basis)
    erhe::physics::Transform           frame_b{};         // anchor frame on body B, or the world anchor
    std::optional<glm::vec3>           body_a_origin{};   // unset: no body link drawn
    std::optional<glm::vec3>           body_b_origin{};
    // The constrained body's arm: body A's center relative to the joint
    // pivot, in frame A. The rotation limits are drawn as where this arm can
    // go, fixed to frame B; unset (or too short) draws them with the frame
    // axis the limit moves instead.
    std::optional<glm::vec3>           arm_in_a{};
    erhe::physics::Joint_limit_shape   shape{};           // the limits the backend enforces
    erhe::physics::Joint_coordinates   coordinates{};     // measure_joint_coordinates(frame_a, frame_b, shape)
    erhe::physics::Joint_range_check   range_check{};
    bool                               live{true};        // false: pending, everything in the pending color
    float                              size{0.25f};       // world-space size of the visual
    bool                               draw_frames{true}; // false: the frame triads and body links another pass already drew
    Joint_line_style                   style{};
};

// Appends one physics joint: axis triads at both frames (B's shorter), body
// links, translation axes along frame A (free: thin line, limited: segment
// with end ticks, fixed: nothing) with a cross at frame B's origin, and the
// rotation limits as where the body's arm (arm_in_a) can go, fixed to
// frame B: the swing boundary (pyramid: the edges of the swing angle
// rectangle with spokes to its corners; cone: a circle with four spokes), the
// twist range arc, and the current arm as spokes in the value or violation
// color.
void build_physics_joint_lines(const Physics_joint_line_input& input, Joint_line_buffer& buffer);

// Everything one IK-constrained bone's visual needs
// (doc/editor/tools.md "Debug_visualizations").
class Ik_limit_line_input
{
public:
    glm::vec3                  head           {0.0f};             // bone head, world space
    glm::quat                  world_from_rest{1.0f, 0.0f, 0.0f, 0.0f}; // parent world rotation * constraint rest rotation
    const Ik_joint_constraint* constraint     {nullptr};
    const Ik_swing_boundary*   boundary       {nullptr};          // sample_ik_swing_boundary() of the same constraint
    Ik_rotation_components     current        {};                 // decompose_ik_rotation() of the current local rotation
    bool                       within_limits  {true};
    float                      twist_sign     {1.0f};             // sign of the child direction along the twist axis
    float                      length         {0.25f};            // swing boundary radius (the bone length)
    Joint_line_style           style{};
};

// Appends one IK bone: the swing boundary polylines at bone length with
// spokes to their ends (a closed loop gets four), the current bone direction
// as a spoke in the value or violation color, and - when the twist axis is
// limited and not locked - the twist range arc with its current twist tick.
void build_ik_limit_lines(const Ik_limit_line_input& input, Joint_line_buffer& buffer);

} // namespace editor
