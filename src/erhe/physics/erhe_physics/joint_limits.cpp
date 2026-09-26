#include "erhe_physics/joint_limits.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace erhe::physics {

namespace {

constexpr float c_epsilon = 1.0e-6f;

// Swing-twist decomposition of q about coordinate axis twist_axis:
// q == swing * twist, twist a rotation about that axis, both canonicalized to
// w >= 0. At a 180 degree swing (no twist component) the twist is identity.
void swing_twist_decompose(const glm::quat& q, const int twist_axis, glm::quat& swing, glm::quat& twist)
{
    const float proj    = (twist_axis == 0) ? q.x : ((twist_axis == 1) ? q.y : q.z);
    const float len2    = (q.w * q.w) + (proj * proj);
    if (len2 < (c_epsilon * c_epsilon)) {
        twist = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
        swing = q;
    } else {
        const float inv_len = 1.0f / std::sqrt(len2);
        glm::vec3 twist_vector{0.0f};
        twist_vector[twist_axis] = proj * inv_len;
        twist = glm::quat{q.w * inv_len, twist_vector.x, twist_vector.y, twist_vector.z};
        swing = q * glm::inverse(twist);
    }
    if (twist.w < 0.0f) {
        twist = -twist;
    }
    if (swing.w < 0.0f) {
        swing = -swing;
    }
}

[[nodiscard]] auto rotation_of(const glm::mat3& basis) -> glm::quat
{
    // The basis may carry numerical skew; orthonormalize through the
    // quaternion.
    return glm::normalize(glm::quat_cast(basis));
}

} // anonymous namespace

auto get_swing_axes(const int twist_axis) -> std::array<int, 2>
{
    return std::array<int, 2>{
        (twist_axis == 0) ? 1 : 0,
        (twist_axis == 2) ? 1 : 2
    };
}

auto measure_joint_coordinates(
    const Transform&         world_from_frame_a,
    const Transform&         world_from_frame_b,
    const Joint_limit_shape& shape
) -> Joint_coordinates
{
    Joint_coordinates coordinates{};
    coordinates.translation = glm::transpose(world_from_frame_a.basis) * (world_from_frame_b.origin - world_from_frame_a.origin);

    const glm::quat q_a = rotation_of(world_from_frame_a.basis);
    const glm::quat q_b = rotation_of(world_from_frame_b.basis);
    const glm::quat q   = glm::normalize(glm::inverse(q_a) * q_b);

    glm::quat swing{1.0f, 0.0f, 0.0f, 0.0f};
    glm::quat twist{1.0f, 0.0f, 0.0f, 0.0f};
    swing_twist_decompose(q, shape.twist_axis, swing, twist);

    const float twist_component = (shape.twist_axis == 0) ? twist.x : ((shape.twist_axis == 1) ? twist.y : twist.z);
    coordinates.twist = 2.0f * std::atan2(twist_component, twist.w);

    const std::array<int, 2> swing_axes = get_swing_axes(shape.twist_axis);
    for (std::size_t k = 0; k < 2; ++k) {
        const int   axis      = swing_axes[k];
        const float component = (axis == 0) ? swing.x : ((axis == 1) ? swing.y : swing.z);
        coordinates.swing[k] = 2.0f * std::atan2(component, swing.w);
    }
    coordinates.cone = 2.0f * std::acos(std::clamp(swing.w, -1.0f, 1.0f));
    return coordinates;
}

auto is_within(const Constraint_axis_limit& limit, const float value, const float tolerance) -> bool
{
    if (!limit.limited) {
        return true;
    }
    return (value >= (limit.min - tolerance)) && (value <= (limit.max + tolerance));
}

auto Joint_range_check::all_ok() const -> bool
{
    return translation_ok[0] && translation_ok[1] && translation_ok[2] && twist_ok && swing_ok;
}

auto check_joint_range(
    const Joint_limit_shape& shape,
    const Joint_coordinates& coordinates,
    const float              linear_tolerance,
    const float              angular_tolerance
) -> Joint_range_check
{
    Joint_range_check check{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        check.translation_ok[axis] = is_within(shape.translation[axis], coordinates.translation[static_cast<int>(axis)], linear_tolerance);
    }
    check.twist_ok = is_within(shape.twist, coordinates.twist, angular_tolerance);
    if (shape.swing_model == Swing_limit_model::pyramid) {
        check.swing_ok =
            is_within(shape.swing[0], coordinates.swing[0], angular_tolerance) &&
            is_within(shape.swing[1], coordinates.swing[1], angular_tolerance);
    } else {
        check.swing_ok = !shape.cone.limited || (coordinates.cone <= (shape.cone.max + angular_tolerance));
    }
    return check;
}

auto pyramid_swing_rotation(const int twist_axis, const float angle_0, const float angle_1) -> glm::quat
{
    // A half-angle pair of (pi / 2, pi / 2) has no swing quaternion; stay
    // just inside it.
    constexpr float c_max_angle = 0.999f * glm::pi<float>();
    const std::array<int, 2> swing_axes = get_swing_axes(twist_axis);
    const float a0 = std::clamp(angle_0, -c_max_angle, c_max_angle);
    const float a1 = std::clamp(angle_1, -c_max_angle, c_max_angle);
    const float s0 = std::sin(0.5f * a0);
    const float c0 = std::cos(0.5f * a0);
    const float s1 = std::sin(0.5f * a1);
    const float c1 = std::cos(0.5f * a1);
    glm::vec3 vector{0.0f};
    vector[swing_axes[0]] = s0 * c1;
    vector[swing_axes[1]] = c0 * s1;
    return glm::normalize(glm::quat{c0 * c1, vector.x, vector.y, vector.z});
}

auto pyramid_swing_direction(const int twist_axis, const float angle_0, const float angle_1) -> glm::vec3
{
    glm::vec3 axis{0.0f};
    axis[twist_axis] = 1.0f;
    return pyramid_swing_rotation(twist_axis, angle_0, angle_1) * axis;
}

} // namespace erhe::physics
