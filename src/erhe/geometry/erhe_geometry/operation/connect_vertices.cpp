#include "erhe_geometry/operation/connect_vertices.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/geometry.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

namespace {

using Vertex_pair = std::pair<GEO::index_t, GEO::index_t>;

[[nodiscard]] auto make_vertex_pair(const GEO::index_t a, const GEO::index_t b) -> Vertex_pair
{
    return (a < b) ? Vertex_pair{a, b} : Vertex_pair{b, a};
}

// Even-odd test of p against the polygon, both projected onto the plane of
// the polygon's Newell normal.
[[nodiscard]] auto is_inside_polygon(const std::span<const GEO::vec3f> polygon, const GEO::vec3f& p) -> bool
{
    const GEO::vec3f normal = compute_newell_normal(polygon);
    const GEO::vec3f axis_u = GEO::normalize(
        (std::abs(normal.x) < 0.9f)
            ? GEO::cross(normal, GEO::vec3f{1.0f, 0.0f, 0.0f})
            : GEO::cross(normal, GEO::vec3f{0.0f, 1.0f, 0.0f})
    );
    const GEO::vec3f axis_v = GEO::cross(normal, axis_u);
    const float px = GEO::dot(p, axis_u);
    const float py = GEO::dot(p, axis_v);
    bool inside = false;
    const std::size_t n = polygon.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const float xi = GEO::dot(polygon[i], axis_u);
        const float yi = GEO::dot(polygon[i], axis_v);
        const float xj = GEO::dot(polygon[j], axis_u);
        const float yj = GEO::dot(polygon[j], axis_v);
        if (((yi > py) != (yj > py)) && (px < (((xj - xi) * (py - yi)) / (yj - yi)) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

// True when local corner c lies strictly inside the cyclic range (a, b) of
// an n-corner facet.
[[nodiscard]] auto is_strictly_between(const std::size_t a, const std::size_t b, const std::size_t c, const std::size_t n) -> bool
{
    const std::size_t length = (b + n - a) % n;
    const std::size_t offset = (c + n - a) % n;
    return (offset > 0) && (offset < length);
}

// True when chords (a, b) and (c, d) of one facet (local corners) cross.
[[nodiscard]] auto chords_cross(const std::pair<std::size_t, std::size_t>& first, const std::pair<std::size_t, std::size_t>& second, const std::size_t n) -> bool
{
    if (
        (first.first == second.first) || (first.first == second.second) ||
        (first.second == second.first) || (first.second == second.second)
    ) {
        return false;
    }
    const bool c_inside = is_strictly_between(first.first, first.second, second.first,  n);
    const bool d_inside = is_strictly_between(first.first, first.second, second.second, n);
    return c_inside != d_inside;
}

enum class Element_kind : unsigned int
{
    vertex = 0,
    edge   = 1
};

[[nodiscard]] auto make_element_key(const Element_kind kind, const GEO::index_t index) -> std::uint64_t
{
    return (static_cast<std::uint64_t>(kind) << 32u) | static_cast<std::uint64_t>(index);
}

[[nodiscard]] auto get_element_kind(const std::uint64_t key) -> Element_kind
{
    return static_cast<Element_kind>(static_cast<unsigned int>(key >> 32u));
}

[[nodiscard]] auto get_element_index(const std::uint64_t key) -> GEO::index_t
{
    return static_cast<GEO::index_t>(key & 0xffffffffu);
}

////////////////////////////////////////////////////////////////////////////////

class Connect_vertices : public Edit_mesh_operation
{
public:
    Connect_vertices(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
    }

    [[nodiscard]] auto share_facet(const GEO::index_t a, const GEO::index_t b) const -> bool
    {
        for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(a)) {
            if (m_edit_mesh.find_facet_corner(facet, b) != GEO::NO_INDEX) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto is_live_vertex(const GEO::index_t vertex) const -> bool
    {
        return (vertex < m_edit_mesh.get_vertex_slot_count()) && m_edit_mesh.is_vertex_alive(vertex);
    }

    void build_set(const std::set<GEO::index_t>& selected_vertices)
    {
        for (const GEO::index_t vertex : selected_vertices) {
            if (is_live_vertex(vertex)) {
                m_selected_vertices.push_back(vertex);
            }
        }
        connect_set();
        emit();
    }

    void build_pair(const GEO::index_t a, const GEO::index_t b)
    {
        if (is_live_vertex(a) && is_live_vertex(b) && (a != b)) {
            m_selected_vertices.push_back(a);
            m_selected_vertices.push_back(b);
            if (!connect_pair(a, b)) {
                log_operation->info("connect vertex pair: no path from vertex {} to vertex {}", a, b);
            }
        }
        emit();
    }

    void make_remap(Component_remap* remap) const
    {
        if ((remap == nullptr) || (remap->source == nullptr) || (remap->destination == nullptr)) {
            return;
        }
        Geometry_component_selection& dst = *remap->destination;
        dst.vertices.clear();
        dst.edges.clear();
        dst.facets.clear();
        for (const std::vector<GEO::index_t>* vertices : {&m_selected_vertices, &m_inserted_vertices}) {
            for (const GEO::index_t vertex : *vertices) {
                const GEO::index_t dst_vertex = get_emitted_vertex(vertex);
                if (dst_vertex != GEO::NO_INDEX) {
                    dst.vertices.insert(dst_vertex);
                }
            }
        }
        for (const Vertex_pair& pair : m_new_edges) {
            const GEO::index_t a = get_emitted_vertex(pair.first);
            const GEO::index_t b = get_emitted_vertex(pair.second);
            if ((a != GEO::NO_INDEX) && (b != GEO::NO_INDEX) && (m_edit_mesh.find_edge(pair.first, pair.second) != GEO::NO_INDEX)) {
                dst.edges.insert(make_vertex_pair(a, b));
            }
        }
    }

private:
    void connect_set()
    {
        std::vector<std::uint8_t> selected(m_edit_mesh.get_vertex_slot_count(), 0);
        for (const GEO::index_t vertex : m_selected_vertices) {
            selected[vertex] = 1;
        }
        std::vector<GEO::index_t>                        ring;
        std::vector<GEO::vec3f>                          polygon;
        std::vector<std::size_t>                         kept;
        std::vector<std::pair<std::size_t, std::size_t>> accepted;
        std::vector<GEO::index_t>                        pieces;
        const GEO::index_t facet_slot_count = m_edit_mesh.get_facet_slot_count(); // facets split off below are not revisited
        for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
            if (!m_edit_mesh.is_facet_alive(facet)) {
                continue;
            }
            ring.clear();
            polygon.clear();
            for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                ring.push_back(corner.vertex);
                polygon.push_back(m_edit_mesh.get_position(corner.vertex));
            }
            const std::size_t n = ring.size();
            kept.clear();
            for (std::size_t i = 0; i < n; ++i) {
                if (selected[ring[i]] == 0) {
                    continue;
                }
                const bool previous_selected = (selected[ring[(i + n - 1) % n]] != 0);
                const bool next_selected     = (selected[ring[(i + 1) % n]] != 0);
                if (previous_selected && next_selected) {
                    continue; // inside a contiguous selected run
                }
                kept.push_back(i);
            }
            if (kept.size() < 2) {
                continue;
            }
            const std::size_t pair_count = (kept.size() == 2) ? 1 : kept.size();
            accepted.clear();
            for (std::size_t k = 0; k < pair_count; ++k) {
                const std::pair<std::size_t, std::size_t> chord{kept[k], kept[(k + 1) % kept.size()]};
                const bool adjacent = (((chord.first + 1) % n) == chord.second) || (((chord.second + 1) % n) == chord.first);
                if (adjacent || (ring[chord.first] == ring[chord.second])) {
                    continue;
                }
                const GEO::vec3f midpoint = 0.5f * (polygon[chord.first] + polygon[chord.second]);
                if (!is_inside_polygon(polygon, midpoint)) {
                    log_operation->debug("connect vertices: split {} - {} leaves facet {}, dropped", ring[chord.first], ring[chord.second], facet);
                    continue;
                }
                bool crosses = false;
                for (const std::pair<std::size_t, std::size_t>& earlier : accepted) {
                    if (chords_cross(earlier, chord, n)) {
                        crosses = true;
                        break;
                    }
                }
                if (crosses) {
                    log_operation->debug("connect vertices: split {} - {} crosses an earlier split in facet {}, dropped", ring[chord.first], ring[chord.second], facet);
                    continue;
                }
                accepted.push_back(chord);
            }
            pieces.clear();
            pieces.push_back(facet);
            for (const std::pair<std::size_t, std::size_t>& chord : accepted) {
                static_cast<void>(split_between(pieces, ring[chord.first], ring[chord.second]));
            }
        }
    }

    // Splits the piece holding both vertices as non-adjacent corners.
    auto split_between(std::vector<GEO::index_t>& pieces, const GEO::index_t a, const GEO::index_t b) -> bool
    {
        for (std::size_t p = 0; p < pieces.size(); ++p) {
            const GEO::index_t piece    = pieces[p];
            const GEO::index_t corner_a = m_edit_mesh.find_facet_corner(piece, a);
            const GEO::index_t corner_b = m_edit_mesh.find_facet_corner(piece, b);
            if ((corner_a == GEO::NO_INDEX) || (corner_b == GEO::NO_INDEX)) {
                continue;
            }
            const GEO::index_t new_facet = m_edit_mesh.split_facet(piece, corner_a, corner_b);
            if (new_facet == GEO::NO_INDEX) {
                continue;
            }
            pieces.push_back(new_facet);
            m_new_edges.push_back(Vertex_pair{a, b});
            return true;
        }
        return false;
    }

    [[nodiscard]] auto vertex_normal(const GEO::index_t vertex) const -> GEO::vec3f
    {
        GEO::vec3f sum{0.0f, 0.0f, 0.0f};
        std::vector<GEO::vec3f> polygon;
        for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(vertex)) {
            polygon.clear();
            for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                polygon.push_back(m_edit_mesh.get_position(corner.vertex));
            }
            sum += compute_newell_normal(polygon);
        }
        return sum;
    }

    [[nodiscard]] auto connect_pair(const GEO::index_t a, const GEO::index_t b) -> bool
    {
        const GEO::vec3f position_a = m_edit_mesh.get_position(a);
        const GEO::vec3f position_b = m_edit_mesh.get_position(b);
        const GEO::vec3f d          = position_b - position_a;
        const float      d_length   = GEO::length(d);
        if (!(d_length > 0.0f)) {
            return false;
        }
        const GEO::vec3f d_unit = d / d_length;

        // The plane contains d and the mean normal projected perpendicular to d.
        const auto project = [&d_unit](const GEO::vec3f& v) -> GEO::vec3f {
            return v - (GEO::dot(v, d_unit) * d_unit);
        };
        GEO::vec3f in_plane = project(vertex_normal(a) + vertex_normal(b));
        if (GEO::length(in_plane) < 1e-6f) {
            // The axis least aligned with d.
            const float ax = std::abs(d_unit.x);
            const float ay = std::abs(d_unit.y);
            const float az = std::abs(d_unit.z);
            const GEO::vec3f axis =
                ((ax <= ay) && (ax <= az)) ? GEO::vec3f{1.0f, 0.0f, 0.0f} :
                (ay <= az)                 ? GEO::vec3f{0.0f, 1.0f, 0.0f} :
                                             GEO::vec3f{0.0f, 0.0f, 1.0f};
            in_plane = project(axis);
        }
        const GEO::vec3f plane_normal = GEO::normalize(GEO::cross(d_unit, GEO::normalize(in_plane)));
        const float      tolerance    = 1e-5f * d_length;

        const auto signed_distance = [&](const GEO::index_t vertex) -> float {
            return GEO::dot(plane_normal, m_edit_mesh.get_position(vertex) - position_a);
        };
        const auto on_plane = [&](const GEO::index_t vertex) -> bool {
            return std::abs(signed_distance(vertex)) <= tolerance;
        };
        const auto crossing_t = [&](const GEO::index_t edge) -> float {
            const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
            const float s0 = signed_distance(edit_edge.vertices[0]);
            const float s1 = signed_distance(edit_edge.vertices[1]);
            return s0 / (s0 - s1);
        };
        const auto is_crossed = [&](const GEO::index_t u, const GEO::index_t v) -> bool {
            const float su = signed_distance(u);
            const float sv = signed_distance(v);
            return ((su > tolerance) && (sv < -tolerance)) || ((su < -tolerance) && (sv > tolerance));
        };
        const auto element_point = [&](const std::uint64_t key) -> GEO::vec3f {
            const GEO::index_t index = get_element_index(key);
            if (get_element_kind(key) == Element_kind::vertex) {
                return m_edit_mesh.get_position(index);
            }
            const Edit_edge& edit_edge = m_edit_mesh.get_edge(index);
            const float t = crossing_t(index);
            return ((1.0f - t) * m_edit_mesh.get_position(edit_edge.vertices[0])) + (t * m_edit_mesh.get_position(edit_edge.vertices[1]));
        };

        // Best-first search keyed by accumulated length.
        using Queue_entry = std::pair<float, std::uint64_t>;
        std::priority_queue<Queue_entry, std::vector<Queue_entry>, std::greater<Queue_entry>> queue;
        std::map<std::uint64_t, float>         distance;
        std::map<std::uint64_t, std::uint64_t> parent;
        std::set<std::uint64_t>                visited;
        const std::uint64_t start_key  = make_element_key(Element_kind::vertex, a);
        const std::uint64_t target_key = make_element_key(Element_kind::vertex, b);
        distance[start_key] = 0.0f;
        queue.push(Queue_entry{0.0f, start_key});
        bool found = false;
        std::vector<GEO::index_t> facets;
        while (!queue.empty()) {
            const Queue_entry entry = queue.top();
            queue.pop();
            const std::uint64_t key = entry.second;
            if (visited.contains(key)) {
                continue;
            }
            visited.insert(key);
            if (key == target_key) {
                found = true;
                break;
            }
            const GEO::index_t index = get_element_index(key);
            facets.clear();
            if (get_element_kind(key) == Element_kind::vertex) {
                const std::span<const GEO::index_t> vertex_facets = m_edit_mesh.get_vertex_facets(index);
                facets.assign(vertex_facets.begin(), vertex_facets.end());
            } else {
                facets = m_edit_mesh.get_edge(index).facets;
            }
            const GEO::vec3f point = element_point(key);
            const auto relax = [&](const std::uint64_t next_key) {
                if ((next_key == key) || visited.contains(next_key)) {
                    return;
                }
                const float next_distance = entry.first + GEO::length(element_point(next_key) - point);
                const auto i = distance.find(next_key);
                if ((i == distance.end()) || (next_distance < i->second)) {
                    distance[next_key] = next_distance;
                    parent[next_key]   = key;
                    queue.push(Queue_entry{next_distance, next_key});
                }
            };
            for (const GEO::index_t facet : facets) {
                const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
                const std::size_t n = corners.size();
                for (std::size_t i = 0; i < n; ++i) {
                    const GEO::index_t u = corners[i].vertex;
                    const GEO::index_t v = corners[(i + 1) % n].vertex;
                    if (on_plane(u)) {
                        relax(make_element_key(Element_kind::vertex, u));
                    }
                    if (is_crossed(u, v)) {
                        const GEO::index_t edge = m_edit_mesh.find_edge(u, v);
                        if (edge != GEO::NO_INDEX) {
                            relax(make_element_key(Element_kind::edge, edge));
                        }
                    }
                }
            }
        }
        if (!found) {
            return false;
        }

        std::vector<std::uint64_t> path;
        for (std::uint64_t key = target_key; key != start_key; key = parent.at(key)) {
            path.push_back(key);
        }
        path.push_back(start_key);
        std::reverse(path.begin(), path.end());

        // Split the crossed edges at the plane.
        std::vector<GEO::index_t> path_vertices;
        for (const std::uint64_t key : path) {
            const GEO::index_t index = get_element_index(key);
            if (get_element_kind(key) == Element_kind::vertex) {
                path_vertices.push_back(index);
                continue;
            }
            const float        t      = crossing_t(index);
            const GEO::index_t vertex = m_edit_mesh.split_edge(index, t);
            m_inserted_vertices.push_back(vertex);
            path_vertices.push_back(vertex);
        }

        // Connect consecutive path vertices across the facet they share.
        std::vector<GEO::index_t> candidates;
        for (std::size_t i = 0; (i + 1) < path_vertices.size(); ++i) {
            const GEO::index_t x = path_vertices[i];
            const GEO::index_t y = path_vertices[i + 1];
            if (m_edit_mesh.find_edge(x, y) != GEO::NO_INDEX) {
                m_new_edges.push_back(Vertex_pair{x, y});
                continue;
            }
            const std::span<const GEO::index_t> x_facets = m_edit_mesh.get_vertex_facets(x);
            candidates.assign(x_facets.begin(), x_facets.end());
            if (!split_between(candidates, x, y)) {
                log_operation->warn("connect vertex pair: path vertices {} and {} share no facet", x, y);
            }
        }
        return true;
    }

    std::vector<GEO::index_t> m_selected_vertices; // scratch vertices
    std::vector<GEO::index_t> m_inserted_vertices; // edge split vertices of a pair path
    std::vector<Vertex_pair>  m_new_edges;         // scratch vertex pairs
};

} // anonymous namespace

void connect_vertices(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_vertices,
    Component_remap*              remap
)
{
    Connect_vertices operation{source, destination};
    operation.build_set(selected_vertices);
    operation.make_remap(remap);
}

void connect_vertex_pair(
    const Geometry&    source,
    Geometry&          destination,
    const GEO::index_t a,
    const GEO::index_t b,
    Component_remap*   remap
)
{
    Connect_vertices operation{source, destination};
    operation.build_pair(a, b);
    operation.make_remap(remap);
}

void connect_selection(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Component_remap*                    remap
)
{
    std::set<GEO::index_t> vertices = selection.vertices;
    for (const Vertex_pair& edge : selection.edges) {
        vertices.insert(edge.first);
        vertices.insert(edge.second);
    }
    Connect_vertices operation{source, destination};
    if (vertices.size() == 2) {
        const GEO::index_t a = *vertices.begin();
        const GEO::index_t b = *std::next(vertices.begin());
        if (operation.is_live_vertex(a) && operation.is_live_vertex(b) && !operation.share_facet(a, b)) {
            operation.build_pair(a, b);
            operation.make_remap(remap);
            return;
        }
    }
    operation.build_set(vertices);
    operation.make_remap(remap);
}

} // namespace erhe::geometry::operation
