#include "level/kit_layout_outline.hpp"

#include "erhe_math/curve_path.hpp"

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

// The size along a side, its sign kept: a reach past the origin holds the minimum.
[[nodiscard]] auto signed_clamp(const float sign, const float reach, const float min_size, const float max_size) -> float
{
    return sign * std::clamp(sign * reach, min_size, max_size);
}

[[nodiscard]] auto get_sign(const float value) -> float
{
    return (value < 0.0f) ? -1.0f : 1.0f;
}

// The spacing whose far grid edge, count - 1/2 spacings out (each cell half a spacing beyond its center), is at reach.
[[nodiscard]] auto get_grid_spacing(const float reach, const int count, const float spacing) -> float
{
    const float cells = static_cast<float>(count) - 0.5f;
    if (!(cells > 0.0f)) {
        return spacing;
    }
    return signed_clamp(get_sign(spacing), reach / cells, c_min_layout_handle_step, c_max_grid_spacing);
}

// Count mode: the run's end, set by the step (a run of one has none); gap mode: the end of the length along the direction.
void make_line_handle(const Kit_layout_pattern& pattern, Kit_layout_outline& out)
{
    using Value = Kit_region_handle_value;
    const float given_length = glm::length(pattern.direction);
    if (!(given_length > 0.0f)) {
        return;
    }
    const glm::vec3 direction = pattern.direction / given_length;
    if (pattern.count_mode == Kit_layout_count_mode::gap) {
        out.region_handles.push_back(Kit_region_handle{.position = pattern.length * direction, .side = direction, .value = Value::length});
        return;
    }
    if ((pattern.count < 2) || (pattern.step == 0.0f)) {
        return;
    }
    const float     reach = pattern.step * static_cast<float>(pattern.count - 1);
    const glm::vec3 side  = get_sign(pattern.step) * direction;
    out.region_handles.push_back(Kit_region_handle{.position = reach * direction, .side = side, .value = Value::step});
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
    make_line_handle(pattern, out);
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
    // The far corner, then the middles of the far edges: the outline's sides away from element 0.
    using Value = Kit_region_handle_value;
    const float sign_x = get_sign(grid.spacing_x);
    const float sign_z = get_sign(grid.spacing_z);
    const float mid_x  = 0.5f * (x0 + x1);
    const float mid_z  = 0.5f * (z0 + z1);
    out.region_handles.push_back(Kit_region_handle{.position = glm::vec3{x1, 0.0f, z1},    .side = glm::vec3{sign_x, 0.0f, sign_z}, .value = Value::spacing_x_z});
    out.region_handles.push_back(Kit_region_handle{.position = glm::vec3{x1, 0.0f, mid_z}, .side = glm::vec3{sign_x, 0.0f, 0.0f},   .value = Value::spacing_x});
    out.region_handles.push_back(Kit_region_handle{.position = glm::vec3{mid_x, 0.0f, z1}, .side = glm::vec3{0.0f, 0.0f, sign_z},   .value = Value::spacing_z});
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
    // The radius handle at the arc's middle (at least 60 degrees round) moves along its offset from element 0, which
    // grows with the radius.
    using Value = Kit_region_handle_value;
    const float     degrees    = out.arcs.back().degrees;
    const float     middle     = glm::radians(std::max(0.5f * degrees, c_ring_radius_handle_min_degrees));
    const glm::vec3 per_radius{std::sin(middle), 0.0f, std::cos(middle) - 1.0f};
    out.region_handles.push_back(Kit_region_handle{.position = ring.radius * per_radius, .side = per_radius, .value = Value::radius});
    const float     end = glm::radians(degrees);
    const glm::vec3 end_direction{std::sin(end), 0.0f, std::cos(end)};
    out.region_handles.push_back(Kit_region_handle{.position = center + ((c_ring_arc_handle_inset * ring.radius) * end_direction), .side = end_direction, .value = Value::arc_degrees});
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

// The chords through the curve's points and a tick at each point across the heading of the segments beside it.
void make_segments(const erhe::math::Curve_path_table& table, Kit_layout_outline& out)
{
    const std::size_t span_count = table.get_span_count();
    if (span_count == 0) {
        return;
    }
    const bool closed = (table.get_closure() == erhe::math::Curve_closure::closed);
    const std::size_t point_count = closed ? span_count : (span_count + 1);
    for (std::size_t i = 0; i < point_count; ++i) {
        out.polyline.push_back((i < span_count) ? table.get_span(i).control[0] : table.get_span(span_count - 1).control[3]);
    }
    out.closed = closed;
    for (std::size_t i = 0; i < point_count; ++i) {
        const bool has_before = closed || (i > 0);
        const bool has_after  = closed || (i < span_count);
        // The unit headings' sum: a corner's tick bisects it.
        const auto unit_chord = [&table](const std::size_t span_index) -> glm::vec3 {
            const erhe::math::Curve_span& span   = table.get_span(span_index);
            const glm::vec3               chord  = span.control[3] - span.control[0];
            const float                   length = glm::length(chord);
            return (length > 0.0f) ? (chord / length) : glm::vec3{0.0f};
        };
        glm::vec3 heading{0.0f};
        if (has_before) {
            heading += unit_chord((i + span_count - 1) % span_count);
        }
        if (has_after) {
            heading += unit_chord(i % span_count);
        }
        out.ticks.push_back(Kit_layout_tick{.position = out.polyline[i], .direction = get_line_tick_direction(heading)});
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
        case Kit_region_handle_value::step:          return "step";
        case Kit_region_handle_value::length:        return "length";
        case Kit_region_handle_value::spacing_x:     return "spacing_x";
        case Kit_region_handle_value::spacing_z:     return "spacing_z";
        case Kit_region_handle_value::spacing_x_z:   return "spacing_x_z";
        case Kit_region_handle_value::radius:        return "radius";
        case Kit_region_handle_value::arc_degrees:   return "arc_degrees";
        default:                                     return "?";
    }
}

auto get_layout_handle_set(const Kit_layout_pattern& pattern) -> Kit_layout_handle_set
{
    Kit_layout_handle_set set{.kind = pattern.kind};
    switch (pattern.kind) {
        case Repeat_pattern_kind::line: {
            set.count_mode = pattern.count_mode;
            const bool has_end = (pattern.count_mode == Kit_layout_count_mode::gap) || ((pattern.count >= 2) && (pattern.step != 0.0f));
            set.count = has_end ? 1 : 0;
            break;
        }
        case Repeat_pattern_kind::grid: set.count = 3; break;
        case Repeat_pattern_kind::ring: set.count = 2; break;
        case Repeat_pattern_kind::scatter: {
            set.region = pattern.region;
            set.count  = (pattern.region == Repeat_scatter_region::disc) ? 4 : 8;
            break;
        }
        default: break;
    }
    return set;
}

auto begin_layout_handle_drag(const Kit_layout_pattern& start) -> Kit_layout_handle_drag
{
    return Kit_layout_handle_drag{.arc_degrees = start.arc_degrees};
}

void apply_region_handle(
    const Kit_region_handle& handle,
    const glm::vec3&         point,
    Kit_layout_handle_drag&  in_out_drag,
    Kit_layout_pattern&      in_out_pattern
)
{
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
        return;
    }
    const auto clamped = [](const float size) -> float {
        return std::clamp(size, c_min_scatter_region_size, c_max_scatter_region_size);
    };
    Kit_layout_pattern& pattern = in_out_pattern;
    switch (handle.value) {
        // The region is centred on the node's origin, so an edge drag resizes it about the origin: twice the reach.
        case Kit_region_handle_value::size_x: {
            pattern.size_x = clamped(2.0f * (handle.side.x * point.x));
            break;
        }
        case Kit_region_handle_value::size_z: {
            pattern.size_z = clamped(2.0f * (handle.side.z * point.z));
            break;
        }
        case Kit_region_handle_value::size_x_z: {
            pattern.size_x = clamped(2.0f * (handle.side.x * point.x));
            pattern.size_z = clamped(2.0f * (handle.side.z * point.z));
            break;
        }
        case Kit_region_handle_value::region_radius: {
            pattern.region_radius = clamped(std::sqrt((point.x * point.x) + (point.z * point.z)));
            break;
        }
        case Kit_region_handle_value::step: {
            // The run's end is count - 1 steps out; side points along the run, so the step keeps its sign.
            if (pattern.count < 2) {
                break;
            }
            const float reach = glm::dot(point, handle.side) / static_cast<float>(pattern.count - 1);
            pattern.step = get_sign(pattern.step) * std::clamp(reach, c_min_layout_handle_step, c_max_line_step);
            break;
        }
        case Kit_region_handle_value::length: {
            pattern.length = std::clamp(glm::dot(point, handle.side), 0.0f, c_max_line_length);
            break;
        }
        case Kit_region_handle_value::spacing_x: {
            pattern.spacing_x = get_grid_spacing(point.x, pattern.count_x, pattern.spacing_x);
            break;
        }
        case Kit_region_handle_value::spacing_z: {
            pattern.spacing_z = get_grid_spacing(point.z, pattern.count_z, pattern.spacing_z);
            break;
        }
        case Kit_region_handle_value::spacing_x_z: {
            pattern.spacing_x = get_grid_spacing(point.x, pattern.count_x, pattern.spacing_x);
            pattern.spacing_z = get_grid_spacing(point.z, pattern.count_z, pattern.spacing_z);
            break;
        }
        case Kit_region_handle_value::radius: {
            const float per_radius = glm::dot(handle.side, handle.side);
            if (!(per_radius > 0.0f)) {
                break;
            }
            pattern.radius = std::clamp(glm::dot(point, handle.side) / per_radius, c_min_ring_radius, c_max_ring_radius);
            break;
        }
        case Kit_region_handle_value::arc_degrees: {
            // The angle about the center, counter-clockwise from element 0 seen from above, unwrapped to the turn nearest
            // the last unclamped one.
            const float dx = point.x;
            const float dz = point.z + pattern.radius;
            if (((dx * dx) + (dz * dz)) == 0.0f) {
                break;
            }
            const float angle = glm::degrees(std::atan2(dx, dz));
            const float turns = std::round((in_out_drag.arc_degrees - angle) / 360.0f);
            in_out_drag.arc_degrees = angle + (360.0f * turns);
            pattern.arc_degrees     = std::clamp(in_out_drag.arc_degrees, c_min_ring_arc, c_max_ring_arc);
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
    const erhe::math::Curve_path_table* const  path_table,
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
        case Repeat_pattern_kind::segments: {
            if (path_table != nullptr) {
                make_segments(*path_table, out_outline);
            }
            break;
        }
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
