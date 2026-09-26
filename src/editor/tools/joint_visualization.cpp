#include "tools/joint_visualization.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace editor {

using glm::quat;
using glm::vec3;
using glm::vec4;

namespace {

const vec4 c_axis_colors[3] = {
    vec4{1.0f, 0.0f, 0.0f, 1.0f},
    vec4{0.0f, 1.0f, 0.0f, 1.0f},
    vec4{0.0f, 0.0f, 1.0f, 1.0f}
};

[[nodiscard]] auto unit_axis(const int axis) -> vec3
{
    vec3 v{0.0f};
    v[axis] = 1.0f;
    return v;
}

void add_line(Joint_line_buffer& buffer, const vec3 p0, const vec3 p1, const vec4& color, const float width)
{
    buffer.lines.push_back(Joint_line{.p0 = p0, .p1 = p1, .color = color, .width = width});
}

// Three-axis cross of half-size arm at p.
void add_cross(Joint_line_buffer& buffer, const vec3 p, const glm::mat3& basis, const float arm, const vec4& color, const float width)
{
    for (int axis = 0; axis < 3; ++axis) {
        const vec3 d = basis[axis] * arm;
        add_line(buffer, p - d, p + d, color, width);
    }
}

[[nodiscard]] auto is_fixed(const erhe::physics::Constraint_axis_limit& limit) -> bool
{
    return limit.limited && (limit.min >= limit.max);
}

// The range an axis covers: its limit, or [-pi, pi] when free.
[[nodiscard]] auto angle_range(const erhe::physics::Constraint_axis_limit& limit) -> std::array<float, 2>
{
    if (!limit.limited) {
        return {-glm::pi<float>(), glm::pi<float>()};
    }
    return {limit.min, limit.max};
}

class Physics_joint_colors
{
public:
    vec4 limit;
    vec4 free;
    vec4 body_link;
    std::array<vec4, 3> axes;
};

[[nodiscard]] auto value_color(const Physics_joint_line_input& input, const bool ok) -> vec4
{
    if (!input.live) {
        return input.style.pending_color;
    }
    return ok ? input.style.value_color : input.style.violation_color;
}

void add_translation_axes(const Physics_joint_line_input& input, const Physics_joint_colors& colors, Joint_line_buffer& buffer)
{
    const Joint_line_style& style  = input.style;
    const vec3              origin = input.frame_a.origin;
    bool any_movable = false;
    for (int axis = 0; axis < 3; ++axis) {
        const erhe::physics::Constraint_axis_limit& limit = input.shape.translation[static_cast<std::size_t>(axis)];
        const vec3 direction = input.frame_a.basis[axis];
        if (!limit.limited) {
            add_line(buffer, origin - (direction * input.size), origin + (direction * input.size), colors.free, style.thin_line_width);
            any_movable = true;
            continue;
        }
        if (is_fixed(limit)) {
            continue;
        }
        any_movable = true;
        const vec3 p_min = origin + (direction * limit.min);
        const vec3 p_max = origin + (direction * limit.max);
        add_line(buffer, p_min, p_max, colors.limit, style.line_width);
        const vec3 tick = input.frame_a.basis[(axis + 1) % 3] * (0.1f * input.size);
        add_line(buffer, p_min - tick, p_min + tick, colors.limit, style.line_width);
        add_line(buffer, p_max - tick, p_max + tick, colors.limit, style.line_width);
    }
    if (any_movable) {
        const bool ok = input.range_check.translation_ok[0] && input.range_check.translation_ok[1] && input.range_check.translation_ok[2];
        add_cross(buffer, input.frame_b.origin, input.frame_a.basis, 0.08f * input.size, value_color(input, ok), style.line_width);
    }
}

// The rotation limits are drawn as where the constrained body's ARM can go:
// the vector from the joint pivot to body A's center, held in frame A. At
// the relative rotation q = inverse(frame A) * frame B, frame A is
// frame B * inverse(q), so a limit rotation q puts the arm at
// frame_b * inverse(q) * arm. The limits are thereby drawn fixed to frame B
// (the anchor side), centered on the body however the joint frames are
// oriented, and the current arm is the spoke to the body's center.

[[nodiscard]] auto rotation_of(const glm::mat3& basis) -> quat
{
    return glm::normalize(glm::quat_cast(basis));
}

// The arm the swing limits sweep, in frame A: the body arm, or - when the
// body center sits on the pivot and there is no arm - the twist axis at the
// visual size, which is the frame axis the swing limits move.
[[nodiscard]] auto swing_arm(const Physics_joint_line_input& input) -> vec3
{
    if (input.arm_in_a.has_value() && (glm::length(input.arm_in_a.value()) > (0.05f * input.size))) {
        return input.arm_in_a.value();
    }
    return unit_axis(input.shape.twist_axis) * input.size;
}

// The vector the twist limit sweeps, in frame A: the part of the body arm
// across the twist axis, or a frame axis across it when the arm runs along
// the twist axis (a twist does not move such an arm).
[[nodiscard]] auto twist_arm(const Physics_joint_line_input& input) -> vec3
{
    const vec3 e_t = unit_axis(input.shape.twist_axis);
    if (input.arm_in_a.has_value()) {
        const vec3 arm    = input.arm_in_a.value();
        const vec3 across = arm - (e_t * glm::dot(arm, e_t));
        if (glm::length(across) > (0.05f * input.size)) {
            return across;
        }
    }
    const std::array<int, 2> swing_axes = erhe::physics::get_swing_axes(input.shape.twist_axis);
    return unit_axis(swing_axes[0]) * (0.6f * input.size);
}

// Where arm (in frame A) lands when the joint is at relative rotation q.
[[nodiscard]] auto arm_point(const Physics_joint_line_input& input, const quat& q, const vec3 arm) -> vec3
{
    return input.frame_b.origin + (rotation_of(input.frame_b.basis) * (glm::inverse(q) * arm));
}

// Where arm is now: carried by frame A.
[[nodiscard]] auto current_arm_point(const Physics_joint_line_input& input, const vec3 arm) -> vec3
{
    return input.frame_a.origin + (rotation_of(input.frame_a.basis) * arm);
}

void add_pyramid_swing(const Physics_joint_line_input& input, const Physics_joint_colors& colors, Joint_line_buffer& buffer)
{
    const erhe::physics::Joint_limit_shape& shape = input.shape;
    const Joint_line_style&                 style = input.style;
    const bool free_0 = !shape.swing[0].limited;
    const bool free_1 = !shape.swing[1].limited;
    if (free_0 && free_1) {
        return; // the arm may point anywhere the twist leaves it
    }
    if (is_fixed(shape.swing[0]) && is_fixed(shape.swing[1])) {
        return; // no swing: the current-arm spoke alone shows the arm
    }
    const std::array<float, 2> range_0 = angle_range(shape.swing[0]);
    const std::array<float, 2> range_1 = angle_range(shape.swing[1]);
    const int  segments = std::max(1, style.arc_segments);
    const vec3 arm      = swing_arm(input);
    const auto point_at = [&](const float a0, const float a1) -> vec3 {
        return arm_point(input, erhe::physics::pyramid_swing_rotation(shape.twist_axis, a0, a1), arm);
    };

    // An edge of the swing angle rectangle: one swing angle held at `held`,
    // the other swept over its range.
    const auto add_edge = [&](const int held_k, const float held, const std::array<float, 2>& swept) {
        if (swept[1] <= swept[0]) {
            return; // sweeping a fixed axis: the edge is a single point
        }
        vec3 previous{0.0f};
        for (int i = 0; i <= segments; ++i) {
            const float t     = static_cast<float>(i) / static_cast<float>(segments);
            const float sweep = swept[0] + ((swept[1] - swept[0]) * t);
            const vec3  p     = (held_k == 0) ? point_at(held, sweep) : point_at(sweep, held);
            if (i > 0) {
                add_line(buffer, previous, p, colors.limit, style.line_width);
            }
            previous = p;
        }
    };
    if (!free_0) {
        add_edge(0, range_0[0], range_1);
        if (!is_fixed(shape.swing[0])) {
            add_edge(0, range_0[1], range_1);
        }
    }
    if (!free_1) {
        add_edge(1, range_1[0], range_0);
        if (!is_fixed(shape.swing[1])) {
            add_edge(1, range_1[1], range_0);
        }
    }
    // Spokes to the corners of the limited ranges.
    if (!free_0 && !free_1) {
        for (const float a0 : range_0) {
            for (const float a1 : range_1) {
                add_line(buffer, input.frame_b.origin, point_at(a0, a1), colors.limit, style.thin_line_width);
            }
        }
    }
}

void add_cone_swing(const Physics_joint_line_input& input, const Physics_joint_colors& colors, Joint_line_buffer& buffer)
{
    const erhe::physics::Joint_limit_shape& shape = input.shape;
    if (!shape.cone.limited) {
        return;
    }
    const Joint_line_style&  style      = input.style;
    const std::array<int, 2> swing_axes = erhe::physics::get_swing_axes(shape.twist_axis);
    const vec3  e_a      = unit_axis(swing_axes[0]);
    const vec3  e_b      = unit_axis(swing_axes[1]);
    const vec3  arm      = swing_arm(input);
    const int   segments = std::max(4, 4 * (style.arc_segments / 4));
    vec3 previous{0.0f};
    for (int i = 0; i <= segments; ++i) {
        // A swing of the cone's half-angle about an axis across the twist axis.
        const float phi   = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const quat  swing = glm::angleAxis(shape.cone.max, (e_a * std::cos(phi)) + (e_b * std::sin(phi)));
        const vec3  p     = arm_point(input, swing, arm);
        if (i > 0) {
            add_line(buffer, previous, p, colors.limit, style.line_width);
        }
        if ((i % (segments / 4)) == 0) {
            add_line(buffer, input.frame_b.origin, p, colors.limit, style.thin_line_width);
        }
        previous = p;
    }
}

void add_twist(const Physics_joint_line_input& input, const Physics_joint_colors& colors, Joint_line_buffer& buffer)
{
    const erhe::physics::Joint_limit_shape& shape = input.shape;
    const Joint_line_style&                 style = input.style;
    if (is_fixed(shape.twist)) {
        return;
    }
    const vec3 e_t      = unit_axis(shape.twist_axis);
    const vec3 arm      = twist_arm(input);
    const vec3 origin   = input.frame_b.origin;
    const auto point_at = [&](const float angle) -> vec3 {
        return arm_point(input, glm::angleAxis(angle, e_t), arm);
    };
    const std::array<float, 2> range = angle_range(shape.twist);
    const int   segments = std::max(1, style.arc_segments);
    const vec4& color    = shape.twist.limited ? colors.limit : colors.free;
    const float width    = shape.twist.limited ? style.line_width : style.thin_line_width;
    vec3 previous{0.0f};
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const vec3  p = point_at(range[0] + ((range[1] - range[0]) * t));
        if (i > 0) {
            add_line(buffer, previous, p, color, width);
        }
        previous = p;
    }
    if (shape.twist.limited) {
        add_line(buffer, origin, point_at(range[0]), colors.limit, style.thin_line_width);
        add_line(buffer, origin, point_at(range[1]), colors.limit, style.thin_line_width);
    }
    add_line(buffer, origin, current_arm_point(input, arm), value_color(input, input.range_check.twist_ok), style.line_width);
}

} // anonymous namespace

void build_physics_joint_lines(const Physics_joint_line_input& input, Joint_line_buffer& buffer)
{
    const Joint_line_style& style = input.style;
    Physics_joint_colors colors{};
    if (!input.live) {
        colors.limit     = style.pending_color;
        colors.free      = style.pending_color;
        colors.body_link = style.pending_color;
        colors.axes      = {style.pending_color, style.pending_color, style.pending_color};
    } else {
        colors.limit     = input.shape.is_exact ? style.limit_color : style.approximated_color;
        colors.free      = style.free_color;
        colors.body_link = style.body_link_color;
        colors.axes      = {c_axis_colors[0], c_axis_colors[1], c_axis_colors[2]};
    }

    // Anchor frames.
    for (int axis = 0; axis < 3; ++axis) {
        add_line(buffer, input.frame_a.origin, input.frame_a.origin + (input.frame_a.basis[axis] * (0.5f  * input.size)), colors.axes[static_cast<std::size_t>(axis)], style.line_width);
        add_line(buffer, input.frame_b.origin, input.frame_b.origin + (input.frame_b.basis[axis] * (0.35f * input.size)), colors.axes[static_cast<std::size_t>(axis)], style.thin_line_width);
    }
    if (input.body_a_origin.has_value()) {
        add_line(buffer, input.frame_a.origin, input.body_a_origin.value(), colors.body_link, style.thin_line_width);
    }
    if (input.body_b_origin.has_value()) {
        add_line(buffer, input.frame_b.origin, input.body_b_origin.value(), colors.body_link, style.thin_line_width);
    }

    add_translation_axes(input, colors, buffer);

    if (input.shape.swing_model == erhe::physics::Swing_limit_model::pyramid) {
        add_pyramid_swing(input, colors, buffer);
    } else {
        add_cone_swing(input, colors, buffer);
    }
    // Current swing: where the arm is now.
    const bool swing_movable =
        (input.shape.swing_model == erhe::physics::Swing_limit_model::cone) ||
        !is_fixed(input.shape.swing[0]) || !is_fixed(input.shape.swing[1]);
    if (swing_movable) {
        add_line(
            buffer,
            input.frame_b.origin,
            current_arm_point(input, swing_arm(input)),
            value_color(input, input.range_check.swing_ok),
            style.line_width
        );
    }

    add_twist(input, colors, buffer);
}

void build_ik_limit_lines(const Ik_limit_line_input& input, Joint_line_buffer& buffer)
{
    if ((input.constraint == nullptr) || (input.constraint->twist_axis < 0)) {
        return;
    }
    const Ik_joint_constraint& constraint = *input.constraint;
    const Joint_line_style&    style      = input.style;
    const int                  twist_axis = constraint.twist_axis;
    const vec3                 e_t        = unit_axis(twist_axis) * input.twist_sign;
    const auto direction_point = [&](const quat& swing) -> vec3 {
        return input.head + (input.world_from_rest * (swing * e_t)) * input.length;
    };

    // Swing boundary.
    if (input.boundary != nullptr) {
        const Ik_swing_boundary& boundary = *input.boundary;
        std::size_t begin = 0;
        for (const std::size_t end : boundary.polyline_ends) {
            if (end <= begin) {
                continue;
            }
            for (std::size_t i = begin + 1; i < end; ++i) {
                add_line(buffer, direction_point(boundary.swings[i - 1]), direction_point(boundary.swings[i]), style.swing_color, style.line_width);
            }
            const bool closed = (end - begin) > 2 && (std::abs(glm::dot(boundary.swings[begin], boundary.swings[end - 1])) > 0.99999f);
            if (closed) {
                const std::size_t count = end - begin - 1;
                for (std::size_t q = 0; q < 4; ++q) {
                    add_line(buffer, input.head, direction_point(boundary.swings[begin + ((q * count) / 4)]), style.swing_color, style.thin_line_width);
                }
            } else {
                add_line(buffer, input.head, direction_point(boundary.swings[begin]),   style.swing_color, style.thin_line_width);
                add_line(buffer, input.head, direction_point(boundary.swings[end - 1]), style.swing_color, style.thin_line_width);
            }
            begin = end;
        }
    }

    const vec4& current_color = input.within_limits ? style.value_color : style.violation_color;
    add_line(buffer, input.head, direction_point(input.current.swing), current_color, style.line_width);

    // Twist range, about the current (swung) twist axis.
    if (constraint.limit[twist_axis] && !constraint.lock[twist_axis]) {
        const std::array<int, 2> swing_axes = erhe::physics::get_swing_axes(twist_axis);
        const vec3  e_axis  = unit_axis(twist_axis);
        const vec3  e_ref   = unit_axis(swing_axes[0]);
        const float radius  = 0.3f * input.length;
        const quat  q_frame = input.world_from_rest * input.current.swing;
        const auto  point_at = [&](const float angle) -> vec3 {
            return input.head + (q_frame * (glm::angleAxis(angle, e_axis) * e_ref)) * radius;
        };
        const float min      = constraint.limit_min[twist_axis];
        const float max      = constraint.limit_max[twist_axis];
        const int   segments = std::max(1, style.arc_segments);
        vec3 previous{0.0f};
        for (int i = 0; i <= segments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const vec3  p = point_at(min + ((max - min) * t));
            if (i > 0) {
                add_line(buffer, previous, p, style.twist_color, style.line_width);
            }
            previous = p;
        }
        add_line(buffer, input.head, point_at(min), style.twist_color, style.thin_line_width);
        add_line(buffer, input.head, point_at(max), style.twist_color, style.thin_line_width);
        add_line(buffer, input.head, point_at(input.current.twist_angle), current_color, style.line_width);
    }
}

} // namespace editor
