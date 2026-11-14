#include "level/level_repeat.hpp"

#include "erhe_math/curve_path.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <span>

namespace editor {

namespace {

constexpr glm::vec3 c_axis_x{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 c_axis_y{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 c_axis_z{0.0f, 0.0f, 1.0f};

[[nodiscard]] auto is_finite(const glm::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] auto count_refusal(const char* name, const int count) -> std::string
{
    if (count < 1) {
        return fmt::format("{} is {}, it must be at least 1", name, count);
    }
    if (static_cast<std::size_t>(count) > c_max_repeat_count) {
        return fmt::format("{} is {}, more than the {} pieces one repeat places", name, count, c_max_repeat_count);
    }
    return {};
}

void expand_line(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses)
{
    const glm::vec3 step = start.rotation * pattern.spacing;
    for (int i = 0; i < pattern.count; ++i) {
        out_poses.push_back(Repeat_pose{.position = start.position + (static_cast<float>(i) * step), .rotation = start.rotation});
    }
}

void expand_grid(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses)
{
    const glm::vec3 step_x = start.rotation * (pattern.spacing_x * c_axis_x);
    const glm::vec3 step_z = start.rotation * (pattern.spacing_z * c_axis_z);
    for (int iz = 0; iz < pattern.count_z; ++iz) {
        for (int ix = 0; ix < pattern.count_x; ++ix) {
            out_poses.push_back(
                Repeat_pose{
                    .position = start.position + (static_cast<float>(ix) * step_x) + (static_cast<float>(iz) * step_z),
                    .rotation = start.rotation
                }
            );
        }
    }
}

void expand_ring(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses)
{
    const glm::vec3 up     = start.rotation * c_axis_y;
    const glm::vec3 center = start.position - (pattern.radius * (start.rotation * c_axis_z));
    const bool      full   = pattern.arc_degrees >= 360.0f;
    const double    arc    = glm::radians(static_cast<double>(pattern.arc_degrees));
    const double    step   = full
        ? (arc / static_cast<double>(pattern.count))
        : ((pattern.count > 1) ? (arc / static_cast<double>(pattern.count - 1)) : 0.0);
    const glm::vec3 offset = start.position - center;
    for (int i = 0; i < pattern.count; ++i) {
        const glm::quat turn = glm::angleAxis(static_cast<float>(step * static_cast<double>(i)), up);
        out_poses.push_back(
            Repeat_pose{
                .position = center + (turn * offset),
                .rotation = (pattern.facing == Repeat_ring_facing::outward) ? glm::normalize(turn * start.rotation) : start.rotation
            }
        );
    }
}

// The rotation whose x, y and z axes are the given orthonormal axes.
[[nodiscard]] auto rotation_from_axes(const glm::vec3& x_axis, const glm::vec3& y_axis) -> glm::quat
{
    return glm::normalize(glm::quat_cast(glm::mat3{x_axis, y_axis, glm::cross(x_axis, y_axis)}));
}

// A distance within the fit tolerance of a control point is that point's distance, so the piece takes the leaving span.
[[nodiscard]] auto snap_to_point_distance(const float distance, const std::span<const float> point_distances, const float epsilon) -> float
{
    for (const float point_distance : point_distances) {
        if (std::abs(distance - point_distance) <= epsilon) {
            return point_distance;
        }
    }
    return distance;
}

void expand_path(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses)
{
    const erhe::math::Curve_path_table& table = *pattern.path;
    const std::size_t count   = static_cast<std::size_t>(pattern.count);
    const float       epsilon = 1.0e-4f * std::max(pattern.path_step, 1.0e-3f);
    const std::span<const float> point_distances = table.get_point_distances();
    std::vector<float> distances(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float distance = static_cast<float>(static_cast<double>(i) * static_cast<double>(pattern.path_step));
        distances[i] = snap_to_point_distance(std::min(distance, table.get_length()), point_distances, epsilon);
    }
    std::vector<erhe::math::Curve_frame> frames(count);
    table.get_up_frames_at_distances(distances, c_axis_y, frames);
    for (const erhe::math::Curve_frame& frame : frames) {
        // side is cross(up, tangent), so cross(side, up) is the tangent's heading, kept across vertical samples.
        const glm::quat local_rotation = (pattern.path_orientation == Repeat_path_orientation::tangent)
            ? rotation_from_axes(frame.tangent, frame.up)
            : rotation_from_axes(glm::normalize(glm::cross(frame.side, c_axis_y)), c_axis_y);
        out_poses.push_back(
            Repeat_pose{
                .position = start.position + (start.rotation * frame.position),
                .rotation = glm::normalize(start.rotation * local_rotation)
            }
        );
    }
}

[[nodiscard]] auto check_pattern(const Repeat_pattern& pattern, const Repeat_pose& start) -> std::string
{
    if (!is_finite(start.position) || !std::isfinite(start.rotation.w) || !std::isfinite(start.rotation.x) ||
        !std::isfinite(start.rotation.y) || !std::isfinite(start.rotation.z)) {
        return "the start transform is not finite";
    }
    switch (pattern.kind) {
        case Repeat_pattern_kind::line: {
            const std::string refusal = count_refusal("count", pattern.count);
            if (!refusal.empty()) {
                return refusal;
            }
            if (!is_finite(pattern.spacing)) {
                return "spacing must be finite";
            }
            if ((pattern.count > 1) && !(glm::length(pattern.spacing) > 0.0f)) {
                return "spacing must not be zero";
            }
            return {};
        }
        case Repeat_pattern_kind::grid: {
            std::string refusal = count_refusal("count x", pattern.count_x);
            if (refusal.empty()) {
                refusal = count_refusal("count z", pattern.count_z);
            }
            if (!refusal.empty()) {
                return refusal;
            }
            const std::size_t count = static_cast<std::size_t>(pattern.count_x) * static_cast<std::size_t>(pattern.count_z);
            if (count > c_max_repeat_count) {
                return fmt::format(
                    "the grid is {} x {}: {} pieces, more than the {} pieces one repeat places",
                    pattern.count_x, pattern.count_z, count, c_max_repeat_count
                );
            }
            if (!std::isfinite(pattern.spacing_x) || !std::isfinite(pattern.spacing_z)) {
                return "spacing must be finite";
            }
            if (((pattern.count_x > 1) && (pattern.spacing_x == 0.0f)) || ((pattern.count_z > 1) && (pattern.spacing_z == 0.0f))) {
                return "spacing must not be zero along an axis with more than one piece";
            }
            return {};
        }
        case Repeat_pattern_kind::ring: {
            const std::string refusal = count_refusal("count", pattern.count);
            if (!refusal.empty()) {
                return refusal;
            }
            if (!std::isfinite(pattern.radius) || !(pattern.radius > 0.0f)) {
                return fmt::format("radius is {}, it must be positive", pattern.radius);
            }
            if (!std::isfinite(pattern.arc_degrees) || !(pattern.arc_degrees > 0.0f) || (pattern.arc_degrees > 360.0f)) {
                return fmt::format("arc is {} degrees, it must be more than 0 and at most 360", pattern.arc_degrees);
            }
            return {};
        }
        case Repeat_pattern_kind::path: {
            const std::string refusal = count_refusal("count", pattern.count);
            if (!refusal.empty()) {
                return refusal;
            }
            if (pattern.path == nullptr) {
                return "a path needs a curve";
            }
            if (!std::isfinite(pattern.path_step) || (pattern.path_step < 0.0f)) {
                return fmt::format("the path step is {}, it must be zero or more", pattern.path_step);
            }
            return {};
        }
        default: {
            return "unknown pattern";
        }
    }
}

} // anonymous namespace

auto c_str(const Repeat_pattern_kind kind) -> const char*
{
    switch (kind) {
        case Repeat_pattern_kind::line:     return "line";
        case Repeat_pattern_kind::grid:     return "grid";
        case Repeat_pattern_kind::ring:     return "ring";
        case Repeat_pattern_kind::path:     return "path";
        default:                            return "?";
    }
}

auto c_str(const Repeat_path_orientation orientation) -> const char*
{
    switch (orientation) {
        case Repeat_path_orientation::upright: return "upright";
        case Repeat_path_orientation::tangent: return "tangent";
        default:                               return "?";
    }
}

auto expand_repeat(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses) -> std::string
{
    out_poses.clear();
    const std::string refusal = check_pattern(pattern, start);
    if (!refusal.empty()) {
        return refusal;
    }
    switch (pattern.kind) {
        case Repeat_pattern_kind::line: expand_line(pattern, start, out_poses); return {};
        case Repeat_pattern_kind::grid: expand_grid(pattern, start, out_poses); return {};
        case Repeat_pattern_kind::ring: expand_ring(pattern, start, out_poses); return {};
        case Repeat_pattern_kind::path: expand_path(pattern, start, out_poses); return {};
        default: return "unknown pattern";
    }
}

} // namespace editor
