#include "level/level_repeat.hpp"

#include "level/kit_mesh_builder.hpp"

#include "erhe_math/curve_path.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
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

void expand_segments(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses)
{
    const erhe::math::Curve_path_table& table = *pattern.path;
    for (std::size_t i = 0, end = table.get_span_count(); i < end; ++i) {
        const erhe::math::Curve_span& span = table.get_span(i);
        Repeat_pose local;
        // check_pattern() made every pose already, so this cannot refuse.
        static_cast<void>(make_segment_pose(i, span.control[0], span.control[3], pattern.segment_items[i % pattern.segment_items.size()], pattern.path_orientation, local));
        out_poses.push_back(
            Repeat_pose{
                .position = start.position + (start.rotation * local.position),
                .rotation = glm::normalize(start.rotation * local.rotation),
                .scale    = local.scale,
                .item     = i % pattern.segment_items.size()
            }
        );
    }
}

// Grid cells per side at most: a scatter's spacing checks look at the 3 x 3 cells around a candidate.
constexpr int c_scatter_cells_per_side = 64;
// A cell a little wider than the reach: a cell index rounded at an edge never hides a pair closer than it.
constexpr float c_scatter_cell_margin = 1.0f + (1.0f / 1024.0f);

[[nodiscard]] auto get_total_scatter_weight(const std::span<const Repeat_scatter_item> items) -> uint64_t
{
    uint64_t total = 0;
    for (const Repeat_scatter_item& item : items) {
        total += item.weight;
    }
    return total;
}

// Dart throwing over one candidate stream of the seed (doc/editor/level_tools.md "Scatter layouts"). Each product and
// sum is its own statement, which the fp-contract policy of doc/cmake_conventions.md keeps unfused on every compiler.
void expand_scatter(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& out_poses)
{
    const std::span<const Repeat_scatter_item> items = pattern.scatter_items;
    const uint64_t total_weight = get_total_scatter_weight(items);
    float max_radius = 0.0f;
    for (const Repeat_scatter_item& item : items) {
        if (item.weight > 0) {
            max_radius = std::max(max_radius, item.radius);
        }
    }
    const bool  disc            = (pattern.scatter_region == Repeat_scatter_region::disc);
    const float size_x          = disc ? (2.0f * pattern.scatter_radius) : pattern.scatter_size_x;
    const float size_z          = disc ? (2.0f * pattern.scatter_radius) : pattern.scatter_size_z;
    const float half_x          = 0.5f * size_x;
    const float half_z          = 0.5f * size_z;
    const float radius_squared  = pattern.scatter_radius * pattern.scatter_radius;
    const float spacing         = pattern.scatter_spacing;
    // The largest center distance a pair can need: two of the widest footprints and the spacing.
    const float max_diameter    = 2.0f * max_radius;
    const float reach           = max_diameter + spacing;
    const float cell            = std::max(reach * c_scatter_cell_margin, std::max(size_x, size_z) / static_cast<float>(c_scatter_cells_per_side));
    const int   cells_x         = static_cast<int>(size_x / cell) + 1;
    const int   cells_z         = static_cast<int>(size_z / cell) + 1;
    const float yaw_range       = pattern.yaw_max_degrees - pattern.yaw_min_degrees;
    const glm::vec3 up          = start.rotation * c_axis_y;

    // Leaf scratch: the first point of each cell (-1: none), the next point of the same cell, the accepted points
    // (x, z and the footprint radius).
    static thread_local std::vector<int>       cell_heads;
    static thread_local std::vector<int>       next_in_cell;
    static thread_local std::vector<glm::vec3> points;
    cell_heads.assign(static_cast<std::size_t>(cells_x) * static_cast<std::size_t>(cells_z), -1);
    next_in_cell.clear();
    points.clear();

    const std::size_t wanted   = static_cast<std::size_t>(pattern.count);
    const std::size_t attempts = wanted * c_scatter_attempts_per_piece;
    Kit_random random{pattern.scatter_seed, "layout scatter"};
    for (std::size_t attempt = 0; (attempt < attempts) && (points.size() < wanted); ++attempt) {
        // Every candidate draws four values (x, z, yaw, item), taken or not, so the stream depends on no value but the seed.
        const float       u    = random.next_unit() - 0.5f;
        const float       v    = random.next_unit() - 0.5f;
        const float       w    = random.next_unit();
        const uint32_t    draw = random.next_u32();
        const std::size_t item = pick_scatter_item(items, total_weight, draw);
        const float       x    = u * size_x;
        const float       z    = v * size_z;
        if (disc) {
            const float xx = x * x;
            const float zz = z * z;
            const float rr = xx + zz;
            if (rr > radius_squared) {
                continue;
            }
        }
        const float radius = items[item].radius;
        const int cell_x = std::clamp(static_cast<int>((x + half_x) / cell), 0, cells_x - 1);
        const int cell_z = std::clamp(static_cast<int>((z + half_z) / cell), 0, cells_z - 1);
        bool too_close = false;
        if (reach > 0.0f) {
            for (int nz = std::max(cell_z - 1, 0), end_z = std::min(cell_z + 1, cells_z - 1); (nz <= end_z) && !too_close; ++nz) {
                for (int nx = std::max(cell_x - 1, 0), end_x = std::min(cell_x + 1, cells_x - 1); (nx <= end_x) && !too_close; ++nx) {
                    for (int j = cell_heads[static_cast<std::size_t>((nz * cells_x) + nx)]; j >= 0; j = next_in_cell[static_cast<std::size_t>(j)]) {
                        const glm::vec3& other = points[static_cast<std::size_t>(j)];
                        const float dx       = x - other.x;
                        const float dz       = z - other.y;
                        const float dxx      = dx * dx;
                        const float dzz      = dz * dz;
                        const float dd       = dxx + dzz;
                        const float radii    = radius + other.z;
                        const float limit    = radii + spacing;
                        const float limit_sq = limit * limit;
                        if (dd < limit_sq) {
                            too_close = true;
                            break;
                        }
                    }
                }
            }
        }
        if (too_close) {
            continue;
        }
        const std::size_t cell_index = static_cast<std::size_t>((cell_z * cells_x) + cell_x);
        next_in_cell.push_back(cell_heads[cell_index]);
        cell_heads[cell_index] = static_cast<int>(points.size());
        points.push_back(glm::vec3{x, z, radius});

        const float yaw_offset = yaw_range * w;
        const float yaw        = pattern.yaw_min_degrees + yaw_offset;
        out_poses.push_back(
            Repeat_pose{
                .position = start.position + (start.rotation * glm::vec3{x, 0.0f, z}),
                .rotation = glm::normalize(glm::angleAxis(glm::radians(yaw), up) * start.rotation),
                .item     = item
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
        case Repeat_pattern_kind::segments: {
            if (pattern.path == nullptr) {
                return "a segments run needs a curve";
            }
            const std::size_t count = pattern.path->get_span_count();
            if (count < 1) {
                return "the curve has fewer than 2 points: a segments run needs at least one segment";
            }
            if (count > c_max_repeat_count) {
                return fmt::format("the curve has {} segments, more than the {} pieces one repeat places", count, c_max_repeat_count);
            }
            if (pattern.segment_items.empty()) {
                return "a segments run needs at least one item";
            }
            for (std::size_t i = 0; i < count; ++i) {
                const erhe::math::Curve_span& span = pattern.path->get_span(i);
                Repeat_pose pose;
                const std::string refusal = make_segment_pose(
                    i, span.control[0], span.control[3], pattern.segment_items[i % pattern.segment_items.size()], pattern.path_orientation, pose
                );
                if (!refusal.empty()) {
                    return refusal;
                }
            }
            return {};
        }
        case Repeat_pattern_kind::scatter: {
            const std::string refusal = count_refusal("count", pattern.count);
            if (!refusal.empty()) {
                return refusal;
            }
            if (pattern.scatter_region == Repeat_scatter_region::disc) {
                if (!std::isfinite(pattern.scatter_radius) || !(pattern.scatter_radius > 0.0f)) {
                    return fmt::format("the scatter radius is {} m, it must be positive", pattern.scatter_radius);
                }
            } else if (
                !std::isfinite(pattern.scatter_size_x) || !(pattern.scatter_size_x > 0.0f) ||
                !std::isfinite(pattern.scatter_size_z) || !(pattern.scatter_size_z > 0.0f)
            ) {
                return fmt::format("the scatter region is {} x {} m, each side must be positive", pattern.scatter_size_x, pattern.scatter_size_z);
            }
            if (!std::isfinite(pattern.scatter_spacing) || (pattern.scatter_spacing < 0.0f)) {
                return fmt::format("the minimum spacing is {} m, it must be zero or more", pattern.scatter_spacing);
            }
            if (pattern.scatter_items.empty()) {
                return "a scatter needs at least one item";
            }
            for (std::size_t i = 0, end = pattern.scatter_items.size(); i < end; ++i) {
                const Repeat_scatter_item& item = pattern.scatter_items[i];
                if (item.weight > c_max_scatter_weight) {
                    return fmt::format("item {} has weight {}, more than {}", i, item.weight, c_max_scatter_weight);
                }
                if (!std::isfinite(item.radius) || (item.radius < 0.0f)) {
                    return fmt::format("item {} has a footprint radius of {} m, it must be zero or more", i, item.radius);
                }
            }
            const uint64_t total_weight = get_total_scatter_weight(pattern.scatter_items);
            if (total_weight == 0) {
                return "every item's weight is 0: a scatter needs an item of positive weight";
            }
            if (total_weight > uint64_t{0xffffffffu}) {
                return fmt::format("the item weights sum to {}, more than the {} a pick draws over", total_weight, 0xffffffffu);
            }
            if (
                !std::isfinite(pattern.yaw_min_degrees) || !std::isfinite(pattern.yaw_max_degrees) ||
                (pattern.yaw_min_degrees < -360.0f) || (pattern.yaw_max_degrees > 360.0f) || (pattern.yaw_min_degrees > pattern.yaw_max_degrees)
            ) {
                return fmt::format(
                    "the yaw range is {} - {} degrees, it must lie in -360 - 360 with its minimum not above its maximum",
                    pattern.yaw_min_degrees, pattern.yaw_max_degrees
                );
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
        case Repeat_pattern_kind::scatter:  return "scatter";
        case Repeat_pattern_kind::segments: return "segments";
        case Repeat_pattern_kind::stairs:   return "stairs";
        case Repeat_pattern_kind::floor:    return "floor";
        default:                            return "?";
    }
}

auto is_curve_layout_kind(const Repeat_pattern_kind kind) -> bool
{
    return (kind == Repeat_pattern_kind::path) || (kind == Repeat_pattern_kind::segments) || (kind == Repeat_pattern_kind::stairs) ||
           (kind == Repeat_pattern_kind::floor);
}

auto c_str(const Repeat_path_orientation orientation) -> const char*
{
    switch (orientation) {
        case Repeat_path_orientation::upright: return "upright";
        case Repeat_path_orientation::tangent: return "tangent";
        default:                               return "?";
    }
}

auto c_str(const Repeat_scatter_region region) -> const char*
{
    switch (region) {
        case Repeat_scatter_region::rectangle: return "rectangle";
        case Repeat_scatter_region::disc:      return "disc";
        default:                               return "?";
    }
}

auto pick_scatter_item(const std::span<const Repeat_scatter_item> items, const uint64_t total_weight, const uint32_t draw) -> std::size_t
{
    // The draw times the total, shifted down: an integer in [0, total) with no rounding on any platform.
    uint64_t target = (static_cast<uint64_t>(draw) * total_weight) >> 32;
    for (std::size_t i = 0, end = items.size(); i < end; ++i) {
        if (target < items[i].weight) {
            return i;
        }
        target -= items[i].weight;
    }
    return 0;
}

auto get_segment_span_length(const glm::vec3& p0, const glm::vec3& p1, const Repeat_path_orientation orientation) -> float
{
    const glm::vec3 chord = p1 - p0;
    return glm::length((orientation == Repeat_path_orientation::upright) ? glm::vec3{chord.x, 0.0f, chord.z} : chord);
}

auto make_segment_pose(
    const std::size_t                segment,
    const glm::vec3&                 p0,
    const glm::vec3&                 p1,
    const Repeat_segment_item&       item,
    const Repeat_path_orientation    orientation,
    Repeat_pose&                     out_pose
) -> std::string
{
    if (!is_finite(p0) || !is_finite(p1)) {
        return fmt::format("segment {}: a point is not finite", segment);
    }
    const glm::vec3 chord = p1 - p0;
    // Upright spans the chord's plan length at point i's height; tangent spans the chord itself.
    const glm::vec3 spanned = (orientation == Repeat_path_orientation::upright) ? glm::vec3{chord.x, 0.0f, chord.z} : chord;
    const float     length  = glm::length(spanned);
    if (!(length >= c_min_segment_length)) {
        if (glm::length(chord) >= c_min_segment_length) {
            return fmt::format("segment {} (points {} - {}) is vertical: an upright piece has no heading along it", segment, segment, segment + 1);
        }
        return fmt::format("segment {} (points {} - {}) is {} m long, shorter than the {} m a piece spans", segment, segment, segment + 1, glm::length(chord), c_min_segment_length);
    }
    const glm::vec3 x_axis = spanned / length;
    glm::vec3       y_axis = c_axis_y;
    if (orientation == Repeat_path_orientation::tangent) {
        const float along = glm::dot(c_axis_y, x_axis);
        const glm::vec3 up_rest = c_axis_y - (along * x_axis);
        const float     rest    = glm::length(up_rest);
        if (!(rest >= 1.0e-4f)) {
            return fmt::format("segment {} (points {} - {}) is vertical: a tangent piece has no up axis across it", segment, segment, segment + 1);
        }
        y_axis = up_rest / rest;
    }
    // L24: never scaled; the piece's x extent is centred on the segment (a piece whose child.length was set to the
    // segment's length spans it end to end).
    const float center = (std::isfinite(item.min_x) && std::isfinite(item.max_x)) ? (0.5f * (item.min_x + item.max_x)) : 0.0f;
    out_pose.position = p0 + (((0.5f * length) - center) * x_axis);
    out_pose.rotation = rotation_from_axes(x_axis, y_axis);
    out_pose.scale    = glm::vec3{1.0f};
    out_pose.item     = 0;
    return {};
}

namespace {

[[nodiscard]] auto plan_of(const glm::vec3& v) -> glm::vec2
{
    return glm::vec2{v.x, v.z};
}

// The end point moved onto the moved segment's line: along the neighbour's plan line through it, or along the normal.
[[nodiscard]] auto slide_end_point(
    const glm::vec3&                path_point,
    const std::optional<glm::vec3>& neighbour_point,
    const glm::vec2&                normal,
    const float                     offset
) -> glm::vec3
{
    glm::vec2 moved = plan_of(path_point) + (offset * normal);
    if (neighbour_point.has_value()) {
        const glm::vec2 direction = plan_of(path_point) - plan_of(neighbour_point.value());
        const float     length    = glm::length(direction);
        if (length >= c_min_segment_length) {
            const glm::vec2 unit   = direction / length;
            const float     across = glm::dot(unit, normal);
            if (std::abs(across) >= c_segment_slide_min_sine) {
                moved = plan_of(path_point) + ((offset / across) * unit);
            }
        }
    }
    return glm::vec3{moved.x, path_point.y, moved.y};
}

} // anonymous namespace

auto get_segment_normal(const erhe::math::Curve_path& path, const std::size_t segment, glm::vec3& out_normal) -> std::string
{
    const std::size_t count = erhe::math::get_span_count(path);
    if (segment >= count) {
        return fmt::format("segment {} is outside the curve's {} segments", segment, count);
    }
    const glm::vec3& p0 = path.points[segment].position;
    const glm::vec3& p1 = path.points[(segment + 1) % path.points.size()].position;
    if (!is_finite(p0) || !is_finite(p1)) {
        return fmt::format("segment {}: a point is not finite", segment);
    }
    const glm::vec2 chord  = plan_of(p1) - plan_of(p0);
    const float     length = glm::length(chord);
    if (!(length >= c_min_segment_length)) {
        return fmt::format("segment {} (points {} - {}) has a plan length of {} m: it has no normal", segment, segment, (segment + 1) % path.points.size(), length);
    }
    const glm::vec2 unit = chord / length;
    // The right of the direction with y up: cross(direction, y).
    out_normal = glm::vec3{-unit.y, 0.0f, unit.x};
    return {};
}

auto move_curve_segment(const erhe::math::Curve_path& path, const std::size_t segment, const float offset, Segment_move& out_move) -> std::string
{
    glm::vec3 normal{0.0f};
    const std::string refusal = get_segment_normal(path, segment, normal);
    if (!refusal.empty()) {
        return refusal;
    }
    if (!std::isfinite(offset)) {
        return fmt::format("the offset {} is not finite", offset);
    }
    const std::size_t point_count = path.points.size();
    const bool        closed      = (path.closure == erhe::math::Curve_closure::closed);
    const std::size_t first       = segment;
    const std::size_t second      = (segment + 1) % point_count;
    std::optional<glm::vec3> before_first;
    std::optional<glm::vec3> after_second;
    if (closed || (first > 0)) {
        before_first = path.points[(first + point_count - 1) % point_count].position;
    }
    if (closed || ((second + 1) < point_count)) {
        after_second = path.points[(second + 1) % point_count].position;
    }
    const glm::vec2 plan_normal{normal.x, normal.z};
    out_move.first_point  = first;
    out_move.second_point = second;
    out_move.first        = slide_end_point(path.points[first].position,  before_first, plan_normal, offset);
    out_move.second       = slide_end_point(path.points[second].position, after_second, plan_normal, offset);
    return {};
}

void apply_segment_move(const Segment_move& move, erhe::math::Curve_path& path)
{
    if ((move.first_point < path.points.size()) && (move.second_point < path.points.size())) {
        path.points[move.first_point].position  = move.first;
        path.points[move.second_point].position = move.second;
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
        case Repeat_pattern_kind::scatter: expand_scatter(pattern, start, out_poses); return {};
        case Repeat_pattern_kind::segments: expand_segments(pattern, start, out_poses); return {};
        // The staircase is one generated child in the node's own frame.
        case Repeat_pattern_kind::stairs: out_poses.push_back(start); return {};
        case Repeat_pattern_kind::floor:  out_poses.push_back(start); return {};
        default: return "unknown pattern";
    }
}

} // namespace editor
