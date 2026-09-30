#include "erhe_geometry/operation/knife_cut.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

namespace {

// Edge and segment parameters closer than this to an end are that end.
constexpr float c_parameter_epsilon = 1e-5f;

// Adds scale * src into dst, merging entries with the same source index.
void accumulate_sources(std::vector<Edit_source>& dst, const std::vector<Edit_source>& src, const float scale)
{
    if (scale == 0.0f) {
        return;
    }
    for (const Edit_source& entry : src) {
        const float weight = scale * entry.first;
        bool found = false;
        for (Edit_source& existing : dst) {
            if (existing.second == entry.second) {
                existing.first += weight;
                found = true;
                break;
            }
        }
        if (!found) {
            dst.emplace_back(weight, entry.second);
        }
    }
}

[[nodiscard]] auto cross_2d(const GEO::vec2f& a, const GEO::vec2f& b) -> float
{
    return (a.x * b.y) - (a.y * b.x);
}

// Crossing number test.
[[nodiscard]] auto is_point_in_polygon(const std::span<const GEO::vec2f> polygon, const GEO::vec2f& p) -> bool
{
    bool inside = false;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const GEO::vec2f& a = polygon[i];
        const GEO::vec2f& b = polygon[j];
        if ((a.y > p.y) != (b.y > p.y)) {
            const float x = a.x + (((p.y - a.y) / (b.y - a.y)) * (b.x - a.x));
            if (p.x < x) {
                inside = !inside;
            }
        }
    }
    return inside;
}

// Proper crossing of segments p0 - p1 and q0 - q1: u along p, v along q,
// both strictly inside their segments.
[[nodiscard]] auto intersect_segments(
    const GEO::vec2f& p0,
    const GEO::vec2f& p1,
    const GEO::vec2f& q0,
    const GEO::vec2f& q1,
    float&            u,
    float&            v
) -> bool
{
    const GEO::vec2f r     = p1 - p0;
    const GEO::vec2f s     = q1 - q0;
    const float      denom = cross_2d(r, s);
    if (std::abs(denom) <= (1e-12f * GEO::length(r) * GEO::length(s))) {
        return false;
    }
    const GEO::vec2f qp = q0 - p0;
    u = cross_2d(qp, s) / denom;
    v = cross_2d(qp, r) / denom;
    return
        (u > c_parameter_epsilon) && (u < (1.0f - c_parameter_epsilon)) &&
        (v > c_parameter_epsilon) && (v < (1.0f - c_parameter_epsilon));
}

} // anonymous namespace

Knife_cut::Knife_cut(const Geometry& source, Geometry& destination, const Knife_view& view, const Knife_options& options)
    : Edit_mesh_operation{source, destination}
    , m_view             {view}
    , m_options          {options}
{
}

auto Knife_cut::get_point_count() const -> std::size_t
{
    return m_points.size();
}

void Knife_cut::add_point(const Knife_point& point)
{
    if (m_finished) {
        log_operation->error("Knife_cut::add_point(): the cut is already finished");
        return;
    }
    Knife_vertex element;
    if (!make_element(point, element)) {
        log_operation->warn("Knife_cut::add_point(): the point names an element the mesh does not have; ignored");
        return;
    }
    m_points.push_back(point);
    process_point(m_points.size() - 1);
}

void Knife_cut::undo_last_point()
{
    if (m_finished || m_points.empty()) {
        return;
    }
    m_points.pop_back();
    rebuild();
}

void Knife_cut::rebuild()
{
    m_knife_vertices.clear();
    m_knife_edges.clear();
    for (std::size_t i = 0; i < m_points.size(); ++i) {
        process_point(i);
    }
}

void Knife_cut::process_point(const std::size_t point_index)
{
    if (point_index == 0) {
        Knife_vertex element;
        if (make_element(m_points[0], element)) {
            static_cast<void>(find_or_add_knife_vertex(element));
        }
        return;
    }
    process_segment(m_points[point_index - 1], m_points[point_index]);
}

void Knife_cut::get_preview_segments(std::vector<std::pair<GEO::vec3f, GEO::vec3f>>& out_segments) const
{
    out_segments.clear();
    for (const Knife_edge& edge : m_knife_edges) {
        out_segments.emplace_back(m_knife_vertices[edge.a].position, m_knife_vertices[edge.b].position);
    }
}

auto Knife_cut::make_element(const Knife_point& point, Knife_vertex& out_element) -> bool
{
    out_element = Knife_vertex{};
    switch (point.snap) {
        case Knife_snap::vertex: {
            if ((point.vertex >= m_edit_mesh.get_vertex_slot_count()) || !m_edit_mesh.is_vertex_alive(point.vertex)) {
                return false;
            }
            out_element.kind     = Knife_snap::vertex;
            out_element.vertex   = point.vertex;
            out_element.position = m_edit_mesh.get_position(point.vertex);
            return true;
        }
        case Knife_snap::edge: {
            const GEO::index_t slot_count = m_edit_mesh.get_vertex_slot_count();
            if ((point.edge_v0 >= slot_count) || (point.edge_v1 >= slot_count) || (point.edge_v0 == point.edge_v1)) {
                return false;
            }
            if (m_edit_mesh.find_edge(point.edge_v0, point.edge_v1) == GEO::NO_INDEX) {
                return false;
            }
            const GEO::index_t v0 = std::min(point.edge_v0, point.edge_v1);
            const GEO::index_t v1 = std::max(point.edge_v0, point.edge_v1);
            const GEO::vec3f   a  = m_edit_mesh.get_position(v0);
            const GEO::vec3f   b  = m_edit_mesh.get_position(v1);
            const GEO::vec3f   ab = b - a;
            const float length_squared = GEO::dot(ab, ab);
            const float t = (length_squared > 0.0f) ? std::clamp(GEO::dot(point.position - a, ab) / length_squared, 0.0f, 1.0f) : 0.0f;
            if (t <= c_parameter_epsilon) {
                out_element.kind     = Knife_snap::vertex;
                out_element.vertex   = v0;
                out_element.position = a;
                return true;
            }
            if (t >= (1.0f - c_parameter_epsilon)) {
                out_element.kind     = Knife_snap::vertex;
                out_element.vertex   = v1;
                out_element.position = b;
                return true;
            }
            out_element.kind     = Knife_snap::edge;
            out_element.edge_v0  = v0;
            out_element.edge_v1  = v1;
            out_element.t        = t;
            out_element.position = a + (t * ab);
            return true;
        }
        case Knife_snap::facet: {
            if ((point.facet >= m_edit_mesh.get_facet_slot_count()) || !m_edit_mesh.is_facet_alive(point.facet)) {
                return false;
            }
            GEO::vec3f normal;
            GEO::vec3f axis_u;
            GEO::vec3f axis_v;
            GEO::vec3f origin;
            make_facet_frame(point.facet, normal, axis_u, axis_v, origin);
            out_element.kind     = Knife_snap::facet;
            out_element.facet    = point.facet;
            out_element.position = point.position - (GEO::dot(point.position - origin, normal) * normal);
            return true;
        }
        default: {
            return false;
        }
    }
}

auto Knife_cut::project(const GEO::vec3f& p, GEO::vec2f& out_pixel) const -> bool
{
    const GEO::mat4& m = m_view.clip_from_mesh;
    double clip[4];
    for (GEO::index_t i = 0; i < 4; ++i) {
        clip[i] =
            (m(i, 0) * static_cast<double>(p.x)) +
            (m(i, 1) * static_cast<double>(p.y)) +
            (m(i, 2) * static_cast<double>(p.z)) +
            m(i, 3);
    }
    if (clip[3] <= 1e-12) {
        return false;
    }
    out_pixel.x = static_cast<float>((((clip[0] / clip[3]) * 0.5) + 0.5) * static_cast<double>(m_view.viewport_width));
    out_pixel.y = static_cast<float>((((clip[1] / clip[3]) * 0.5) + 0.5) * static_cast<double>(m_view.viewport_height));
    return true;
}

auto Knife_cut::get_depth(const GEO::vec3f& p) const -> float
{
    return m_view.perspective
        ? GEO::length(p - m_view.eye_in_mesh)
        : GEO::dot(p, m_view.view_direction_in_mesh);
}

auto Knife_cut::get_screen_distance(const GEO::vec3f& first, const GEO::vec3f& second) const -> float
{
    GEO::vec2f lhs_pixel;
    GEO::vec2f rhs_pixel;
    if (!project(first, lhs_pixel) || !project(second, rhs_pixel)) {
        // Behind the eye: only an identical point is the same.
        return (GEO::length(first - second) == 0.0f) ? 0.0f : std::numeric_limits<float>::max();
    }
    return GEO::length(lhs_pixel - rhs_pixel);
}

auto Knife_cut::same_element(const Knife_vertex& first, const Knife_vertex& second) const -> bool
{
    if (first.kind != second.kind) {
        return false;
    }
    switch (first.kind) {
        case Knife_snap::vertex: {
            return first.vertex == second.vertex;
        }
        case Knife_snap::edge: {
            return
                (first.edge_v0 == second.edge_v0) &&
                (first.edge_v1 == second.edge_v1) &&
                (get_screen_distance(first.position, second.position) <= m_options.edge_tolerance_px);
        }
        case Knife_snap::facet: {
            return
                (first.facet == second.facet) &&
                (get_screen_distance(first.position, second.position) <= m_options.facet_tolerance_px);
        }
        default: {
            return false;
        }
    }
}

auto Knife_cut::on_common_edge(const Knife_vertex& first, const Knife_vertex& second) const -> bool
{
    if ((first.kind == Knife_snap::vertex) && (second.kind == Knife_snap::vertex)) {
        return m_edit_mesh.find_edge(first.vertex, second.vertex) != GEO::NO_INDEX;
    }
    if ((first.kind == Knife_snap::vertex) && (second.kind == Knife_snap::edge)) {
        return (first.vertex == second.edge_v0) || (first.vertex == second.edge_v1);
    }
    if ((first.kind == Knife_snap::edge) && (second.kind == Knife_snap::vertex)) {
        return (second.vertex == first.edge_v0) || (second.vertex == first.edge_v1);
    }
    if ((first.kind == Knife_snap::edge) && (second.kind == Knife_snap::edge)) {
        return (first.edge_v0 == second.edge_v0) && (first.edge_v1 == second.edge_v1);
    }
    return false;
}

auto Knife_cut::contains_element(const GEO::index_t facet, const Knife_vertex& element) const -> bool
{
    switch (element.kind) {
        case Knife_snap::vertex: return m_edit_mesh.find_facet_corner(facet, element.vertex) != GEO::NO_INDEX;
        case Knife_snap::edge:   return m_edit_mesh.are_adjacent_in_facet(facet, element.edge_v0, element.edge_v1);
        case Knife_snap::facet:  return element.facet == facet;
        default:                 return false;
    }
}

auto Knife_cut::belongs_to(const Knife_vertex& element, const GEO::index_t facet) const -> bool
{
    return contains_element(facet, element);
}

void Knife_cut::make_facet_frame(
    const GEO::index_t facet,
    GEO::vec3f&        out_normal,
    GEO::vec3f&        out_axis_u,
    GEO::vec3f&        out_axis_v,
    GEO::vec3f&        out_origin
)
{
    m_polygon.clear();
    GEO::vec3f sum{0.0f, 0.0f, 0.0f};
    for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
        const GEO::vec3f p = m_edit_mesh.get_position(corner.vertex);
        m_polygon.push_back(p);
        sum += p;
    }
    out_normal = compute_newell_normal(m_polygon);
    out_origin = m_polygon.empty() ? sum : (sum / static_cast<float>(m_polygon.size()));
    out_axis_u = GEO::normalize(
        (std::abs(out_normal.x) < 0.9f)
            ? GEO::cross(out_normal, GEO::vec3f{1.0f, 0.0f, 0.0f})
            : GEO::cross(out_normal, GEO::vec3f{0.0f, 1.0f, 0.0f})
    );
    out_axis_v = GEO::cross(out_normal, out_axis_u);
}

auto Knife_cut::is_inside_facet(const GEO::index_t facet, const GEO::vec3f& p) -> bool
{
    GEO::vec3f normal;
    GEO::vec3f axis_u;
    GEO::vec3f axis_v;
    GEO::vec3f origin;
    make_facet_frame(facet, normal, axis_u, axis_v, origin);
    m_polygon_2d.clear();
    for (const GEO::vec3f& q : m_polygon) {
        m_polygon_2d.emplace_back(GEO::dot(q - origin, axis_u), GEO::dot(q - origin, axis_v));
    }
    return is_point_in_polygon(m_polygon_2d, GEO::vec2f{GEO::dot(p - origin, axis_u), GEO::dot(p - origin, axis_v)});
}

auto Knife_cut::is_occluded(const Knife_vertex& element) -> bool
{
    const GEO::vec3f p = element.position;
    const GEO::vec3f direction = m_view.perspective ? (m_view.eye_in_mesh - p) : (-1.0f * m_view.view_direction_in_mesh);
    const float      max_t     = m_view.perspective ? 1.0f : std::numeric_limits<float>::max();
    const float      min_t     = m_view.perspective ? 1e-5f : (1e-5f * (1.0f + GEO::length(p)));
    const GEO::index_t facet_count = m_edit_mesh.get_facet_slot_count();
    for (GEO::index_t facet = 0; facet < facet_count; ++facet) {
        if (!m_edit_mesh.is_facet_alive(facet) || contains_element(facet, element)) {
            continue;
        }
        GEO::vec3f normal;
        GEO::vec3f axis_u;
        GEO::vec3f axis_v;
        GEO::vec3f origin;
        make_facet_frame(facet, normal, axis_u, axis_v, origin);
        const float denom = GEO::dot(normal, direction);
        if (std::abs(denom) < 1e-12f) {
            continue;
        }
        const float t = GEO::dot(normal, origin - p) / denom;
        if ((t <= min_t) || (t >= max_t)) {
            continue;
        }
        const GEO::vec3f q = p + (t * direction);
        m_polygon_2d.clear();
        for (const GEO::vec3f& corner : m_polygon) {
            m_polygon_2d.emplace_back(GEO::dot(corner - origin, axis_u), GEO::dot(corner - origin, axis_v));
        }
        if (is_point_in_polygon(m_polygon_2d, GEO::vec2f{GEO::dot(q - origin, axis_u), GEO::dot(q - origin, axis_v)})) {
            return true;
        }
    }
    return false;
}

auto Knife_cut::find_or_add_knife_vertex(const Knife_vertex& element) -> std::size_t
{
    for (std::size_t i = 0; i < m_knife_vertices.size(); ++i) {
        if (same_element(m_knife_vertices[i], element)) {
            return i;
        }
    }
    m_knife_vertices.push_back(element);
    return m_knife_vertices.size() - 1;
}

void Knife_cut::add_cut_edge(const std::size_t a, const std::size_t b, const GEO::index_t facet)
{
    GEO::vec3f normal;
    GEO::vec3f axis_u;
    GEO::vec3f axis_v;
    GEO::vec3f origin;
    make_facet_frame(facet, normal, axis_u, axis_v, origin);
    const auto to_plane = [&](const std::size_t knife_vertex) -> GEO::vec2f {
        const GEO::vec3f p = m_knife_vertices[knife_vertex].position;
        return GEO::vec2f{GEO::dot(p - origin, axis_u), GEO::dot(p - origin, axis_v)};
    };

    m_work.clear();
    m_work.emplace_back(a, b);
    while (!m_work.empty()) {
        const std::pair<std::size_t, std::size_t> piece = m_work.back();
        m_work.pop_back();
        const std::size_t p = piece.first;
        const std::size_t q = piece.second;
        if (p == q) {
            continue;
        }
        bool duplicate = false;
        for (const Knife_edge& edge : m_knife_edges) {
            if ((edge.facet == facet) && (((edge.a == p) && (edge.b == q)) || ((edge.a == q) && (edge.b == p)))) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        // A crossing with an earlier cut edge of the facet splits both.
        bool split = false;
        const std::size_t edge_count = m_knife_edges.size();
        for (std::size_t i = 0; i < edge_count; ++i) {
            const Knife_edge edge = m_knife_edges[i];
            if ((edge.facet != facet) || (edge.a == p) || (edge.a == q) || (edge.b == p) || (edge.b == q)) {
                continue;
            }
            float u = 0.0f;
            float v = 0.0f;
            if (!intersect_segments(to_plane(p), to_plane(q), to_plane(edge.a), to_plane(edge.b), u, v)) {
                continue;
            }
            const GEO::vec3f edge_a_position = m_knife_vertices[edge.a].position;
            const GEO::vec3f edge_b_position = m_knife_vertices[edge.b].position;
            Knife_vertex crossing;
            crossing.kind     = Knife_snap::facet;
            crossing.facet    = facet;
            crossing.position = edge_a_position + (v * (edge_b_position - edge_a_position));
            const std::size_t x = find_or_add_knife_vertex(crossing);
            if ((x == edge.a) || (x == edge.b) || (x == p) || (x == q)) {
                continue; // the crossing merged into an end point
            }
            m_knife_edges[i].b = x;
            m_knife_edges.push_back(Knife_edge{.a = x, .b = edge.b, .facet = facet});
            m_work.emplace_back(p, x);
            m_work.emplace_back(x, q);
            split = true;
            break;
        }
        if (!split) {
            m_knife_edges.push_back(Knife_edge{.a = p, .b = q, .facet = facet});
        }
    }
}

void Knife_cut::process_segment(const Knife_point& from, const Knife_point& to)
{
    Knife_vertex element_0;
    Knife_vertex element_1;
    if (!make_element(from, element_0) || !make_element(to, element_1)) {
        return;
    }
    GEO::vec2f s0;
    GEO::vec2f s1;
    if (!project(element_0.position, s0) || !project(element_1.position, s1)) {
        log_operation->warn("Knife_cut: a segment end is behind the eye; segment ignored");
        return;
    }
    const GEO::vec2f d              = s1 - s0;
    const float      length_squared = GEO::dot(d, d);
    if (length_squared < 1e-12f) {
        return;
    }
    const GEO::vec3f p0      = element_0.position;
    const GEO::vec3f segment = element_1.position - p0;
    GEO::vec3f normal = GEO::cross(segment, m_view.perspective ? (m_view.eye_in_mesh - p0) : m_view.view_direction_in_mesh);
    const float normal_length = GEO::length(normal);
    if (normal_length < 1e-20f) {
        return; // the segment runs along the view ray
    }
    normal = normal / normal_length;
    const auto plane_distance = [&](const GEO::vec3f& p) -> float {
        return GEO::dot(normal, p - p0);
    };
    const auto screen_parameter = [&](const GEO::vec2f& q) -> float {
        return GEO::dot(q - s0, d) / length_squared;
    };

    m_hits.clear();
    m_hits.push_back(Knife_hit{.element = element_0, .s = 0.0f, .depth = get_depth(element_0.position), .endpoint = true});
    m_hits.push_back(Knife_hit{.element = element_1, .s = 1.0f, .depth = get_depth(element_1.position), .endpoint = true});

    // Vertex hits. A marked vertex is a hit (or a segment end, or next to
    // one); the edges at a marked vertex yield no edge hit.
    const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
    m_vertex_marks.assign(vertex_slot_count, 0);
    if (element_0.kind == Knife_snap::vertex) {
        m_vertex_marks[element_0.vertex] = 1;
    }
    if (element_1.kind == Knife_snap::vertex) {
        m_vertex_marks[element_1.vertex] = 1;
    }
    const float vertex_tolerance = m_options.vertex_tolerance_px;
    for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
        if ((m_vertex_marks[vertex] != 0) || !m_edit_mesh.is_vertex_alive(vertex) || m_edit_mesh.get_vertex_facets(vertex).empty()) {
            continue;
        }
        const GEO::vec3f p = m_edit_mesh.get_position(vertex);
        GEO::vec2f q;
        if (!project(p, q)) {
            continue;
        }
        const float s = screen_parameter(q);
        if ((s < 0.0f) || (s > 1.0f)) {
            continue;
        }
        if (GEO::length(q - (s0 + (s * d))) > vertex_tolerance) {
            continue;
        }
        m_vertex_marks[vertex] = 1;
        if ((GEO::length(q - s0) <= vertex_tolerance) || (GEO::length(q - s1) <= vertex_tolerance)) {
            continue; // the segment end wins
        }
        Knife_vertex element;
        element.kind     = Knife_snap::vertex;
        element.vertex   = vertex;
        element.position = p;
        m_hits.push_back(Knife_hit{.element = element, .s = s, .depth = get_depth(p), .endpoint = false});
    }

    // Candidate facets: corners on both sides of the cut plane.
    const GEO::index_t facet_slot_count = m_edit_mesh.get_facet_slot_count();
    m_candidate_facets.clear();
    for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
        if (!m_edit_mesh.is_facet_alive(facet)) {
            continue;
        }
        float min_distance = std::numeric_limits<float>::max();
        float max_distance = std::numeric_limits<float>::lowest();
        for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
            const float distance = plane_distance(m_edit_mesh.get_position(corner.vertex));
            min_distance = std::min(min_distance, distance);
            max_distance = std::max(max_distance, distance);
        }
        if ((min_distance < 0.0f) && (max_distance > 0.0f)) {
            m_candidate_facets.push_back(facet);
        }
    }

    // Edge hits: the edges of the candidate facets crossing the cut plane
    // within the screen segment.
    const float edge_tolerance = m_options.edge_tolerance_px;
    m_edge_marks.assign(m_edit_mesh.get_edge_slot_count(), 0);
    for (const GEO::index_t facet : m_candidate_facets) {
        const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
        const std::size_t n = corners.size();
        for (std::size_t i = 0; i < n; ++i) {
            const GEO::index_t a    = corners[i].vertex;
            const GEO::index_t b    = corners[(i + 1) % n].vertex;
            const GEO::index_t edge = m_edit_mesh.find_edge(a, b);
            if ((edge == GEO::NO_INDEX) || (m_edge_marks[edge] != 0)) {
                continue;
            }
            m_edge_marks[edge] = 1;
            if ((m_vertex_marks[a] != 0) || (m_vertex_marks[b] != 0)) {
                continue;
            }
            const GEO::vec3f pa = m_edit_mesh.get_position(a);
            const GEO::vec3f pb = m_edit_mesh.get_position(b);
            const float      da = plane_distance(pa);
            const float      db = plane_distance(pb);
            if (((da <= 0.0f) && (db <= 0.0f)) || ((da >= 0.0f) && (db >= 0.0f))) {
                continue;
            }
            const float t_ab = da / (da - db);
            if ((t_ab <= c_parameter_epsilon) || (t_ab >= (1.0f - c_parameter_epsilon))) {
                continue;
            }
            const GEO::vec3f p = pa + (t_ab * (pb - pa));
            GEO::vec2f q;
            if (!project(p, q)) {
                continue;
            }
            const float s = screen_parameter(q);
            if ((s < 0.0f) || (s > 1.0f)) {
                continue;
            }
            if ((GEO::length(q - s0) <= edge_tolerance) || (GEO::length(q - s1) <= edge_tolerance)) {
                continue;
            }
            Knife_vertex element;
            element.kind     = Knife_snap::edge;
            element.edge_v0  = std::min(a, b);
            element.edge_v1  = std::max(a, b);
            element.t        = (a < b) ? t_ab : (1.0f - t_ab);
            element.position = p;
            const auto is_end_on_edge = [&element](const Knife_vertex& end) -> bool {
                return (end.kind == Knife_snap::edge) && (end.edge_v0 == element.edge_v0) && (end.edge_v1 == element.edge_v1);
            };
            if (is_end_on_edge(element_0) || is_end_on_edge(element_1)) {
                continue;
            }
            m_hits.push_back(Knife_hit{.element = element, .s = s, .depth = get_depth(p), .endpoint = false});
        }
    }

    // Sort along the segment, then by depth; merge hits naming the same
    // element (a segment end absorbs its duplicates).
    std::stable_sort(m_hits.begin(), m_hits.end(), [](const Knife_hit& first, const Knife_hit& second) {
        if (first.s != second.s) {
            return first.s < second.s;
        }
        return first.depth < second.depth;
    });
    m_merged_hits.clear();
    for (const Knife_hit& hit : m_hits) {
        bool merged = false;
        for (Knife_hit& kept : m_merged_hits) {
            if (same_element(kept.element, hit.element)) {
                kept.endpoint = kept.endpoint || hit.endpoint;
                merged = true;
                break;
            }
        }
        if (!merged) {
            m_merged_hits.push_back(hit);
        }
    }

    // Occlusion: the segment ends were picked on the visible surface.
    if (!m_options.cut_through) {
        std::size_t kept_count = 0;
        for (std::size_t i = 0; i < m_merged_hits.size(); ++i) {
            if (m_merged_hits[i].endpoint || !is_occluded(m_merged_hits[i].element)) {
                m_merged_hits[kept_count++] = m_merged_hits[i];
            }
        }
        m_merged_hits.resize(kept_count);
    }

    for (Knife_hit& hit : m_merged_hits) {
        hit.knife_vertex = find_or_add_knife_vertex(hit.element);
    }

    // The facets the hits belong to.
    m_touched_facets.clear();
    const auto touch = [this](const GEO::index_t facet) {
        if (std::find(m_touched_facets.begin(), m_touched_facets.end(), facet) == m_touched_facets.end()) {
            m_touched_facets.push_back(facet);
        }
    };
    for (const Knife_hit& hit : m_merged_hits) {
        switch (hit.element.kind) {
            case Knife_snap::vertex: {
                for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(hit.element.vertex)) {
                    touch(facet);
                }
                break;
            }
            case Knife_snap::edge: {
                const GEO::index_t edge = m_edit_mesh.find_edge(hit.element.edge_v0, hit.element.edge_v1);
                if (edge != GEO::NO_INDEX) {
                    for (const GEO::index_t facet : m_edit_mesh.get_edge(edge).facets) {
                        touch(facet);
                    }
                }
                break;
            }
            case Knife_snap::facet: {
                touch(hit.element.facet);
                break;
            }
            default: {
                break;
            }
        }
    }
    std::sort(m_touched_facets.begin(), m_touched_facets.end());

    // Per facet: consecutive hits of the facet become its cut edges.
    for (const GEO::index_t facet : m_touched_facets) {
        m_facet_hits.clear();
        for (std::size_t i = 0; i < m_merged_hits.size(); ++i) {
            if (belongs_to(m_merged_hits[i].element, facet)) {
                m_facet_hits.push_back(i);
            }
        }
        for (std::size_t k = 0; (k + 1) < m_facet_hits.size(); ++k) {
            const Knife_hit& first = m_merged_hits[m_facet_hits[k]];
            const Knife_hit& second = m_merged_hits[m_facet_hits[k + 1]];
            if (first.knife_vertex == second.knife_vertex) {
                continue;
            }
            if (on_common_edge(first.element, second.element)) {
                continue;
            }
            const GEO::vec3f midpoint = 0.5f * (first.element.position + second.element.position);
            if (!is_inside_facet(facet, midpoint)) {
                continue; // concave facet: the chord leaves the facet
            }
            add_cut_edge(first.knife_vertex, second.knife_vertex, facet);
        }
    }
}

void Knife_cut::finish(Knife_result* const result, Component_remap* const remap)
{
    if (m_finished) {
        log_operation->error("Knife_cut::finish(): already finished");
        return;
    }
    m_finished = true;

    if (m_options.close_polyline && (m_points.size() >= 3)) {
        process_segment(m_points.back(), m_points.front());
    }

    const std::size_t knife_vertex_count = m_knife_vertices.size();
    m_vertex_marks.assign(knife_vertex_count, 0); // used by a cut edge
    for (const Knife_edge& edge : m_knife_edges) {
        m_vertex_marks[edge.a] = 1;
        m_vertex_marks[edge.b] = 1;
    }

    // Facet points: new vertices, provenance by mean value coordinates of
    // the facet's corners (computed before any edge split changes them).
    std::vector<Edit_source> sources;
    for (std::size_t i = 0; i < knife_vertex_count; ++i) {
        Knife_vertex& knife_vertex = m_knife_vertices[i];
        if (m_vertex_marks[i] == 0) {
            continue;
        }
        if (knife_vertex.kind == Knife_snap::vertex) {
            knife_vertex.scratch_vertex = knife_vertex.vertex;
            continue;
        }
        if (knife_vertex.kind != Knife_snap::facet) {
            continue;
        }
        m_polygon.clear();
        const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(knife_vertex.facet);
        for (const Edit_corner& corner : corners) {
            m_polygon.push_back(m_edit_mesh.get_position(corner.vertex));
        }
        const GEO::vec3f normal = compute_newell_normal(m_polygon);
        compute_mean_value_weights(m_polygon, normal, knife_vertex.position, m_weights);
        sources.clear();
        for (std::size_t c = 0; c < corners.size(); ++c) {
            accumulate_sources(sources, m_edit_mesh.get_vertex(corners[c].vertex).sources, m_weights[c]);
        }
        knife_vertex.scratch_vertex = m_edit_mesh.add_vertex(knife_vertex.position, sources);
    }

    // Edge points: split each original edge at its points in parameter order.
    m_order.clear();
    for (std::size_t i = 0; i < knife_vertex_count; ++i) {
        if ((m_vertex_marks[i] != 0) && (m_knife_vertices[i].kind == Knife_snap::edge)) {
            m_order.push_back(i);
        }
    }
    std::sort(m_order.begin(), m_order.end(), [this](const std::size_t first, const std::size_t second) {
        const Knife_vertex& l = m_knife_vertices[first];
        const Knife_vertex& r = m_knife_vertices[second];
        if (l.edge_v0 != r.edge_v0) {
            return l.edge_v0 < r.edge_v0;
        }
        if (l.edge_v1 != r.edge_v1) {
            return l.edge_v1 < r.edge_v1;
        }
        return l.t < r.t;
    });
    class Edge_run
    {
    public:
        GEO::index_t              v0;
        GEO::index_t              v1;
        std::vector<GEO::index_t> vertices; // scratch vertices from v0 toward v1
    };
    std::vector<Edge_run> runs;
    for (std::size_t k = 0; k < m_order.size(); ) {
        const GEO::index_t v0 = m_knife_vertices[m_order[k]].edge_v0;
        const GEO::index_t v1 = m_knife_vertices[m_order[k]].edge_v1;
        Edge_run& run = runs.emplace_back(Edge_run{.v0 = v0, .v1 = v1, .vertices = {}});
        GEO::index_t from          = v0;
        float        t_from        = 0.0f;
        GEO::index_t edge          = m_edit_mesh.find_edge(v0, v1);
        GEO::index_t previous      = GEO::NO_INDEX;
        float        previous_t    = 0.0f;
        for (; (k < m_order.size()) && (m_knife_vertices[m_order[k]].edge_v0 == v0) && (m_knife_vertices[m_order[k]].edge_v1 == v1); ++k) {
            Knife_vertex& knife_vertex = m_knife_vertices[m_order[k]];
            if ((previous != GEO::NO_INDEX) && ((knife_vertex.t - previous_t) < c_parameter_epsilon)) {
                knife_vertex.scratch_vertex = previous;
                continue;
            }
            // The remaining edge runs from `from` (at t_from) to v1.
            const float local = (knife_vertex.t - t_from) / (1.0f - t_from);
            const float t     = (m_edit_mesh.get_edge(edge).vertices[0] == from) ? local : (1.0f - local);
            const GEO::index_t vertex = m_edit_mesh.split_edge(edge, t);
            knife_vertex.scratch_vertex = vertex;
            run.vertices.push_back(vertex);
            from       = vertex;
            t_from     = knife_vertex.t;
            previous   = vertex;
            previous_t = knife_vertex.t;
            edge       = m_edit_mesh.find_edge(vertex, v1);
        }
    }

    // Each cut facet along its edge net.
    m_touched_facets.clear();
    for (const Knife_edge& edge : m_knife_edges) {
        if (std::find(m_touched_facets.begin(), m_touched_facets.end(), edge.facet) == m_touched_facets.end()) {
            m_touched_facets.push_back(edge.facet);
        }
    }
    std::sort(m_touched_facets.begin(), m_touched_facets.end());
    for (const GEO::index_t facet : m_touched_facets) {
        m_net_edges.clear();
        for (const Knife_edge& edge : m_knife_edges) {
            if (edge.facet != facet) {
                continue;
            }
            const GEO::index_t a = m_knife_vertices[edge.a].scratch_vertex;
            const GEO::index_t b = m_knife_vertices[edge.b].scratch_vertex;
            if ((a != b) && (a != GEO::NO_INDEX) && (b != GEO::NO_INDEX)) {
                m_net_edges.emplace_back(a, b);
            }
        }
        const Edgenet_result net_result = m_edit_mesh.split_facet_edgenet(facet, m_net_edges, m_out_facets);
        if (net_result == Edgenet_result::invalid_edges) {
            log_operation->warn("Knife_cut::finish(): the cut edges of facet {} do not form a valid edge net; the facet is left unsplit", facet);
        }
    }

    // A facet point left without edges goes; an edge point left without a
    // cut edge is collapsed back into its edge.
    for (std::size_t i = 0; i < knife_vertex_count; ++i) {
        const Knife_vertex& knife_vertex = m_knife_vertices[i];
        const GEO::index_t  vertex       = knife_vertex.scratch_vertex;
        if ((m_vertex_marks[i] == 0) || (vertex == GEO::NO_INDEX) || !m_edit_mesh.is_vertex_alive(vertex)) {
            continue;
        }
        if ((knife_vertex.kind == Knife_snap::facet) && m_edit_mesh.get_vertex_edges(vertex).empty()) {
            const GEO::index_t elements[1]{vertex};
            m_edit_mesh.delete_elements(elements, Delete_context::vertices);
        } else if ((knife_vertex.kind == Knife_snap::edge) && (m_edit_mesh.get_vertex_edges(vertex).size() == 2)) {
            static_cast<void>(m_edit_mesh.collapse_vertex(vertex));
        }
    }

    emit();

    // The remap carries a selected source edge to its split halves through
    // the edge's new vertices, stored from the second (larger) endpoint
    // toward the first (see get_src_edge_new_vertex()).
    for (const Edge_run& run : runs) {
        std::vector<GEO::index_t> dst_vertices;
        for (std::vector<GEO::index_t>::const_reverse_iterator i = run.vertices.rbegin(); i != run.vertices.rend(); ++i) {
            const GEO::index_t dst_vertex = get_emitted_vertex(*i);
            if (dst_vertex != GEO::NO_INDEX) {
                dst_vertices.push_back(dst_vertex);
            }
        }
        if (!dst_vertices.empty()) {
            m_src_edge_to_dst_vertex[std::make_pair(run.v0, run.v1)] = std::move(dst_vertices);
        }
    }

    if (result != nullptr) {
        result->cut_vertices.clear();
        result->cut_edges.clear();
        for (std::size_t i = 0; i < knife_vertex_count; ++i) {
            const Knife_vertex& knife_vertex = m_knife_vertices[i];
            if ((m_vertex_marks[i] == 0) || (knife_vertex.kind == Knife_snap::vertex) || (knife_vertex.scratch_vertex == GEO::NO_INDEX)) {
                continue;
            }
            const GEO::index_t dst_vertex = get_emitted_vertex(knife_vertex.scratch_vertex);
            if (dst_vertex != GEO::NO_INDEX) {
                result->cut_vertices.push_back(dst_vertex);
            }
        }
        for (const Knife_edge& edge : m_knife_edges) {
            const GEO::index_t a = m_knife_vertices[edge.a].scratch_vertex;
            const GEO::index_t b = m_knife_vertices[edge.b].scratch_vertex;
            if ((a == GEO::NO_INDEX) || (b == GEO::NO_INDEX) || (a == b)) {
                continue;
            }
            const GEO::index_t scratch_edge = m_edit_mesh.find_edge(a, b);
            if ((scratch_edge == GEO::NO_INDEX) || (m_edit_mesh.get_edge_facet_count(scratch_edge) == 0)) {
                continue;
            }
            const GEO::index_t dst_a = get_emitted_vertex(a);
            const GEO::index_t dst_b = get_emitted_vertex(b);
            if ((dst_a == GEO::NO_INDEX) || (dst_b == GEO::NO_INDEX)) {
                continue;
            }
            result->cut_edges.emplace_back(std::min(dst_a, dst_b), std::max(dst_a, dst_b));
        }
        std::sort(result->cut_vertices.begin(), result->cut_vertices.end());
        result->cut_vertices.erase(std::unique(result->cut_vertices.begin(), result->cut_vertices.end()), result->cut_vertices.end());
        std::sort(result->cut_edges.begin(), result->cut_edges.end());
        result->cut_edges.erase(std::unique(result->cut_edges.begin(), result->cut_edges.end()), result->cut_edges.end());
    }

    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        remap_component_selection(*remap->source, *remap->destination);
    }
}

void knife_cut(
    const Geometry&                    source,
    Geometry&                          destination,
    const Knife_view&                  view,
    const std::span<const Knife_point> points,
    const Knife_options&               options,
    Knife_result* const                result,
    Component_remap* const             remap
)
{
    Knife_cut operation{source, destination, view, options};
    for (const Knife_point& point : points) {
        operation.add_point(point);
    }
    operation.finish(result, remap);
}

} // namespace erhe::geometry::operation
