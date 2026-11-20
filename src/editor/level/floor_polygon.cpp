#include "level/floor_polygon.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"

#include "erhe_math/curve_path.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace editor {

namespace {

constexpr std::array<const char*, c_floor_max_edges> c_edge_names{
    "edge_0", "edge_1", "edge_2", "edge_3", "edge_4", "edge_5", "edge_6", "edge_7",
    "edge_8", "edge_9", "edge_10", "edge_11", "edge_12", "edge_13", "edge_14", "edge_15",
    "edge_16", "edge_17", "edge_18", "edge_19", "edge_20", "edge_21", "edge_22", "edge_23",
    "edge_24", "edge_25", "edge_26", "edge_27", "edge_28", "edge_29", "edge_30", "edge_31",
    "edge_32", "edge_33", "edge_34", "edge_35", "edge_36", "edge_37", "edge_38", "edge_39",
    "edge_40", "edge_41", "edge_42", "edge_43", "edge_44", "edge_45", "edge_46", "edge_47",
    "edge_48", "edge_49", "edge_50", "edge_51", "edge_52", "edge_53", "edge_54", "edge_55",
    "edge_56", "edge_57", "edge_58", "edge_59", "edge_60", "edge_61", "edge_62", "edge_63"
};

constexpr std::array<const char*, c_floor_max_edges> c_floor_edge_names{
    "floor_0", "floor_1", "floor_2", "floor_3", "floor_4", "floor_5", "floor_6", "floor_7",
    "floor_8", "floor_9", "floor_10", "floor_11", "floor_12", "floor_13", "floor_14", "floor_15",
    "floor_16", "floor_17", "floor_18", "floor_19", "floor_20", "floor_21", "floor_22", "floor_23",
    "floor_24", "floor_25", "floor_26", "floor_27", "floor_28", "floor_29", "floor_30", "floor_31",
    "floor_32", "floor_33", "floor_34", "floor_35", "floor_36", "floor_37", "floor_38", "floor_39",
    "floor_40", "floor_41", "floor_42", "floor_43", "floor_44", "floor_45", "floor_46", "floor_47",
    "floor_48", "floor_49", "floor_50", "floor_51", "floor_52", "floor_53", "floor_54", "floor_55",
    "floor_56", "floor_57", "floor_58", "floor_59", "floor_60", "floor_61", "floor_62", "floor_63"
};

constexpr std::array<const char*, c_floor_max_edges> c_rail_names{
    "rail_0", "rail_1", "rail_2", "rail_3", "rail_4", "rail_5", "rail_6", "rail_7",
    "rail_8", "rail_9", "rail_10", "rail_11", "rail_12", "rail_13", "rail_14", "rail_15",
    "rail_16", "rail_17", "rail_18", "rail_19", "rail_20", "rail_21", "rail_22", "rail_23",
    "rail_24", "rail_25", "rail_26", "rail_27", "rail_28", "rail_29", "rail_30", "rail_31",
    "rail_32", "rail_33", "rail_34", "rail_35", "rail_36", "rail_37", "rail_38", "rail_39",
    "rail_40", "rail_41", "rail_42", "rail_43", "rail_44", "rail_45", "rail_46", "rail_47",
    "rail_48", "rail_49", "rail_50", "rail_51", "rail_52", "rail_53", "rail_54", "rail_55",
    "rail_56", "rail_57", "rail_58", "rail_59", "rail_60", "rail_61", "rail_62", "rail_63"
};

[[nodiscard]] auto cross2(const glm::vec2& a, const glm::vec2& b) -> float
{
    return (a.x * b.y) - (a.y * b.x);
}

[[nodiscard]] auto orientation(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) -> float
{
    return cross2(b - a, c - a);
}

[[nodiscard]] auto on_segment(const glm::vec2& a, const glm::vec2& b, const glm::vec2& p) -> bool
{
    return (std::min(a.x, b.x) <= p.x) && (p.x <= std::max(a.x, b.x)) && (std::min(a.y, b.y) <= p.y) && (p.y <= std::max(a.y, b.y));
}

// Segments ab and cd touch or cross (collinear overlap included).
[[nodiscard]] auto segments_meet(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d) -> bool
{
    // Orientations are areas: the tolerance scales with the segments' lengths.
    const float epsilon = 1.0e-6f * std::max(glm::dot(b - a, b - a), glm::dot(d - c, d - c));
    const float o1 = orientation(a, b, c);
    const float o2 = orientation(a, b, d);
    const float o3 = orientation(c, d, a);
    const float o4 = orientation(c, d, b);
    if ((((o1 > epsilon) && (o2 < -epsilon)) || ((o1 < -epsilon) && (o2 > epsilon))) &&
        (((o3 > epsilon) && (o4 < -epsilon)) || ((o3 < -epsilon) && (o4 > epsilon)))) {
        return true;
    }
    return
        ((std::abs(o1) <= epsilon) && on_segment(a, b, c)) ||
        ((std::abs(o2) <= epsilon) && on_segment(a, b, d)) ||
        ((std::abs(o3) <= epsilon) && on_segment(c, d, a)) ||
        ((std::abs(o4) <= epsilon) && on_segment(c, d, b));
}

[[nodiscard]] auto inside_triangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const float winding) -> bool
{
    const float d1 = orientation(a, b, p) * winding;
    const float d2 = orientation(b, c, p) * winding;
    const float d3 = orientation(c, a, p) * winding;
    return (d1 >= 0.0f) && (d2 >= 0.0f) && (d3 >= 0.0f);
}

} // anonymous namespace

auto check_floor_values(const Floor_values& values) -> std::string
{
    if (!std::isfinite(values.thickness) || (values.thickness < 0.01f) || (values.thickness > 4.0f)) {
        return fmt::format("floor thickness {} is outside 0.01 - 4 m", values.thickness);
    }
    if (!std::isfinite(values.wall_thickness) || (values.wall_thickness < 0.0f) || (values.wall_thickness > 4.0f)) {
        return fmt::format("floor wall_thickness {} is outside 0 - 4 m", values.wall_thickness);
    }
    if (!std::isfinite(values.room_height) || (values.room_height < 0.1f) || (values.room_height > 32.0f)) {
        return fmt::format("floor room_height {} is outside 0.1 - 32 m", values.room_height);
    }
    if (!std::isfinite(values.rail_inset) || (values.rail_inset < 0.0f) || (values.rail_inset > 4.0f)) {
        return fmt::format("floor rail_inset {} is outside 0 - 4 m", values.rail_inset);
    }
    return {};
}

auto read_floor_outline(const erhe::math::Curve_path_table& table, std::vector<glm::vec2>& out_outline) -> std::string
{
    out_outline.clear();
    if (table.get_closure() != erhe::math::Curve_closure::closed) {
        return "a floor fills a closed curve; this curve is open";
    }
    for (std::size_t i = 0, end = table.get_span_count(); i < end; ++i) {
        const erhe::math::Curve_span& span = table.get_span(i);
        if (span.shape != erhe::math::Curve_span_shape::line) {
            return fmt::format("floor edge {} is curved: a floor's points are corners", i);
        }
        if (std::abs(span.control[0].y) > 1.0e-4f) {
            return fmt::format("floor corner {} is at height {} m: a floor's corners lie on its node's plane (y = 0)", i, span.control[0].y);
        }
        out_outline.push_back(glm::vec2{span.control[0].x, span.control[0].z});
    }
    return {};
}

auto get_floor_outline_area2(const std::span<const glm::vec2> outline) -> float
{
    float sum = 0.0f;
    for (std::size_t i = 0, n = outline.size(); i < n; ++i) {
        sum += cross2(outline[i], outline[(i + 1) % n]);
    }
    return sum;
}

auto check_floor_outline(const std::span<const glm::vec2> outline) -> std::string
{
    const std::size_t n = outline.size();
    if (n < 3) {
        return fmt::format("a floor outline needs at least three corners (it has {})", n);
    }
    if (n > c_floor_max_edges) {
        return fmt::format("a floor outline has at most {} corners (it has {})", c_floor_max_edges, n);
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(outline[i].x) || !std::isfinite(outline[i].y)) {
            return fmt::format("floor corner {} is not finite", i);
        }
        if (get_floor_edge_length(outline, i) < c_floor_min_edge) {
            return fmt::format("floor edge {} is shorter than {} m", i, c_floor_min_edge);
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 2; j < n; ++j) {
            if ((i == 0) && (j == (n - 1))) {
                continue; // neighbours across the closing corner
            }
            if (segments_meet(outline[i], outline[(i + 1) % n], outline[j], outline[(j + 1) % n])) {
                return fmt::format("floor edges {} and {} cross", i, j);
            }
        }
    }
    if (std::abs(get_floor_outline_area2(outline)) < 1.0e-6f) {
        return "the floor outline has no area";
    }
    return {};
}

auto check_floor_outline_change(const std::span<const glm::vec2> before, const std::span<const glm::vec2> after) -> std::string
{
    std::string error = check_floor_outline(after);
    if (!error.empty()) {
        return error;
    }
    if ((get_floor_outline_area2(before) < 0.0f) != (get_floor_outline_area2(after) < 0.0f)) {
        return "the floor outline would turn inside out";
    }
    return {};
}

auto make_floor_path(const std::span<const glm::vec2> outline) -> erhe::math::Curve_path
{
    erhe::math::Curve_path path;
    path.closure = erhe::math::Curve_closure::closed;
    for (const glm::vec2& corner : outline) {
        erhe::math::Curve_point point{};
        point.position = glm::vec3{corner.x, 0.0f, corner.y};
        point.kind     = erhe::math::Curve_point_kind::corner;
        path.points.push_back(point);
    }
    return path;
}

void make_floor_rectangle(const float width, const float depth, std::vector<glm::vec2>& out_outline)
{
    const float x = 0.5f * width;
    const float z = 0.5f * depth;
    out_outline.clear();
    out_outline.push_back(glm::vec2{-x, -z});
    out_outline.push_back(glm::vec2{-x,  z});
    out_outline.push_back(glm::vec2{ x,  z});
    out_outline.push_back(glm::vec2{ x, -z});
}

auto make_floor_mesh(const std::span<const glm::vec2> outline, const Floor_values& values, GEO::Mesh& mesh) -> std::string
{
    std::string error = check_floor_values(values);
    if (!error.empty()) {
        return error;
    }
    error = check_floor_outline(outline);
    if (!error.empty()) {
        return error;
    }
    const std::size_t n = outline.size();
    // Corner order counter-clockwise seen from above (a negative plan area), as Kit_mesh_builder::add_box's top.
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = (get_floor_outline_area2(outline) < 0.0f) ? i : (n - 1 - i);
    }
    Kit_mesh_builder builder;
    builder.set_slot(0);
    std::vector<uint32_t> top(n);
    std::vector<uint32_t> bottom(n);
    for (std::size_t k = 0; k < n; ++k) {
        const glm::vec2& p = outline[order[k]];
        top[k]    = builder.add_vertex(glm::vec3{p.x, values.thickness, p.y});
        bottom[k] = builder.add_vertex(glm::vec3{p.x, 0.0f, p.y});
    }
    // Ear clipping over the ordered corners; the winding sign makes a convex corner positive.
    const float winding = -1.0f;
    std::vector<std::size_t> remaining(n);
    for (std::size_t k = 0; k < n; ++k) {
        remaining[k] = k;
    }
    const auto at = [&](const std::size_t k) -> const glm::vec2& { return outline[order[k]]; };
    while (remaining.size() > 3) {
        bool clipped = false;
        const std::size_t m = remaining.size();
        for (std::size_t r = 0; r < m; ++r) {
            const std::size_t a = remaining[(r + m - 1) % m];
            const std::size_t b = remaining[r];
            const std::size_t c = remaining[(r + 1) % m];
            if ((orientation(at(a), at(b), at(c)) * winding) <= 0.0f) {
                continue; // reflex or flat
            }
            bool empty = true;
            for (const std::size_t other : remaining) {
                if ((other != a) && (other != b) && (other != c) && inside_triangle(at(other), at(a), at(b), at(c), winding)) {
                    empty = false;
                    break;
                }
            }
            if (!empty) {
                continue;
            }
            builder.add_triangle(top[a], top[b], top[c]);
            builder.add_triangle(bottom[a], bottom[c], bottom[b]);
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(r));
            clipped = true;
            break;
        }
        if (!clipped) {
            return "the floor outline could not be triangulated";
        }
    }
    builder.add_triangle(top[remaining[0]], top[remaining[1]], top[remaining[2]]);
    builder.add_triangle(bottom[remaining[0]], bottom[remaining[2]], bottom[remaining[1]]);
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t next = (k + 1) % n;
        builder.add_quad(bottom[k], bottom[next], top[next], top[k]);
    }
    builder.write(mesh);
    return {};
}

auto get_floor_edge_length(const std::span<const glm::vec2> outline, const std::size_t edge) -> float
{
    return glm::length(outline[(edge + 1) % outline.size()] - outline[edge]);
}

auto get_floor_edge_inward(const std::span<const glm::vec2> outline, const std::size_t edge) -> glm::vec2
{
    const glm::vec2 d = outline[(edge + 1) % outline.size()] - outline[edge];
    const float     length = glm::length(d);
    if (!(length > 0.0f)) {
        return glm::vec2{0.0f};
    }
    // A negative plan area is clockwise in (x, z): the inside is to the right of each edge.
    const glm::vec2 inward = (get_floor_outline_area2(outline) < 0.0f) ? glm::vec2{d.y, -d.x} : glm::vec2{-d.y, d.x};
    return inward / length;
}

auto get_floor_edge_connector_name(const std::size_t edge) -> const char*
{
    return (edge < c_edge_names.size()) ? c_edge_names[edge] : "";
}

auto get_floor_floor_connector_name(const std::size_t edge) -> const char*
{
    return (edge < c_floor_edge_names.size()) ? c_floor_edge_names[edge] : "";
}

auto get_floor_rail_connector_name(const std::size_t edge) -> const char*
{
    return (edge < c_rail_names.size()) ? c_rail_names[edge] : "";
}

auto get_floor_connector_name(const Floor_connector_family family, const std::size_t edge) -> const char*
{
    switch (family) {
        case Floor_connector_family::floor: return get_floor_floor_connector_name(edge);
        case Floor_connector_family::rail:  return get_floor_rail_connector_name(edge);
        case Floor_connector_family::edge:
        default:                            return get_floor_edge_connector_name(edge);
    }
}

auto parse_floor_connector_name(const std::string_view name, const std::size_t edge_count) -> std::optional<Floor_connector_name>
{
    Floor_connector_family family = Floor_connector_family::edge;
    if (name.starts_with("floor_")) {
        family = Floor_connector_family::floor;
    } else if (name.starts_with("rail_")) {
        family = Floor_connector_family::rail;
    } else if (!name.starts_with("edge_")) {
        return std::nullopt;
    }
    Floor_connector_name result{.family = family, .edge = edge_count};
    for (std::size_t k = 0, end = std::min(edge_count, c_floor_max_edges); k < end; ++k) {
        if (name == get_floor_connector_name(family, k)) {
            result.edge = k;
        }
    }
    return result;
}

namespace {

// The crossing of edge (i - 1)'s and edge i's lines, each moved inset along its inward (C10.2).
[[nodiscard]] auto get_inset_corner(const std::span<const glm::vec2> outline, const float inset, const std::size_t i) -> glm::vec2
{
    const std::size_t n        = outline.size();
    const std::size_t previous = (i + n - 1) % n;
    const glm::vec2   n0       = get_floor_edge_inward(outline, previous);
    const glm::vec2   n1       = get_floor_edge_inward(outline, i);
    const float       det      = cross2(n0, n1);
    if (std::abs(det) < 1.0e-6f) {
        return outline[i] + (inset * n1); // the two edges run on one line
    }
    // dot(p, n0) = dot(a0, n0) + inset and dot(p, n1) = dot(a1, n1) + inset, solved by Cramer's rule.
    const float c0 = glm::dot(outline[previous], n0) + inset;
    const float c1 = glm::dot(outline[i], n1) + inset;
    return glm::vec2{((c0 * n1.y) - (c1 * n0.y)) / det, ((n0.x * c1) - (n1.x * c0)) / det};
}

} // anonymous namespace

auto get_floor_rail_span(const std::span<const glm::vec2> outline, const float rail_inset, const std::size_t edge) -> Floor_rail_span
{
    const std::size_t n = outline.size();
    if ((n < 3) || (edge >= n)) {
        return {};
    }
    const glm::vec2 d      = outline[(edge + 1) % n] - outline[edge];
    const float     length = glm::length(d);
    if (!(length > 0.0f)) {
        return {};
    }
    const glm::vec2 start = get_inset_corner(outline, rail_inset, edge);
    const glm::vec2 end   = get_inset_corner(outline, rail_inset, (edge + 1) % n);
    return Floor_rail_span{.start = start, .end = end, .length = glm::dot(end - start, d / length)};
}

void get_floor_connectors(const std::span<const glm::vec2> outline, const Floor_values& values, std::vector<Kit_connector>& out_connectors)
{
    out_connectors.clear();
    const std::size_t n = std::min(outline.size(), c_floor_max_edges);
    if (n < 3) {
        return;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 inward = get_floor_edge_inward(outline, i);
        const glm::vec2 mid    = 0.5f * (outline[i] + outline[(i + 1) % outline.size()]) + (0.5f * values.wall_thickness * inward);
        out_connectors.push_back(
            Kit_connector{
                .name     = c_edge_names[i],
                .position = glm::vec3{mid.x, values.thickness, mid.y},
                .outward  = glm::vec3{0.0f, 1.0f, 0.0f},
                .up       = glm::vec3{inward.x, 0.0f, inward.y},
                .types    = Kit_connector_types::of(Kit_connector_type::wall_floor)
            }
        );
    }
    // A floor joins another floor edge to edge: on the edge's midpoint at the top, facing out, level.
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 inward = get_floor_edge_inward(outline, i);
        const glm::vec2 mid    = 0.5f * (outline[i] + outline[(i + 1) % outline.size()]);
        out_connectors.push_back(
            Kit_connector{
                .name     = c_floor_edge_names[i],
                .position = glm::vec3{mid.x, values.thickness, mid.y},
                .outward  = glm::vec3{-inward.x, 0.0f, -inward.y},
                .up       = glm::vec3{0.0f, 1.0f, 0.0f},
                .types    = Kit_connector_types::of(Kit_connector_type::floor)
            }
        );
    }
    // C10.2: a rider stands on the middle of its edge's rail span, running along the edge, its front toward the inside.
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2       inward = get_floor_edge_inward(outline, i);
        const Floor_rail_span span   = get_floor_rail_span(outline, values.rail_inset, i);
        const glm::vec2       mid    = 0.5f * (span.start + span.end);
        out_connectors.push_back(
            Kit_connector{
                .name     = c_rail_names[i],
                .position = glm::vec3{mid.x, values.thickness, mid.y},
                .outward  = glm::vec3{0.0f, 1.0f, 0.0f},
                .up       = glm::vec3{inward.x, 0.0f, inward.y},
                .types    = Kit_connector_types::of(Kit_connector_type::rail),
                .rail_end = Kit_rail_end::host
            }
        );
    }
}

void get_floor_supply(
    const std::span<const glm::vec2> outline,
    const Floor_values&              values,
    const std::string_view           wall_material,
    const std::size_t                edge,
    Kit_supply_list&                 out_supply
)
{
    out_supply.clear();
    if (edge >= outline.size()) {
        return;
    }
    const float length = get_floor_edge_length(outline, edge) - values.wall_thickness;
    if (length >= c_floor_min_wall_length) {
        out_supply.add(Kit_supplied_value{.property = Kit_standard_property::child_length, .priority = c_floor_supply_priority, .length = length});
    }
    out_supply.add(Kit_supplied_value{.property = Kit_standard_property::wall_thickness, .priority = c_floor_supply_priority, .length = values.wall_thickness});
    out_supply.add(Kit_supplied_value{.property = Kit_standard_property::room_height,    .priority = c_floor_supply_priority, .length = values.room_height});
    out_supply.add(Kit_supplied_value{.property = Kit_standard_property::wall_material,  .priority = c_floor_supply_priority, .material = wall_material});
}

void get_floor_rail_supply(const std::span<const glm::vec2> outline, const Floor_values& values, const std::size_t edge, Kit_supply_list& out_supply)
{
    out_supply.clear();
    if (edge >= outline.size()) {
        return;
    }
    const float length = get_floor_rail_span(outline, values.rail_inset, edge).length;
    if (length >= c_floor_min_wall_length) {
        out_supply.add(Kit_supplied_value{.property = Kit_standard_property::child_length, .priority = c_floor_supply_priority, .length = length});
    }
}

void get_floor_connector_supply(
    const std::span<const glm::vec2> outline,
    const Floor_values&              values,
    const std::string_view           wall_material,
    const std::string_view           connector_name,
    Kit_supply_list&                 out_supply
)
{
    out_supply.clear();
    const std::optional<Floor_connector_name> name = parse_floor_connector_name(connector_name, outline.size());
    if (!name.has_value() || (name.value().edge >= outline.size())) {
        return;
    }
    if (name.value().family == Floor_connector_family::edge) {
        get_floor_supply(outline, values, wall_material, name.value().edge, out_supply);
    } else if (name.value().family == Floor_connector_family::rail) {
        get_floor_rail_supply(outline, values, name.value().edge, out_supply);
    }
}

auto make_floor_source_text(const std::span<const glm::vec2> outline, const Floor_values& values) -> std::string
{
    std::string text = fmt::format("{{\"generator\":{},\"outline\":[", c_floor_generator_version);
    for (std::size_t i = 0, n = outline.size(); i < n; ++i) {
        text += fmt::format("{}\"{}\",\"{}\"", (i == 0) ? "" : ",", format_hexfloat(outline[i].x), format_hexfloat(outline[i].y));
    }
    text += fmt::format(
        "],\"room_height\":\"{}\",\"thickness\":\"{}\",\"wall_thickness\":\"{}\"}}",
        format_hexfloat(values.room_height), format_hexfloat(values.thickness), format_hexfloat(values.wall_thickness)
    );
    return text;
}

} // namespace editor
