#include "erhe_geometry/operation/bevel_edges.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

namespace {

// The width offset divides by sin(phi / 2); a sine below this (two facets
// folded almost onto each other) is clamped to it.
constexpr float c_min_half_angle_sine = 0.1f;

// Lines closer to parallel than this (squared sine of their angle) have no
// meet.
constexpr float c_parallel_epsilon = 1e-10f;

// Segments are clamped to this.
constexpr int c_max_segments = 1000;

// A profile at or above this is the square corner (infinite exponent).
constexpr float c_square_profile = 0.999f;

// Dense angle steps of the unit superellipse for its arc length.
constexpr int c_profile_arc_steps = 1024;

using Edge_key = std::pair<GEO::index_t, GEO::index_t>;

[[nodiscard]] auto make_key(const GEO::index_t a, const GEO::index_t b) -> Edge_key
{
    return (a < b) ? Edge_key{a, b} : Edge_key{b, a};
}

[[nodiscard]] auto safe_normalize(const GEO::vec3f& v, const GEO::vec3f& fallback) -> GEO::vec3f
{
    const float length = GEO::length(v);
    return (length > 1e-12f) ? (v / length) : fallback;
}

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

// The point of the unit superellipse |x|^r + |y|^r = 1 at angle theta in
// [0, pi / 2] (on the ray (cos theta, sin theta)); square: the max norm
// (infinite r).
[[nodiscard]] auto superellipse_point(const float theta, const float exponent, const bool square) -> GEO::vec2f
{
    const float c = std::max(0.0f, std::cos(theta));
    const float s = std::max(0.0f, std::sin(theta));
    const float m = std::max(c, s);
    float norm = m;
    if (!square) {
        norm = m * std::pow(std::pow(c / m, exponent) + std::pow(s / m, exponent), 1.0f / exponent);
    }
    return GEO::vec2f{c / norm, s / norm};
}

// The segments + 1 unit profile samples (x_k, y_k), k = 0 .. segments, from
// (1, 0) to (0, 1), spaced evenly by arc length and symmetric
// ((x_k, y_k) = (y_n-k, x_n-k)); see the header, "Segments and profile".
void compute_profile_samples(const int segments, const float profile, std::vector<GEO::vec2f>& out_samples)
{
    const std::size_t n = static_cast<std::size_t>(segments);
    out_samples.assign(n + 1, GEO::vec2f{0.0f, 0.0f});
    out_samples.front() = GEO::vec2f{1.0f, 0.0f};
    out_samples.back()  = GEO::vec2f{0.0f, 1.0f};
    if (n < 2) {
        return;
    }
    const bool  square   = (profile >= c_square_profile);
    const float exponent = square ? 0.0f : (1.0f / (1.0f - profile));
    const float quarter  = 0.5f * std::numbers::pi_v<float>;

    // Cumulative arc length over dense angle steps.
    std::vector<float> lengths(c_profile_arc_steps + 1, 0.0f);
    GEO::vec2f previous = superellipse_point(0.0f, exponent, square);
    for (int i = 1; i <= c_profile_arc_steps; ++i) {
        const float      theta = quarter * (static_cast<float>(i) / static_cast<float>(c_profile_arc_steps));
        const GEO::vec2f point = superellipse_point(theta, exponent, square);
        lengths[i] = lengths[i - 1] + GEO::length(point - previous);
        previous = point;
    }
    const float total = lengths.back();
    for (std::size_t k = 1; (2 * k) <= n; ++k) {
        float theta = 0.5f * quarter; // the middle sample of an even count
        if ((2 * k) < n) {
            const float target = total * (static_cast<float>(k) / static_cast<float>(n));
            int i = 0;
            while ((i < (c_profile_arc_steps - 1)) && (lengths[i + 1] < target)) {
                ++i;
            }
            const float span     = lengths[i + 1] - lengths[i];
            const float fraction = (span > 0.0f) ? std::clamp((target - lengths[i]) / span, 0.0f, 1.0f) : 0.0f;
            theta = quarter * ((static_cast<float>(i) + fraction) / static_cast<float>(c_profile_arc_steps));
        }
        const GEO::vec2f point = superellipse_point(theta, exponent, square);
        out_samples[k]     = point;
        out_samples[n - k] = GEO::vec2f{point.y, point.x};
    }
}

// The parameters (s, t) of the closest points a + s * u and b + t * v of two
// lines; nullopt when the lines are parallel.
[[nodiscard]] auto closest_parameters(
    const GEO::vec3f& a,
    const GEO::vec3f& u,
    const GEO::vec3f& b,
    const GEO::vec3f& v
) -> std::optional<std::pair<float, float>>
{
    const GEO::vec3f w0    = a - b;
    const float      uu    = GEO::dot(u, u);
    const float      uv    = GEO::dot(u, v);
    const float      vv    = GEO::dot(v, v);
    const float      uw    = GEO::dot(u, w0);
    const float      vw    = GEO::dot(v, w0);
    const float      denom = (uu * vv) - (uv * uv);
    if (denom <= (c_parallel_epsilon * uu * vv)) {
        return std::nullopt;
    }
    const float s = ((uv * vw) - (vv * uw)) / denom;
    const float t = ((uu * vw) - (uv * uw)) / denom;
    return std::pair<float, float>{s, t};
}

// An offset line relative to its bevel vertex, per unit amount: point +
// s * direction.
class Offset_line
{
public:
    GEO::vec3f point    {0.0f, 0.0f, 0.0f};
    GEO::vec3f direction{0.0f, 0.0f, 0.0f};
};

// The midpoint of the closest approach of two offset lines (their meet when
// they intersect); the mean of their points when they are parallel.
[[nodiscard]] auto meet(const Offset_line& line_a, const Offset_line& line_b) -> GEO::vec3f
{
    const std::optional<std::pair<float, float>> parameters = closest_parameters(line_a.point, line_a.direction, line_b.point, line_b.direction);
    if (!parameters.has_value()) {
        return 0.5f * (line_a.point + line_b.point);
    }
    const GEO::vec3f on_a = line_a.point + (parameters->first  * line_a.direction);
    const GEO::vec3f on_b = line_b.point + (parameters->second * line_b.direction);
    return 0.5f * (on_a + on_b);
}

// The parameter along the unit axis (a ring edge's direction from its bevel
// vertex) of the axis point closest to the offset line; nullopt when parallel.
[[nodiscard]] auto axis_parameter(const GEO::vec3f& axis, const Offset_line& line) -> std::optional<float>
{
    const std::optional<std::pair<float, float>> parameters = closest_parameters(GEO::vec3f{0.0f, 0.0f, 0.0f}, axis, line.point, line.direction);
    if (!parameters.has_value()) {
        return std::nullopt;
    }
    return parameters->first;
}

// One facet around a bevel vertex: the facet order there is
// prev_vertex, vertex, next_vertex.
class Wedge
{
public:
    GEO::index_t facet      {GEO::NO_INDEX};
    GEO::index_t next_vertex{GEO::NO_INDEX}; // far end of the ring edge before the wedge (e_j)
    GEO::index_t prev_vertex{GEO::NO_INDEX}; // far end of the ring edge after the wedge (e_j+1)
};

// The fan of a bevel vertex, counter-clockwise: wedge j lies between ring
// edges j and j + 1. A closed ring has as many edges as wedges (edge m is
// edge 0), an open one one more.
class Ring
{
public:
    std::vector<Wedge>        wedges;
    std::vector<GEO::index_t> edge_far_vertices;
    bool                      closed{false};
};

// A new vertex replacing a bevel vertex.
class Boundary_vertex
{
public:
    GEO::index_t bevel_vertex   {GEO::NO_INDEX};
    GEO::vec3f   direction      {0.0f, 0.0f, 0.0f}; // per unit amount, from the bevel vertex
    GEO::index_t edge_far_vertex{GEO::NO_INDEX};    // the far end of the ring edge it lies on (NO_INDEX: none)
    GEO::index_t scratch_vertex {GEO::NO_INDEX};
};

// A facet with a corner at a bevel vertex, as it was before the bevel.
class Facet_record
{
public:
    std::vector<Edit_corner> corners;
    std::vector<GEO::vec3f>  positions;
    GEO::vec3f               normal{0.0f, 0.0f, 1.0f};
    GEO::index_t             source_facet{GEO::NO_INDEX};
};

class Bevel_edges : public Edit_mesh_operation
{
public:
    Bevel_edges(
        const Geometry&                                        source,
        Geometry&                                              destination,
        const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
        const Bevel_edges_options&                             options
    )
        : Edit_mesh_operation{source, destination}
        , m_options          {options}
    {
        m_options.segments = std::clamp(m_options.segments, 1, c_max_segments);
        m_options.profile  = std::clamp(m_options.profile, 0.0f, 1.0f);
        compute_profile_samples(m_options.segments, m_options.profile, m_profile_samples);
        const GEO::index_t vertex_count = m_edit_mesh.get_vertex_slot_count();
        for (const std::pair<GEO::index_t, GEO::index_t>& pair : selected_edges) {
            if ((pair.first >= vertex_count) || (pair.second >= vertex_count) || (pair.first == pair.second)) {
                continue;
            }
            const GEO::index_t edge = m_edit_mesh.find_edge(pair.first, pair.second);
            if (edge == GEO::NO_INDEX) {
                continue;
            }
            if (!is_bevelable(edge)) {
                log_operation->warn("bevel_edges: edge ({}, {}) is a boundary or non-manifold edge; skipped", pair.first, pair.second);
                continue;
            }
            m_beveled.insert(make_key(pair.first, pair.second));
        }
    }

    void build(Bevel_edges_result* result)
    {
        if (!m_beveled.empty()) {
            build_rings();
        }
        if (!m_beveled.empty()) {
            compute_offsets();
            for (const std::pair<const GEO::index_t, Ring>& entry : m_rings) {
                place_boundary_vertices(entry.first, entry.second);
            }
            rebuild();
        }

        // Scratch facet -> destination facet: emit() writes the live facets in
        // handle order.
        std::vector<GEO::index_t> facet_to_dst(m_edit_mesh.get_facet_slot_count(), GEO::NO_INDEX);
        {
            GEO::index_t next = 0;
            for (GEO::index_t facet = 0; facet < m_edit_mesh.get_facet_slot_count(); ++facet) {
                if (m_edit_mesh.is_facet_alive(facet)) {
                    facet_to_dst[facet] = next++;
                }
            }
        }

        emit();

        m_dst_edge_facets.clear();
        for (const GEO::index_t facet : m_edge_facets) {
            m_dst_edge_facets.push_back(facet_to_dst[facet]);
        }
        std::sort(m_dst_edge_facets.begin(), m_dst_edge_facets.end());
        if (result == nullptr) {
            return;
        }
        result->boundary_vertices  .clear();
        result->boundary_directions.clear();
        result->edge_facets        .clear();
        result->vertex_facets      .clear();
        // m_boundary is in scratch vertex order; emit() keeps the order.
        for (const Boundary_vertex& boundary : m_boundary) {
            const GEO::index_t dst_vertex = get_emitted_vertex(boundary.scratch_vertex);
            if (dst_vertex == GEO::NO_INDEX) {
                continue;
            }
            result->boundary_vertices  .push_back(dst_vertex);
            result->boundary_directions.push_back(boundary.direction);
        }
        result->edge_facets   = m_dst_edge_facets;
        result->beveled_edges = m_edge_facet_sides.size();
        for (const GEO::index_t facet : m_vertex_facets) {
            result->vertex_facets.push_back(facet_to_dst[facet]);
        }
        std::sort(result->vertex_facets.begin(), result->vertex_facets.end());
    }

    // The edge facets with their edges and vertices.
    void remap(Geometry_component_selection& dst) const
    {
        dst.vertices.clear();
        dst.facets  .clear();
        dst.edges   .clear();
        const GEO::Mesh& mesh = destination.get_mesh();
        for (const GEO::index_t facet : m_dst_edge_facets) {
            if (facet >= mesh.facets.nb()) {
                continue;
            }
            dst.facets.insert(facet);
            const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
            for (GEO::index_t local = 0; local < corner_count; ++local) {
                const GEO::index_t a = mesh.facets.vertex(facet, local);
                const GEO::index_t b = mesh.facets.vertex(facet, (local + 1) % corner_count);
                dst.vertices.insert(a);
                dst.edges.insert(make_key(a, b));
            }
        }
    }

private:
    [[nodiscard]] auto is_bevelable(const GEO::index_t edge) const -> bool
    {
        const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
        if ((edit_edge.facets.size() != 2) || (edit_edge.facets[0] == edit_edge.facets[1])) {
            return false;
        }
        const GEO::index_t a = edit_edge.vertices[0];
        const GEO::index_t b = edit_edge.vertices[1];
        return (is_directed_in_facet(edit_edge.facets[0], a, b) != is_directed_in_facet(edit_edge.facets[1], a, b));
    }

    // True when the facet has b right after a.
    [[nodiscard]] auto is_directed_in_facet(const GEO::index_t facet, const GEO::index_t a, const GEO::index_t b) const -> bool
    {
        const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
        const std::size_t                  n       = corners.size();
        for (std::size_t i = 0; i < n; ++i) {
            if ((corners[i].vertex == a) && (corners[(i + 1) % n].vertex == b)) {
                return true;
            }
        }
        return false;
    }

    // The fan of the vertex (see the header); nullopt when it is not one fan.
    [[nodiscard]] auto make_ring(const GEO::index_t vertex) const -> std::optional<Ring>
    {
        std::vector<Wedge> wedges;
        for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(vertex)) {
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
            const std::size_t                  n       = corners.size();
            std::size_t                        count   = 0;
            std::size_t                        corner  = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (corners[i].vertex == vertex) {
                    ++count;
                    corner = i;
                }
            }
            if (count != 1) {
                return std::nullopt;
            }
            for (const Wedge& wedge : wedges) {
                if (wedge.facet == facet) {
                    return std::nullopt;
                }
            }
            wedges.push_back(Wedge{
                .facet       = facet,
                .next_vertex = corners[(corner + 1) % n].vertex,
                .prev_vertex = corners[(corner + n - 1) % n].vertex
            });
        }
        if (wedges.empty()) {
            return std::nullopt;
        }
        // Wedge g follows wedge f when g.next_vertex == f.prev_vertex.
        std::map<GEO::index_t, std::size_t> by_next;
        for (std::size_t i = 0; i < wedges.size(); ++i) {
            if (!by_next.emplace(wedges[i].next_vertex, i).second) {
                return std::nullopt;
            }
        }
        std::set<GEO::index_t> prev_vertices;
        for (const Wedge& wedge : wedges) {
            if (!prev_vertices.insert(wedge.prev_vertex).second) {
                return std::nullopt;
            }
        }
        std::size_t start       = 0;
        std::size_t start_count = 0;
        for (std::size_t i = 0; i < wedges.size(); ++i) {
            if (!prev_vertices.contains(wedges[i].next_vertex)) {
                start = i;
                ++start_count;
            }
        }
        if (start_count > 1) {
            return std::nullopt;
        }
        Ring ring{};
        ring.closed = (start_count == 0);
        std::size_t current = start;
        for (std::size_t step = 0; step < wedges.size(); ++step) {
            ring.wedges.push_back(wedges[current]);
            const std::map<GEO::index_t, std::size_t>::const_iterator next = by_next.find(wedges[current].prev_vertex);
            if (next == by_next.end()) {
                break;
            }
            current = next->second;
            if (current == start) {
                break;
            }
        }
        if (ring.wedges.size() != wedges.size()) {
            return std::nullopt; // more than one fan
        }
        if (ring.closed) {
            if (ring.wedges.front().next_vertex != ring.wedges.back().prev_vertex) {
                return std::nullopt;
            }
            if (ring.wedges.size() < 3) {
                return std::nullopt;
            }
        }
        ring.edge_far_vertices.push_back(ring.wedges.front().next_vertex);
        const std::size_t edge_count = ring.closed ? (ring.wedges.size() - 1) : ring.wedges.size();
        for (std::size_t j = 0; j < edge_count; ++j) {
            ring.edge_far_vertices.push_back(ring.wedges[j].prev_vertex);
        }
        return ring;
    }

    // The rings of every bevel vertex; the beveled edges at a vertex that is
    // not one fan are dropped (ring validity does not depend on the edge set,
    // so one pass settles it).
    void build_rings()
    {
        std::set<GEO::index_t> vertices;
        for (const Edge_key& key : m_beveled) {
            vertices.insert(key.first);
            vertices.insert(key.second);
        }
        std::set<GEO::index_t> invalid;
        for (const GEO::index_t vertex : vertices) {
            std::optional<Ring> ring = make_ring(vertex);
            if (!ring.has_value()) {
                invalid.insert(vertex);
                continue;
            }
            m_rings.emplace(vertex, std::move(ring.value()));
        }
        if (invalid.empty()) {
            return;
        }
        for (std::set<Edge_key>::iterator i = m_beveled.begin(); i != m_beveled.end();) {
            if (invalid.contains(i->first) || invalid.contains(i->second)) {
                log_operation->warn("bevel_edges: edge ({}, {}) has a vertex whose facets are not one fan; skipped", i->first, i->second);
                i = m_beveled.erase(i);
            } else {
                ++i;
            }
        }
        // Drop the rings of the vertices left without a beveled edge.
        std::set<GEO::index_t> remaining;
        for (const Edge_key& key : m_beveled) {
            remaining.insert(key.first);
            remaining.insert(key.second);
        }
        for (std::map<GEO::index_t, Ring>::iterator i = m_rings.begin(); i != m_rings.end();) {
            if (!remaining.contains(i->first)) {
                i = m_rings.erase(i);
            } else {
                ++i;
            }
        }
    }

    [[nodiscard]] auto get_facet_normal(const GEO::index_t facet) -> GEO::vec3f
    {
        const std::map<GEO::index_t, GEO::vec3f>::const_iterator i = m_facet_normals.find(facet);
        if (i != m_facet_normals.end()) {
            return i->second;
        }
        std::vector<GEO::vec3f> positions;
        for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
            positions.push_back(m_edit_mesh.get_position(corner.vertex));
        }
        const GEO::vec3f normal = compute_newell_normal(positions);
        m_facet_normals.emplace(facet, normal);
        return normal;
    }

    // Unit perpendicular to edge (a, b) in the facet's plane, into the facet.
    [[nodiscard]] auto get_in_facet_tangent(const GEO::index_t facet, const GEO::index_t a, const GEO::index_t b) -> GEO::vec3f
    {
        const bool       forward = is_directed_in_facet(facet, a, b);
        const GEO::vec3f d       = forward
            ? (m_edit_mesh.get_position(b) - m_edit_mesh.get_position(a))
            : (m_edit_mesh.get_position(a) - m_edit_mesh.get_position(b));
        return safe_normalize(GEO::cross(get_facet_normal(facet), d), GEO::vec3f{0.0f, 0.0f, 0.0f});
    }

    // The offset distance per unit amount of every beveled edge.
    void compute_offsets()
    {
        for (const Edge_key& key : m_beveled) {
            float scale = 1.0f;
            if (m_options.offset_type == Bevel_offset_type::width) {
                const GEO::index_t               edge   = m_edit_mesh.find_edge(key.first, key.second);
                const std::vector<GEO::index_t>& facets = m_edit_mesh.get_edge(edge).facets;
                const GEO::vec3f t0     = get_in_facet_tangent(facets[0], key.first, key.second);
                const GEO::vec3f t1     = get_in_facet_tangent(facets[1], key.first, key.second);
                const float      cosine = std::clamp(GEO::dot(t0, t1), -1.0f, 1.0f);
                const float      sine   = std::sqrt(std::max(0.0f, 0.5f * (1.0f - cosine)));
                scale = 1.0f / (2.0f * std::max(sine, c_min_half_angle_sine));
            }
            m_offset_scale.emplace(key, scale);
        }
    }

    // The offset line of beveled edge (vertex, far_vertex) in the facet, relative to
    // vertex, per unit amount.
    [[nodiscard]] auto get_offset_line(const GEO::index_t vertex, const GEO::index_t far_vertex, const GEO::index_t facet) -> Offset_line
    {
        const float      scale   = m_offset_scale.at(make_key(vertex, far_vertex));
        const GEO::vec3f tangent = get_in_facet_tangent(facet, vertex, far_vertex);
        return Offset_line{
            .point     = scale * tangent,
            .direction = safe_normalize(m_edit_mesh.get_position(far_vertex) - m_edit_mesh.get_position(vertex), GEO::vec3f{1.0f, 0.0f, 0.0f})
        };
    }

    [[nodiscard]] auto get_axis(const GEO::index_t vertex, const GEO::index_t far_vertex) const -> GEO::vec3f
    {
        return safe_normalize(m_edit_mesh.get_position(far_vertex) - m_edit_mesh.get_position(vertex), GEO::vec3f{1.0f, 0.0f, 0.0f});
    }

    [[nodiscard]] auto is_beveled(const GEO::index_t a, const GEO::index_t b) const -> bool
    {
        return m_beveled.contains(make_key(a, b));
    }

    auto add_boundary_vertex(const GEO::index_t vertex, const GEO::vec3f& direction, const GEO::index_t edge_far_vertex) -> std::size_t
    {
        const GEO::vec3f               position = m_edit_mesh.get_position(vertex) + (m_options.amount * direction);
        const std::vector<Edit_source> sources  = m_edit_mesh.get_vertex(vertex).sources;
        const GEO::index_t             scratch  = m_edit_mesh.add_vertex(position, sources);
        m_boundary.push_back(Boundary_vertex{
            .bevel_vertex    = vertex,
            .direction       = direction,
            .edge_far_vertex = edge_far_vertex,
            .scratch_vertex  = scratch
        });
        return m_boundary.size() - 1;
    }

    // The boundary vertex on ring edge (vertex, far_vertex) where the offset line
    // comes closest to it (or, parallel, the offset line's point).
    auto add_on_edge_vertex(const GEO::index_t vertex, const GEO::index_t far_vertex, const Offset_line& line) -> std::size_t
    {
        const GEO::vec3f           axis      = get_axis(vertex, far_vertex);
        const std::optional<float> parameter = axis_parameter(axis, line);
        if (!parameter.has_value()) {
            return add_boundary_vertex(vertex, line.point, GEO::NO_INDEX);
        }
        return add_boundary_vertex(vertex, parameter.value() * axis, far_vertex);
    }

    // The boundary vertices of one bevel vertex (see the header), the chain
    // of every wedge and the vertex facet's vertices.
    void place_boundary_vertices(const GEO::index_t vertex, const Ring& ring)
    {
        const std::size_t wedge_count = ring.wedges.size();
        const std::size_t edge_count  = ring.edge_far_vertices.size();
        const auto far_of = [&ring, edge_count](const std::size_t j) -> GEO::index_t {
            return ring.edge_far_vertices[j % edge_count];
        };
        const auto wedge_facet = [&ring, wedge_count](const std::size_t j) -> GEO::index_t {
            return ring.wedges[j % wedge_count].facet;
        };
        std::vector<std::size_t> beveled;
        for (std::size_t j = 0; j < edge_count; ++j) {
            if (is_beveled(vertex, ring.edge_far_vertices[j])) {
                beveled.push_back(j);
            }
        }
        if (beveled.empty()) {
            return;
        }
        std::vector<std::vector<std::size_t>> chains(wedge_count);
        std::vector<std::size_t>              polygon;

        if (ring.closed && (beveled.size() == 1)) {
            // A single beveled edge e_i: a boundary vertex on every other ring edge.
            const std::size_t m = wedge_count;
            const std::size_t i = beveled.front();
            std::vector<std::size_t> on_edge(m, 0);
            const Offset_line right = get_offset_line(vertex, far_of(i), wedge_facet(i));
            const Offset_line left  = get_offset_line(vertex, far_of(i), wedge_facet(i + m - 1));
            on_edge[(i + 1) % m] = add_on_edge_vertex(vertex, far_of(i + 1), right);
            for (std::size_t j = i + 2; j <= i + m - 2; ++j) {
                on_edge[j % m] = add_boundary_vertex(vertex, get_axis(vertex, far_of(j)), far_of(j));
            }
            on_edge[(i + m - 1) % m] = add_on_edge_vertex(vertex, far_of(i + m - 1), left);
            chains[i % m]           = {on_edge[(i + 1) % m]};
            chains[(i + m - 1) % m] = {on_edge[(i + m - 1) % m]};
            for (std::size_t k = i + 1; k <= i + m - 2; ++k) {
                chains[k % m] = {on_edge[(k + 1) % m], on_edge[k % m]};
            }
            for (std::size_t j = i + 1; j <= i + m - 1; ++j) {
                polygon.push_back(on_edge[j % m]);
            }
        } else {
            // One boundary vertex per span of wedges between consecutive
            // beveled edges.
            const auto assign_span = [&chains, wedge_count](const std::size_t first_wedge, const std::size_t end_wedge, const std::size_t boundary) {
                for (std::size_t j = first_wedge; j < end_wedge; ++j) {
                    chains[j % wedge_count] = {boundary};
                }
            };
            if (!ring.closed) {
                // Start span: wedges 0 .. b0 - 1, the boundary vertex on e_b0-1.
                const std::size_t b0     = beveled.front();
                const Offset_line line   = get_offset_line(vertex, far_of(b0), wedge_facet(b0 - 1));
                const std::size_t vertex_index = add_on_edge_vertex(vertex, far_of(b0 - 1), line);
                assign_span(0, b0, vertex_index);
                polygon.push_back(vertex_index);
            }
            const std::size_t q          = beveled.size();
            const std::size_t span_count = ring.closed ? q : (q - 1);
            for (std::size_t s = 0; s < span_count; ++s) {
                const std::size_t start  = beveled[s];
                std::size_t       end    = beveled[(s + 1) % q];
                if (end <= start) {
                    end += wedge_count; // closed ring wrap
                }
                const Offset_line line_a = get_offset_line(vertex, far_of(start), wedge_facet(start));
                const Offset_line line_b = get_offset_line(vertex, far_of(end),   wedge_facet(end - 1));
                const std::size_t middle = end - start - 1;
                std::size_t boundary = 0;
                bool        placed   = false;
                if ((middle == 1) && m_options.loop_slide) {
                    const GEO::index_t         far_vertex     = far_of(start + 1);
                    const GEO::vec3f           axis    = get_axis(vertex, far_vertex);
                    const std::optional<float> param_a = axis_parameter(axis, line_a);
                    const std::optional<float> param_b = axis_parameter(axis, line_b);
                    if (param_a.has_value() || param_b.has_value()) {
                        const float parameter =
                            (param_a.has_value() && param_b.has_value()) ? (0.5f * (param_a.value() + param_b.value())) :
                            param_a.has_value()                          ? param_a.value()                                :
                                                                           param_b.value();
                        boundary = add_boundary_vertex(vertex, parameter * axis, far_vertex);
                        placed   = true;
                    }
                }
                if (!placed) {
                    boundary = add_boundary_vertex(vertex, meet(line_a, line_b), GEO::NO_INDEX);
                }
                assign_span(start, end, boundary);
                polygon.push_back(boundary);
            }
            if (!ring.closed) {
                // End span: wedges bq .. wedge_count - 1, the boundary vertex on e_bq+1.
                const std::size_t bq     = beveled.back();
                const Offset_line line   = get_offset_line(vertex, far_of(bq), wedge_facet(bq));
                const std::size_t vertex_index = add_on_edge_vertex(vertex, far_of(bq + 1), line);
                assign_span(bq, wedge_count, vertex_index);
                polygon.push_back(vertex_index);
            }
        }

        for (std::size_t j = 0; j < wedge_count; ++j) {
            m_chains.emplace(std::pair<GEO::index_t, GEO::index_t>{ring.wedges[j].facet, vertex}, std::move(chains[j]));
        }
        if (polygon.size() >= 3) {
            if (ring.closed && (beveled.size() >= 3)) {
                m_cutoff_vertices.insert(vertex);
            }
            m_polygons.emplace(vertex, std::move(polygon));
        }
    }

    // The profile of bevel vertex `vertex` between its boundary vertices a
    // (x = 1) and b (y = 1), created once (with segments > 1) and shared by
    // every edge facet and facet side between the two.
    void make_profile(const GEO::index_t vertex, const std::size_t a, const std::size_t b)
    {
        const std::size_t n = m_profile_samples.size() - 1;
        if (n < 2) {
            return;
        }
        const Profile_key key{vertex, std::min(a, b), std::max(a, b)};
        if (m_profiles.contains(key)) {
            return;
        }
        const GEO::vec3f direction_a = m_boundary[a].direction;
        const GEO::vec3f direction_b = m_boundary[b].direction;
        Profile profile{.first = a, .interior = {}};
        for (std::size_t k = 1; k < n; ++k) {
            const GEO::vec2f& sample = m_profile_samples[k];
            profile.interior.push_back(add_boundary_vertex(vertex, ((1.0f - sample.y) * direction_a) + ((1.0f - sample.x) * direction_b), GEO::NO_INDEX));
        }
        m_profiles.emplace(key, std::move(profile));
    }

    // The interior samples of the profile between boundary vertices a and b
    // of `vertex`, in order from a to b; false (out cleared) without one.
    auto get_profile_interior(const GEO::index_t vertex, const std::size_t a, const std::size_t b, std::vector<std::size_t>& out_interior) const -> bool
    {
        out_interior.clear();
        const std::map<Profile_key, Profile>::const_iterator i = m_profiles.find(Profile_key{vertex, std::min(a, b), std::max(a, b)});
        if (i == m_profiles.end()) {
            return false;
        }
        out_interior = i->second.interior;
        if (i->second.first != a) {
            std::reverse(out_interior.begin(), out_interior.end());
        }
        return true;
    }

    // The full profile [a, interior..., b].
    void get_profile(const GEO::index_t vertex, const std::size_t a, const std::size_t b, std::vector<std::size_t>& out_profile) const
    {
        std::vector<std::size_t> interior;
        get_profile_interior(vertex, a, b, interior);
        out_profile.clear();
        out_profile.push_back(a);
        out_profile.insert(out_profile.end(), interior.begin(), interior.end());
        out_profile.push_back(b);
    }

    // The provenance of boundary vertex `boundary` as a corner of the
    // original facet `record` whose local corner `corner` was at its bevel
    // vertex.
    void interpolate_corner(
        const Facet_record&       record,
        const std::size_t         corner,
        const Boundary_vertex&    boundary,
        std::vector<Edit_source>& out_sources
    ) const
    {
        out_sources.clear();
        const std::size_t n        = record.corners.size();
        const std::size_t next     = (corner + 1) % n;
        const std::size_t prev     = (corner + n - 1) % n;
        const GEO::vec3f  position = m_edit_mesh.get_position(boundary.scratch_vertex);
        std::size_t far_corner = n;
        if (boundary.edge_far_vertex != GEO::NO_INDEX) {
            if (record.corners[next].vertex == boundary.edge_far_vertex) {
                far_corner = next;
            } else if (record.corners[prev].vertex == boundary.edge_far_vertex) {
                far_corner = prev;
            }
        }
        if (far_corner < n) {
            const float length = GEO::length(record.positions[far_corner] - record.positions[corner]);
            const float t      = (length > 0.0f) ? std::clamp(GEO::length(position - record.positions[corner]) / length, 0.0f, 1.0f) : 0.0f;
            accumulate_sources(out_sources, record.corners[corner    ].sources, 1.0f - t);
            accumulate_sources(out_sources, record.corners[far_corner].sources, t);
            if (out_sources.empty()) {
                out_sources = record.corners[corner].sources;
            }
            return;
        }
        const GEO::vec3f& at_vertex = record.positions[corner];
        if ((position.x == at_vertex.x) && (position.y == at_vertex.y) && (position.z == at_vertex.z)) {
            out_sources = record.corners[corner].sources;
            return;
        }
        const GEO::vec3f in_plane = position - (GEO::dot(position - record.positions[0], record.normal) * record.normal);
        compute_mean_value_weights(record.positions, record.normal, in_plane, m_weights);
        for (std::size_t j = 0; j < n; ++j) {
            accumulate_sources(out_sources, record.corners[j].sources, m_weights[j]);
        }
    }

    // Rebuilds the facets around the bevel vertices, creates the edge and
    // vertex facets and deletes the bevel vertices (with the beveled edges).
    void rebuild()
    {
        // The original facets touching a bevel vertex.
        std::map<GEO::index_t, Facet_record> records;
        for (const std::pair<const GEO::index_t, Ring>& entry : m_rings) {
            for (const Wedge& wedge : entry.second.wedges) {
                if (records.contains(wedge.facet)) {
                    continue;
                }
                Facet_record record{};
                for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(wedge.facet)) {
                    record.corners.push_back(corner);
                    record.positions.push_back(m_edit_mesh.get_position(corner.vertex));
                }
                record.normal       = compute_newell_normal(record.positions);
                record.source_facet = m_edit_mesh.get_facet(wedge.facet).source_facet;
                records.emplace(wedge.facet, std::move(record));
            }
        }

        // One edge facet per beveled edge: (r, l, l', r'), r / r' at the two
        // ends in the facet traversing the edge from v to w, l / l' in the other.
        for (const Edge_key& key : m_beveled) {
            const GEO::index_t v = key.first;
            const GEO::index_t w = key.second;
            const GEO::index_t edge = m_edit_mesh.find_edge(v, w);
            if (edge == GEO::NO_INDEX) {
                continue;
            }
            const std::vector<GEO::index_t>& edge_facets = m_edit_mesh.get_edge(edge).facets;
            if (edge_facets.size() != 2) {
                continue;
            }
            const GEO::index_t facet_r = is_directed_in_facet(edge_facets[0], v, w) ? edge_facets[0] : edge_facets[1];
            const GEO::index_t facet_l = (facet_r == edge_facets[0]) ? edge_facets[1] : edge_facets[0];
            m_edge_facet_sides.push_back(Edge_facet_sides{.v = v, .w = w, .facet_r = facet_r, .facet_l = facet_l});
        }
        // The profiles at both ends of every beveled edge (segments > 1),
        // before any facet uses them.
        for (const Edge_facet_sides& sides : m_edge_facet_sides) {
            make_profile(sides.v, m_chains.at({sides.facet_r, sides.v}).back (), m_chains.at({sides.facet_l, sides.v}).front());
            make_profile(sides.w, m_chains.at({sides.facet_r, sides.w}).front(), m_chains.at({sides.facet_l, sides.w}).back ());
        }

        // The sharpness of the unbeveled ring edges the boundary vertices lie
        // on, read before the rebuild replaces them.
        std::vector<std::pair<std::size_t, float>> on_edge_sharpness;
        for (std::size_t i = 0; i < m_boundary.size(); ++i) {
            const Boundary_vertex& boundary = m_boundary[i];
            if (boundary.edge_far_vertex == GEO::NO_INDEX) {
                continue;
            }
            const GEO::index_t edge = m_edit_mesh.find_edge(boundary.bevel_vertex, boundary.edge_far_vertex);
            if (edge == GEO::NO_INDEX) {
                continue;
            }
            const std::optional<float> sharpness = m_edit_mesh.get_edge_sharpness(edge);
            if (sharpness.has_value()) {
                on_edge_sharpness.emplace_back(i, sharpness.value());
            }
        }

        // Rebuild every such facet with its bevel vertex corners replaced by
        // the chains (with the profile samples between the two ends of a
        // profile).
        std::vector<Edit_corner>  corners;
        std::vector<GEO::index_t> vertices;
        std::vector<Edit_source>  sources;
        std::vector<std::size_t>  interior;
        for (const std::pair<const GEO::index_t, Facet_record>& entry : records) {
            const GEO::index_t  facet  = entry.first;
            const Facet_record& record = entry.second;
            corners.clear();
            for (std::size_t i = 0; i < record.corners.size(); ++i) {
                const Edit_corner& corner = record.corners[i];
                const std::map<std::pair<GEO::index_t, GEO::index_t>, std::vector<std::size_t>>::const_iterator chain =
                    m_chains.find(std::pair<GEO::index_t, GEO::index_t>{facet, corner.vertex});
                if (chain == m_chains.end()) {
                    corners.push_back(corner);
                    continue;
                }
                const std::vector<std::size_t>& chain_vertices = chain->second;
                for (std::size_t c = 0; c < chain_vertices.size(); ++c) {
                    const Boundary_vertex& boundary = m_boundary[chain_vertices[c]];
                    interpolate_corner(record, i, boundary, sources);
                    corners.push_back(Edit_corner{.vertex = boundary.scratch_vertex, .sources = sources});
                    if (((c + 1) < chain_vertices.size()) && get_profile_interior(corner.vertex, chain_vertices[c], chain_vertices[c + 1], interior)) {
                        for (const std::size_t sample_index : interior) {
                            const Boundary_vertex& sample = m_boundary[sample_index];
                            interpolate_corner(record, i, sample, sources);
                            corners.push_back(Edit_corner{.vertex = sample.scratch_vertex, .sources = sources});
                        }
                    }
                }
            }
            if (corners.size() == record.corners.size()) {
                // Same corner count: the facet keeps its handle.
                vertices.clear();
                for (const Edit_corner& corner : corners) {
                    vertices.push_back(corner.vertex);
                }
                m_edit_mesh.set_facet_vertices(facet, vertices);
                for (std::size_t i = 0; i < corners.size(); ++i) {
                    if (corners[i].vertex != record.corners[i].vertex) {
                        m_edit_mesh.set_corner_sources(facet, static_cast<GEO::index_t>(i), corners[i].sources);
                    }
                }
                continue;
            }
            const GEO::index_t facets_to_delete[] = {facet};
            m_edit_mesh.delete_elements(facets_to_delete, Delete_context::only_faces);
            const GEO::index_t rebuilt = m_edit_mesh.create_facet_from_corners(corners, record.source_facet);
            if (rebuilt == GEO::NO_INDEX) {
                log_operation->warn("bevel_edges: facet {} was not rebuilt", facet);
            }
        }

        // The edge facets (the facet lists read above, before the rebuild):
        // one quad per segment between the matching samples of the profiles
        // at the two ends (one segment: the quad (r_v, l_v, l_w, r_w)).
        std::vector<std::size_t> profile_v;
        std::vector<std::size_t> profile_w;
        std::vector<Edit_source> sources_v;
        std::vector<Edit_source> sources_w;
        for (const Edge_facet_sides& sides : m_edge_facet_sides) {
            const Facet_record& record_r = records.at(sides.facet_r);
            const Facet_record& record_l = records.at(sides.facet_l);
            const auto corner_sources = [](const Facet_record& record, const GEO::index_t vertex) -> const std::vector<Edit_source>& {
                for (const Edit_corner& corner : record.corners) {
                    if (corner.vertex == vertex) {
                        return corner.sources;
                    }
                }
                return record.corners.front().sources;
            };
            get_profile(sides.v, m_chains.at({sides.facet_r, sides.v}).back (), m_chains.at({sides.facet_l, sides.v}).front(), profile_v);
            get_profile(sides.w, m_chains.at({sides.facet_r, sides.w}).front(), m_chains.at({sides.facet_l, sides.w}).back (), profile_w);
            if (profile_v.size() != profile_w.size()) {
                log_operation->warn("bevel_edges: the profiles of edge ({}, {}) differ in size; skipped", sides.v, sides.w);
                continue;
            }
            const std::size_t last = profile_v.size() - 1;
            // Sample k of the end at `vertex`: the right facet's corner there
            // blended toward the left facet's by k / last.
            const auto blend_sources = [&](const GEO::index_t vertex, const std::size_t k, std::vector<Edit_source>& out) {
                const std::vector<Edit_source>& right = corner_sources(record_r, vertex);
                const std::vector<Edit_source>& left  = corner_sources(record_l, vertex);
                if (k == 0) {
                    out = right;
                    return;
                }
                if (k == last) {
                    out = left;
                    return;
                }
                const float t = static_cast<float>(k) / static_cast<float>(last);
                out.clear();
                accumulate_sources(out, right, 1.0f - t);
                accumulate_sources(out, left,  t);
            };
            for (std::size_t j = 0; j < last; ++j) {
                corners.clear();
                blend_sources(sides.v, j,     sources_v);
                corners.push_back(Edit_corner{.vertex = m_boundary[profile_v[j]].scratch_vertex, .sources = sources_v});
                blend_sources(sides.v, j + 1, sources_v);
                corners.push_back(Edit_corner{.vertex = m_boundary[profile_v[j + 1]].scratch_vertex, .sources = sources_v});
                blend_sources(sides.w, j + 1, sources_w);
                corners.push_back(Edit_corner{.vertex = m_boundary[profile_w[j + 1]].scratch_vertex, .sources = sources_w});
                blend_sources(sides.w, j,     sources_w);
                corners.push_back(Edit_corner{.vertex = m_boundary[profile_w[j]].scratch_vertex, .sources = sources_w});
                const GEO::index_t facet = m_edit_mesh.create_facet_from_corners(corners, record_r.source_facet);
                if (facet == GEO::NO_INDEX) {
                    log_operation->warn("bevel_edges: edge facet of edge ({}, {}) was not created", sides.v, sides.w);
                    continue;
                }
                m_edge_facets.push_back(facet);
            }
        }

        // Vertex facets: the boundary vertices in ring order (with the
        // profile samples along the sides), or the cutoff patch; corners
        // averaging every original corner at the vertex.
        std::vector<std::size_t>              polygon;
        std::vector<std::vector<std::size_t>> sides;
        for (const std::pair<const GEO::index_t, std::vector<std::size_t>>& entry : m_polygons) {
            const GEO::index_t vertex = entry.first;
            const Ring&        ring   = m_rings.at(vertex);
            sources.clear();
            const float weight = 1.0f / static_cast<float>(ring.wedges.size());
            for (const Wedge& wedge : ring.wedges) {
                const Facet_record& record = records.at(wedge.facet);
                for (const Edit_corner& corner : record.corners) {
                    if (corner.vertex == vertex) {
                        accumulate_sources(sources, corner.sources, weight);
                    }
                }
            }
            const GEO::index_t source_facet = records.at(ring.wedges.front().facet).source_facet;
            const auto add_vertex_facet = [&](std::span<const std::size_t> boundary_indices) {
                corners.clear();
                for (const std::size_t boundary_index : boundary_indices) {
                    corners.push_back(Edit_corner{.vertex = m_boundary[boundary_index].scratch_vertex, .sources = sources});
                }
                const GEO::index_t facet = m_edit_mesh.create_facet_from_corners(corners, source_facet);
                if (facet == GEO::NO_INDEX) {
                    log_operation->warn("bevel_edges: vertex facet of vertex {} was not created", vertex);
                    return;
                }
                m_vertex_facets.push_back(facet);
            };
            const std::vector<std::size_t>& ring_polygon = entry.second;
            const std::size_t               k            = ring_polygon.size();
            const std::size_t               n            = m_profile_samples.size() - 1;
            bool cutoff = (n >= 2) && m_cutoff_vertices.contains(vertex);
            if (cutoff) {
                sides.resize(k);
                for (std::size_t i = 0; i < k; ++i) {
                    get_profile(vertex, ring_polygon[i], ring_polygon[(i + 1) % k], sides[i]);
                    if (sides[i].size() != (n + 1)) {
                        cutoff = false; // a side without a profile: one polygon below
                    }
                }
            }
            if (!cutoff) {
                polygon.clear();
                for (std::size_t i = 0; i < k; ++i) {
                    polygon.push_back(ring_polygon[i]);
                    if (get_profile_interior(vertex, ring_polygon[i], ring_polygon[(i + 1) % k], interior)) {
                        polygon.insert(polygon.end(), interior.begin(), interior.end());
                    }
                }
                add_vertex_facet(polygon);
                continue;
            }
            // The cutoff patch (see the header).
            const std::size_t h = n / 2;
            polygon.clear();
            for (std::size_t i = 0; i < k; ++i) {
                polygon.push_back(sides[i][h]);
                if ((n - h) != h) {
                    polygon.push_back(sides[i][n - h]);
                }
            }
            add_vertex_facet(polygon);
            for (std::size_t i = 0; i < k; ++i) {
                const std::vector<std::size_t>& side      = sides[i];
                const std::vector<std::size_t>& next_side = sides[(i + 1) % k];
                const std::size_t triangle[3] = {side[n - 1], side[n], next_side[1]};
                add_vertex_facet(triangle);
                for (std::size_t j = 1; j < h; ++j) {
                    const std::size_t quad[4] = {side[n - j - 1], side[n - j], next_side[j], next_side[j + 1]};
                    add_vertex_facet(quad);
                }
            }
        }

        // The shortened unbeveled ring edges keep their sharpness: from the
        // boundary vertex to the far vertex, or to the far vertex's boundary
        // vertex on the same edge when the far vertex is beveled too.
        for (const std::pair<std::size_t, float>& entry : on_edge_sharpness) {
            const Boundary_vertex& boundary = m_boundary[entry.first];
            GEO::index_t           other    = boundary.edge_far_vertex;
            if (m_rings.contains(other)) {
                other = GEO::NO_INDEX;
                for (const Boundary_vertex& candidate : m_boundary) {
                    if ((candidate.bevel_vertex == boundary.edge_far_vertex) && (candidate.edge_far_vertex == boundary.bevel_vertex)) {
                        other = candidate.scratch_vertex;
                        break;
                    }
                }
            }
            if (other == GEO::NO_INDEX) {
                continue;
            }
            const GEO::index_t edge = m_edit_mesh.find_edge(boundary.scratch_vertex, other);
            if (edge != GEO::NO_INDEX) {
                m_edit_mesh.set_edge_sharpness(edge, entry.second);
            }
        }

        // The bevel vertices go, with their edges (the beveled edges and the
        // wire edges the rebuilt facets left behind).
        std::vector<GEO::index_t> bevel_vertices;
        for (const std::pair<const GEO::index_t, Ring>& entry : m_rings) {
            bevel_vertices.push_back(entry.first);
        }
        m_edit_mesh.delete_elements(bevel_vertices, Delete_context::vertices);
    }

    class Edge_facet_sides
    {
    public:
        GEO::index_t v      {GEO::NO_INDEX};
        GEO::index_t w      {GEO::NO_INDEX};
        GEO::index_t facet_r{GEO::NO_INDEX};
        GEO::index_t facet_l{GEO::NO_INDEX};
    };

    // Profile of a bevel vertex between two of its boundary vertices, keyed
    // by (bevel vertex, lower boundary index, higher boundary index).
    using Profile_key = std::tuple<GEO::index_t, std::size_t, std::size_t>;
    class Profile
    {
    public:
        std::size_t              first{0};  // the boundary vertex at x = 1
        std::vector<std::size_t> interior;  // samples 1 .. n - 1 from `first`
    };

    Bevel_edges_options                                                          m_options;
    std::vector<GEO::vec2f>                                                      m_profile_samples; // unit (x, y), segments + 1
    std::map<Profile_key, Profile>                                               m_profiles;
    std::set<GEO::index_t>                                                       m_cutoff_vertices; // closed fans with three or more beveled edges
    std::set<Edge_key>                                                           m_beveled;       // canonical scratch vertex pairs
    std::map<GEO::index_t, Ring>                                                 m_rings;         // per bevel vertex
    std::map<Edge_key, float>                                                    m_offset_scale;  // offset per unit amount, per beveled edge
    std::map<GEO::index_t, GEO::vec3f>                                           m_facet_normals;
    std::vector<Boundary_vertex>                                                 m_boundary;
    std::map<std::pair<GEO::index_t, GEO::index_t>, std::vector<std::size_t>>    m_chains;        // (facet, bevel vertex) -> boundary vertices in facet order
    std::map<GEO::index_t, std::vector<std::size_t>>                             m_polygons;      // bevel vertex -> boundary vertices in ring order
    std::vector<Edge_facet_sides>                                                m_edge_facet_sides;
    std::vector<GEO::index_t>                                                    m_edge_facets;   // scratch facets
    std::vector<GEO::index_t>                                                    m_vertex_facets; // scratch facets
    std::vector<GEO::index_t>                                                    m_dst_edge_facets;
    mutable std::vector<float>                                                   m_weights;
};

} // anonymous namespace

void bevel_edges(
    const Geometry&                                        source,
    Geometry&                                              destination,
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
    const Bevel_edges_options                              options,
    Bevel_edges_result* const                              result,
    Component_remap* const                                 remap
)
{
    Bevel_edges operation{source, destination, selected_edges, options};
    operation.build(result);
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap(*remap->destination);
    }
}

} // namespace erhe::geometry::operation
