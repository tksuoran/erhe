#include "level/kit_layout_outline.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace editor {

namespace {

constexpr glm::vec3 c_axis_x{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 c_axis_y{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 c_axis_z{0.0f, 0.0f, 1.0f};

// The unit direction across the line in the node's xz plane; a line along y takes the z axis.
[[nodiscard]] auto get_line_tick_direction(const glm::vec3& along) -> glm::vec3
{
    const glm::vec3 across = glm::cross(c_axis_y, along);
    const float     length = glm::length(across);
    return (length > 1.0e-6f) ? (across / length) : c_axis_z;
}

void make_line(const Kit_layout_pattern& pattern, const Kit_layout_run& run, const std::span<const Repeat_pose> poses, Kit_layout_outline& out)
{
    if (poses.empty()) {
        return;
    }
    // A run of one element has no step; the pattern's direction still orients its tick.
    const float spacing_length = glm::length(run.pattern.spacing);
    const float given_length   = glm::length(pattern.direction);
    glm::vec3   along          = c_axis_x;
    if (spacing_length > 0.0f) {
        along = run.pattern.spacing / spacing_length;
    } else if (given_length > 0.0f) {
        along = pattern.direction / given_length;
    }
    const glm::vec3 across = get_line_tick_direction(along);
    out.polyline.push_back(poses.front().position);
    out.polyline.push_back(poses.back().position);
    for (const Repeat_pose& pose : poses) {
        out.ticks.push_back(Kit_layout_tick{.position = pose.position, .direction = across});
    }
}

void make_grid(const Kit_layout_run& run, Kit_layout_outline& out)
{
    // The cells of the elements: each center half a spacing inside the outline.
    const Repeat_pattern& grid = run.pattern;
    const float x0 = -0.5f * grid.spacing_x;
    const float z0 = -0.5f * grid.spacing_z;
    const float x1 = (static_cast<float>(grid.count_x) - 0.5f) * grid.spacing_x;
    const float z1 = (static_cast<float>(grid.count_z) - 0.5f) * grid.spacing_z;
    out.polyline.push_back(glm::vec3{x0, 0.0f, z0});
    out.polyline.push_back(glm::vec3{x1, 0.0f, z0});
    out.polyline.push_back(glm::vec3{x1, 0.0f, z1});
    out.polyline.push_back(glm::vec3{x0, 0.0f, z1});
    out.closed = true;
    for (int ix = 0; ix < grid.count_x; ++ix) {
        out.ticks.push_back(Kit_layout_tick{.position = glm::vec3{static_cast<float>(ix) * grid.spacing_x, 0.0f, z0}, .direction = c_axis_z});
    }
    for (int iz = 0; iz < grid.count_z; ++iz) {
        out.ticks.push_back(Kit_layout_tick{.position = glm::vec3{x0, 0.0f, static_cast<float>(iz) * grid.spacing_z}, .direction = c_axis_x});
    }
}

void make_ring(const Kit_layout_run& run, const std::span<const Repeat_pose> poses, Kit_layout_outline& out)
{
    // Element 0 at the origin, the center radius behind it along -z (expand_repeat()).
    const Repeat_pattern& ring   = run.pattern;
    const glm::vec3       center{0.0f, 0.0f, -ring.radius};
    out.arcs.push_back(Kit_layout_arc{.center = center, .radius = ring.radius, .degrees = std::min(ring.arc_degrees, 360.0f)});
    for (const Repeat_pose& pose : poses) {
        const glm::vec3 radial = pose.position - center;
        const float     length = glm::length(radial);
        out.ticks.push_back(Kit_layout_tick{.position = pose.position, .direction = (length > 0.0f) ? (radial / length) : c_axis_z});
    }
}

void make_region_handles(const Repeat_pattern& scatter, Kit_layout_outline& out)
{
    using Value = Kit_region_handle_value;
    if (scatter.scatter_region == Repeat_scatter_region::disc) {
        const float r = scatter.scatter_radius;
        // At 0, 90, 180 and 270 degrees of get_arc_point(): +z, +x, -z, -x.
        for (const glm::vec3& side : std::array<glm::vec3, 4>{c_axis_z, c_axis_x, -c_axis_z, -c_axis_x}) {
            out.region_handles.push_back(Kit_region_handle{.position = r * side, .side = side, .value = Value::region_radius});
        }
        return;
    }
    const float x = 0.5f * scatter.scatter_size_x;
    const float z = 0.5f * scatter.scatter_size_z;
    // The corners in the polyline's order, then the edge between each corner and the next.
    const std::array<glm::vec3, 4> corner_sides{glm::vec3{-1.0f, 0.0f, -1.0f}, glm::vec3{1.0f, 0.0f, -1.0f}, glm::vec3{1.0f, 0.0f, 1.0f}, glm::vec3{-1.0f, 0.0f, 1.0f}};
    for (const glm::vec3& side : corner_sides) {
        out.region_handles.push_back(Kit_region_handle{.position = glm::vec3{side.x * x, 0.0f, side.z * z}, .side = side, .value = Value::size_x_z});
    }
    const std::array<glm::vec3, 4> edge_sides{-c_axis_z, c_axis_x, c_axis_z, -c_axis_x};
    for (const glm::vec3& side : edge_sides) {
        const Value value = (side.x != 0.0f) ? Value::size_x : Value::size_z;
        out.region_handles.push_back(Kit_region_handle{.position = glm::vec3{side.x * x, 0.0f, side.z * z}, .side = side, .value = value});
    }
}

void make_scatter(const Kit_layout_run& run, const std::span<const Repeat_pose> poses, const std::span<const Repeat_scatter_item> items, Kit_layout_outline& out)
{
    const Repeat_pattern& scatter = run.pattern;
    if (scatter.scatter_region == Repeat_scatter_region::disc) {
        out.arcs.push_back(Kit_layout_arc{.center = glm::vec3{0.0f}, .radius = scatter.scatter_radius, .degrees = 360.0f});
    } else {
        const float x = 0.5f * scatter.scatter_size_x;
        const float z = 0.5f * scatter.scatter_size_z;
        out.polyline.push_back(glm::vec3{-x, 0.0f, -z});
        out.polyline.push_back(glm::vec3{ x, 0.0f, -z});
        out.polyline.push_back(glm::vec3{ x, 0.0f,  z});
        out.polyline.push_back(glm::vec3{-x, 0.0f,  z});
        out.closed = true;
    }
    make_region_handles(scatter, out);
    // Two pieces at exactly the minimum gap show touching outer circles.
    const float half_spacing = 0.5f * scatter.scatter_spacing;
    for (const Repeat_pose& pose : poses) {
        const float footprint = (pose.item < items.size()) ? items[pose.item].radius : 0.0f;
        out.circles.push_back(Kit_layout_spacing_circle{.center = pose.position, .footprint_radius = footprint, .spacing_radius = footprint + half_spacing});
    }
}

} // anonymous namespace

void Kit_layout_outline::clear()
{
    kind   = Repeat_pattern_kind::line;
    closed = false;
    polyline.clear();
    arcs.clear();
    ticks.clear();
    circles.clear();
    region_handles.clear();
}

auto c_str(const Kit_region_handle_value value) -> const char*
{
    switch (value) {
        case Kit_region_handle_value::size_x:        return "size_x";
        case Kit_region_handle_value::size_z:        return "size_z";
        case Kit_region_handle_value::size_x_z:      return "size_x_z";
        case Kit_region_handle_value::region_radius: return "region_radius";
        default:                                     return "?";
    }
}

void apply_region_handle(const Kit_region_handle& handle, const glm::vec3& point, Kit_layout_pattern& in_out_pattern)
{
    // The region is centred on the node's origin, so an edge drag resizes it about the origin: twice the reach.
    if (!std::isfinite(point.x) || !std::isfinite(point.z)) {
        return;
    }
    const auto clamped = [](const float size) -> float {
        return std::clamp(size, c_min_scatter_region_size, c_max_scatter_region_size);
    };
    switch (handle.value) {
        case Kit_region_handle_value::size_x: {
            in_out_pattern.size_x = clamped(2.0f * (handle.side.x * point.x));
            break;
        }
        case Kit_region_handle_value::size_z: {
            in_out_pattern.size_z = clamped(2.0f * (handle.side.z * point.z));
            break;
        }
        case Kit_region_handle_value::size_x_z: {
            in_out_pattern.size_x = clamped(2.0f * (handle.side.x * point.x));
            in_out_pattern.size_z = clamped(2.0f * (handle.side.z * point.z));
            break;
        }
        case Kit_region_handle_value::region_radius: {
            in_out_pattern.region_radius = clamped(std::sqrt((point.x * point.x) + (point.z * point.z)));
            break;
        }
        default: {
            break;
        }
    }
}

void make_layout_outline(
    const Kit_layout_pattern&                  pattern,
    const Kit_layout_run&                      run,
    const std::span<const Repeat_pose>         poses,
    const std::span<const Repeat_scatter_item> items,
    Kit_layout_outline&                        out_outline
)
{
    out_outline.clear();
    out_outline.kind = pattern.kind;
    switch (pattern.kind) {
        case Repeat_pattern_kind::line:    make_line   (pattern, run, poses, out_outline); break;
        case Repeat_pattern_kind::grid:    make_grid   (run, out_outline); break;
        case Repeat_pattern_kind::ring:    make_ring   (run, poses, out_outline); break;
        case Repeat_pattern_kind::scatter: make_scatter(run, poses, items, out_outline); break;
        case Repeat_pattern_kind::path:
        default:                           break;
    }
}

auto get_arc_point(const Kit_layout_arc& arc, const float degrees) -> glm::vec3
{
    const float radians = glm::radians(degrees);
    return arc.center + (arc.radius * glm::vec3{std::sin(radians), 0.0f, std::cos(radians)});
}

} // namespace editor
