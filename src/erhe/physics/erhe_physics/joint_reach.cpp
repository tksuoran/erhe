#include "erhe_physics/joint_reach.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace erhe::physics {

namespace {

[[nodiscard]] auto is_fixed(const Constraint_axis_limit& limit) -> bool
{
    return limit.limited && ((limit.max - limit.min) <= Joint_reach::c_fixed_axis_epsilon);
}

// Angle wrapped to (-pi, pi].
[[nodiscard]] auto wrap_angle(const float angle) -> float
{
    const float two_pi  = glm::two_pi<float>();
    float       wrapped = std::fmod(angle + glm::pi<float>(), two_pi);
    if (wrapped < 0.0f) {
        wrapped += two_pi;
    }
    return wrapped - glm::pi<float>();
}

// Joint angle theta (from a wrapped angle) moved into [min, max]; outside the
// range, the limit nearer in angular distance.
[[nodiscard]] auto clamp_angle(const float theta, const float min, const float max) -> float
{
    const float two_pi = glm::two_pi<float>();
    if ((max - min) >= two_pi) {
        return theta;
    }
    for (const float candidate : { theta, theta + two_pi, theta - two_pi }) {
        if ((candidate >= min) && (candidate <= max)) {
            return candidate;
        }
    }
    const float distance_to_min = std::abs(wrap_angle(theta - min));
    const float distance_to_max = std::abs(wrap_angle(theta - max));
    return (distance_to_min <= distance_to_max) ? min : max;
}

[[nodiscard]] auto transform_point(const Transform& transform, const glm::vec3 point) -> glm::vec3
{
    return (transform.basis * point) + transform.origin;
}

} // anonymous namespace

auto c_str(const Joint_reach_shape shape) -> const char*
{
    switch (shape) {
        case Joint_reach_shape::unprojected: return "unprojected";
        case Joint_reach_shape::point:       return "point";
        case Joint_reach_shape::circle:      return "circle";
        case Joint_reach_shape::sphere:      return "sphere";
        case Joint_reach_shape::box:         return "box";
        default:                             return "?";
    }
}

void Joint_reach::configure(
    const Transform&                            world_from_fixed_anchor,
    const Joint_side                            moving_side,
    const std::array<Constraint_axis_limit, 6>& limits,
    const glm::vec3                             point_in_moving_anchor,
    const glm::vec3                             point_in_world_now
)
{
    m_world_from_fixed_anchor = world_from_fixed_anchor;
    m_fixed_anchor_from_world = inverse(world_from_fixed_anchor);
    m_shape                   = Joint_reach_shape::unprojected;
    m_axis                    = -1;
    m_sign                    = (moving_side == Joint_side::a) ? -1.0f : 1.0f;
    m_angle_limited           = false;
    m_last_projected          = point_in_world_now;

    int moving_linear_count  = 0;
    int moving_angular_count = 0;
    int moving_angular_axis  = -1;
    bool rotation_fixed_at_zero = true;
    glm::vec3 fixed_offset{0.0f};
    for (std::size_t i = 0; i < 3; ++i) {
        const Constraint_axis_limit& linear  = limits[i];
        const Constraint_axis_limit& angular = limits[3 + i];
        if (is_fixed(linear)) {
            fixed_offset[static_cast<int>(i)] = 0.5f * (linear.min + linear.max);
        } else {
            ++moving_linear_count;
        }
        if (is_fixed(angular)) {
            if ((std::abs(angular.min) > c_fixed_axis_epsilon) || (std::abs(angular.max) > c_fixed_axis_epsilon)) {
                rotation_fixed_at_zero = false;
            }
        } else {
            ++moving_angular_count;
            moving_angular_axis = static_cast<int>(i);
        }
    }

    const glm::vec3 p = point_in_moving_anchor;
    if (!rotation_fixed_at_zero) {
        return; // a rotation fixed at a non-zero angle: not handled
    }

    if (moving_angular_count == 0) {
        if (moving_linear_count == 0) {
            m_shape  = Joint_reach_shape::point;
            m_center = (moving_side == Joint_side::a) ? glm::vec3{0.0f} : fixed_offset;
            m_vector = (moving_side == Joint_side::a) ? (p - fixed_offset) : p;
        } else {
            m_shape = Joint_reach_shape::box;
            const float infinity = std::numeric_limits<float>::infinity();
            for (int i = 0; i < 3; ++i) {
                const Constraint_axis_limit& linear = limits[static_cast<std::size_t>(i)];
                const float t_min = linear.limited ? linear.min : -infinity;
                const float t_max = linear.limited ? linear.max :  infinity;
                if (moving_side == Joint_side::a) {
                    m_box_min[i] = p[i] - t_max;
                    m_box_max[i] = p[i] - t_min;
                } else {
                    m_box_min[i] = p[i] + t_min;
                    m_box_max[i] = p[i] + t_max;
                }
            }
        }
    } else if (moving_linear_count == 0) {
        m_center = (moving_side == Joint_side::a) ? glm::vec3{0.0f} : fixed_offset;
        m_vector = (moving_side == Joint_side::a) ? (p - fixed_offset) : p;
        if (moving_angular_count == 1) {
            const int k = moving_angular_axis;
            const int i = (k + 1) % 3;
            const int j = (k + 2) % 3;
            const float radius = std::sqrt((m_vector[i] * m_vector[i]) + (m_vector[j] * m_vector[j]));
            if (radius < c_radius_epsilon) {
                m_shape = Joint_reach_shape::point;
            } else {
                m_shape = Joint_reach_shape::circle;
                m_axis  = k;
                const Constraint_axis_limit& angular = limits[3 + static_cast<std::size_t>(k)];
                m_angle_limited = angular.limited;
                m_angle_min     = angular.min;
                m_angle_max     = angular.max;
            }
        } else {
            m_shape = (glm::length(m_vector) < c_radius_epsilon)
                ? Joint_reach_shape::point
                : Joint_reach_shape::sphere;
        }
    }
    // else: translation and rotation both movable - not handled (unprojected)

    if (m_shape != Joint_reach_shape::unprojected) {
        const glm::vec3 now_in_fixed_anchor = transform_point(m_fixed_anchor_from_world, point_in_world_now);
        // Seed: the reachable position nearest to where the point is now;
        // when even that has no unique nearest position, the zero-angle one.
        const glm::vec3 seed = project_local(now_in_fixed_anchor, m_center + m_vector);
        m_last_projected = transform_point(m_world_from_fixed_anchor, seed);
    }
}

auto Joint_reach::project_local(const glm::vec3 target, const glm::vec3 fallback) const -> glm::vec3
{
    switch (m_shape) {
        case Joint_reach_shape::point: {
            return m_center + m_vector;
        }
        case Joint_reach_shape::box: {
            return glm::clamp(target, m_box_min, m_box_max);
        }
        case Joint_reach_shape::sphere: {
            const glm::vec3 d      = target - m_center;
            const float     length = glm::length(d);
            if (length < c_radius_epsilon) {
                return fallback;
            }
            return m_center + (d / length) * glm::length(m_vector);
        }
        case Joint_reach_shape::circle: {
            const int k = m_axis;
            const int i = (k + 1) % 3;
            const int j = (k + 2) % 3;
            const glm::vec3 d = target - m_center;
            const float target_radius = std::sqrt((d[i] * d[i]) + (d[j] * d[j]));
            if (target_radius < c_radius_epsilon) {
                return fallback;
            }
            const float radius     = std::sqrt((m_vector[i] * m_vector[i]) + (m_vector[j] * m_vector[j]));
            const float zero_angle = std::atan2(m_vector[j], m_vector[i]);
            const float alpha      = wrap_angle(std::atan2(d[j], d[i]) - zero_angle);
            float       theta      = m_sign * alpha;
            if (m_angle_limited) {
                theta = clamp_angle(theta, m_angle_min, m_angle_max);
            }
            const float angle = zero_angle + (m_sign * theta);
            glm::vec3 result{0.0f};
            result[k] = m_vector[k];
            result[i] = radius * std::cos(angle);
            result[j] = radius * std::sin(angle);
            return m_center + result;
        }
        case Joint_reach_shape::unprojected:
        default: {
            return target;
        }
    }
}

auto Joint_reach::project(const glm::vec3 target_in_world) -> glm::vec3
{
    if (m_shape == Joint_reach_shape::unprojected) {
        return target_in_world;
    }
    const glm::vec3 target_in_fixed_anchor = transform_point(m_fixed_anchor_from_world, target_in_world);
    const glm::vec3 fallback_in_fixed_anchor = transform_point(m_fixed_anchor_from_world, m_last_projected);
    const glm::vec3 projected = project_local(target_in_fixed_anchor, fallback_in_fixed_anchor);
    m_last_projected = transform_point(m_world_from_fixed_anchor, projected);
    return m_last_projected;
}

auto Joint_reach::get_shape() const -> Joint_reach_shape
{
    return m_shape;
}

auto Joint_reach::get_axis() const -> int
{
    return m_axis;
}

auto Joint_reach::get_radius() const -> float
{
    if (m_shape == Joint_reach_shape::circle) {
        const int i = (m_axis + 1) % 3;
        const int j = (m_axis + 2) % 3;
        return std::sqrt((m_vector[i] * m_vector[i]) + (m_vector[j] * m_vector[j]));
    }
    if (m_shape == Joint_reach_shape::sphere) {
        return glm::length(m_vector);
    }
    return 0.0f;
}

auto Joint_reach::get_center() const -> glm::vec3
{
    glm::vec3 center_in_fixed_anchor = m_center;
    if (m_shape == Joint_reach_shape::circle) {
        center_in_fixed_anchor[m_axis] += m_vector[m_axis];
    }
    return transform_point(m_world_from_fixed_anchor, center_in_fixed_anchor);
}

auto Joint_reach::get_last_projected() const -> glm::vec3
{
    return m_last_projected;
}

} // namespace erhe::physics
