#include "level/level_repeat.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

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

// The start rotation turned about its up axis so that its x axis follows the direction's part across that axis.
[[nodiscard]] auto heading_along(const Repeat_pose& start, const glm::vec3& direction, const glm::quat& previous) -> glm::quat
{
    const glm::vec3 up     = start.rotation * c_axis_y;
    const glm::vec3 x_axis = start.rotation * c_axis_x;
    const glm::vec3 across = direction - (glm::dot(direction, up) * up);
    const float     length = glm::length(across);
    if (!(length > 1.0e-6f)) {
        return previous;
    }
    const glm::vec3 target = across / length;
    const float     angle  = std::atan2(glm::dot(glm::cross(x_axis, target), up), glm::dot(x_axis, target));
    return glm::normalize(glm::angleAxis(angle, up) * start.rotation);
}

[[nodiscard]] auto expand_polyline(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses) -> std::string
{
    double total_length = 0.0;
    glm::vec3 from = start.position;
    for (const glm::vec3& point : pattern.points) {
        total_length += static_cast<double>(glm::length(point - from));
        from = point;
    }
    const double spacing   = static_cast<double>(pattern.path_spacing);
    const double steps     = std::floor((total_length / spacing) + 1.0e-4);
    if ((steps + 1.0) > static_cast<double>(c_max_repeat_count)) {
        return fmt::format(
            "the path is {:.3f} m long: {:.0f} pieces at {} m spacing, more than the {} pieces one repeat places",
            total_length, steps + 1.0, pattern.path_spacing, c_max_repeat_count
        );
    }
    const std::size_t count   = static_cast<std::size_t>(steps) + 1;
    const double      epsilon = 1.0e-4 * spacing;

    // Segment k runs from corner k (the start position for k 0) to points[k], from arc length segment_start.
    std::size_t segment       = 0;
    double      segment_start = 0.0;
    glm::vec3   segment_from  = start.position;
    glm::quat   heading       = start.rotation;
    const auto  segment_length = [&]() -> double {
        return static_cast<double>(glm::length(pattern.points[segment] - segment_from));
    };
    heading = heading_along(start, pattern.points[0] - start.position, heading);
    for (std::size_t i = 0; i < count; ++i) {
        const double s = static_cast<double>(i) * spacing;
        // A piece at a corner takes the segment that leaves it.
        while (((segment + 1) < pattern.points.size()) && (s >= (segment_start + segment_length() - epsilon))) {
            segment_start += segment_length();
            segment_from   = pattern.points[segment];
            ++segment;
            heading = heading_along(start, pattern.points[segment] - segment_from, heading);
        }
        const double    length    = segment_length();
        const double    along     = std::clamp(s - segment_start, 0.0, length);
        const glm::vec3 direction = (length > 0.0)
            ? ((pattern.points[segment] - segment_from) / static_cast<float>(length))
            : glm::vec3{0.0f};
        out_poses.push_back(Repeat_pose{.position = segment_from + (static_cast<float>(along) * direction), .rotation = heading});
    }
    return {};
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
        case Repeat_pattern_kind::polyline: {
            if (pattern.points.empty()) {
                return "a path needs at least one point after the start";
            }
            for (const glm::vec3& point : pattern.points) {
                if (!is_finite(point)) {
                    return "the path points must be finite";
                }
            }
            if (!std::isfinite(pattern.path_spacing) || !(pattern.path_spacing > 0.0f)) {
                return fmt::format("spacing is {}, it must be positive", pattern.path_spacing);
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
        case Repeat_pattern_kind::polyline: return "polyline";
        default:                            return "?";
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
        case Repeat_pattern_kind::polyline: {
            const std::string path_refusal = expand_polyline(pattern, start, out_poses);
            if (!path_refusal.empty()) {
                out_poses.clear();
            }
            return path_refusal;
        }
        default: return "unknown pattern";
    }
}

} // namespace editor
