#include "erhe_geometry/operation/fill.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/split_components.hpp"
#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/geometry.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
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

// The area vector of a polygon (Newell, not normalized): its length is twice
// the area of a planar polygon, and it shrinks when the polygon folds over
// itself (a bow-tie).
[[nodiscard]] auto newell_vector(const Edit_mesh& edit_mesh, const std::span<const GEO::index_t> cycle) -> GEO::vec3f
{
    GEO::vec3f normal{0.0f, 0.0f, 0.0f};
    const std::size_t n = cycle.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::vec3f a = edit_mesh.get_position(cycle[i]);
        const GEO::vec3f b = edit_mesh.get_position(cycle[(i + 1) % n]);
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    return normal;
}

[[nodiscard]] auto facet_unit_normal(const Edit_mesh& edit_mesh, const GEO::index_t facet) -> GEO::vec3f
{
    GEO::vec3f normal{0.0f, 0.0f, 0.0f};
    const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
    const std::size_t n = corners.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::vec3f a = edit_mesh.get_position(corners[i].vertex);
        const GEO::vec3f b = edit_mesh.get_position(corners[(i + 1) % n].vertex);
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    const float length = GEO::length(normal);
    return (length > 0.0f) ? (normal / length) : GEO::vec3f{0.0f, 0.0f, 0.0f};
}

// True when the facet has the directed edge u -> v.
[[nodiscard]] auto traverses(const Edit_mesh& edit_mesh, const GEO::index_t facet, const GEO::index_t u, const GEO::index_t v) -> bool
{
    const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
    const std::size_t n = corners.size();
    for (std::size_t i = 0; i < n; ++i) {
        if ((corners[i].vertex == u) && (corners[(i + 1) % n].vertex == v)) {
            return true;
        }
    }
    return false;
}

// The least squares plane normal of the points (the covariance eigenvector
// of the smallest eigenvalue, through the largest 2x2 determinant), with its
// largest component made positive. +Z when the points are degenerate.
[[nodiscard]] auto fit_plane_normal(const std::span<const GEO::vec3f> points) -> GEO::vec3f
{
    GEO::vec3f centroid{0.0f, 0.0f, 0.0f};
    for (const GEO::vec3f& p : points) {
        centroid += p;
    }
    centroid = centroid / static_cast<float>(std::max<std::size_t>(points.size(), 1));
    double xx = 0.0;
    double xy = 0.0;
    double xz = 0.0;
    double yy = 0.0;
    double yz = 0.0;
    double zz = 0.0;
    for (const GEO::vec3f& p : points) {
        const double x = static_cast<double>(p.x - centroid.x);
        const double y = static_cast<double>(p.y - centroid.y);
        const double z = static_cast<double>(p.z - centroid.z);
        xx += x * x;
        xy += x * y;
        xz += x * z;
        yy += y * y;
        yz += y * z;
        zz += z * z;
    }
    const double det_x = (yy * zz) - (yz * yz);
    const double det_y = (xx * zz) - (xz * xz);
    const double det_z = (xx * yy) - (xy * xy);
    const double det_max = std::max(det_x, std::max(det_y, det_z));
    if (det_max <= 0.0) {
        return GEO::vec3f{0.0f, 0.0f, 1.0f};
    }
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    if (det_max == det_x) {
        nx = det_x;
        ny = (xz * yz) - (xy * zz);
        nz = (xy * yz) - (xz * yy);
    } else if (det_max == det_y) {
        nx = (xz * yz) - (xy * zz);
        ny = det_y;
        nz = (xy * xz) - (yz * xx);
    } else {
        nx = (xy * yz) - (xz * yy);
        ny = (xy * xz) - (yz * xx);
        nz = det_z;
    }
    GEO::vec3f normal{static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz)};
    const float length = GEO::length(normal);
    if (!(length > 0.0f)) {
        return GEO::vec3f{0.0f, 0.0f, 1.0f};
    }
    normal = normal / length;
    const float ax = std::abs(normal.x);
    const float ay = std::abs(normal.y);
    const float az = std::abs(normal.z);
    const float largest = ((ax >= ay) && (ax >= az)) ? normal.x : ((ay >= az) ? normal.y : normal.z);
    return (largest < 0.0f) ? (-1.0f * normal) : normal;
}

class Chain
{
public:
    std::vector<GEO::index_t> vertices;
    bool                      closed{false};
};

////////////////////////////////////////////////////////////////////////////////

class Fill : public Edit_mesh_operation
{
public:
    Fill(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
    }

    void build(const Geometry_component_selection& selection)
    {
        gather(selection);
        m_filled =
            fill_two_vertices()            ||
            fill_free_vertex_and_chain()   ||
            fill_edges()                   ||
            dissolve_selected_facets()     ||
            fill_radial();
        if (!m_filled) {
            log_operation->info("fill: nothing to fill");
        }
        emit();
    }

    void make_result(Fill_result* result, Component_remap* remap) const
    {
        std::vector<GEO::index_t> facet_to_dst(m_edit_mesh.get_facet_slot_count(), GEO::NO_INDEX);
        GEO::index_t next = 0;
        for (GEO::index_t facet = 0, end = m_edit_mesh.get_facet_slot_count(); facet < end; ++facet) {
            if (m_edit_mesh.is_facet_alive(facet)) {
                facet_to_dst[facet] = next++;
            }
        }
        if (result != nullptr) {
            result->new_facets.clear();
            result->filled = m_filled;
        }
        const bool has_remap = (remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr);
        if (has_remap) {
            remap->destination->vertices.clear();
            remap->destination->edges.clear();
            remap->destination->facets.clear();
        }
        for (const GEO::index_t facet : m_new_facets) {
            if (!m_edit_mesh.is_facet_alive(facet)) {
                continue;
            }
            if (result != nullptr) {
                result->new_facets.push_back(facet_to_dst[facet]);
            }
            if (!has_remap) {
                continue;
            }
            Geometry_component_selection& dst = *remap->destination;
            dst.facets.insert(facet_to_dst[facet]);
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
            const std::size_t n = corners.size();
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t a = get_emitted_vertex(corners[i].vertex);
                const GEO::index_t b = get_emitted_vertex(corners[(i + 1) % n].vertex);
                dst.vertices.insert(a);
                dst.edges.insert(make_vertex_pair(a, b));
            }
        }
    }

private:
    void gather(const Geometry_component_selection& selection)
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        std::set<GEO::index_t> vertices;
        std::set<GEO::index_t> edges;
        for (const GEO::index_t vertex : selection.vertices) {
            if ((vertex < vertex_slot_count) && m_edit_mesh.is_vertex_alive(vertex)) {
                vertices.insert(vertex);
                for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
                    if (selection.vertices.contains(m_edit_mesh.get_edge_other_vertex(edge, vertex))) {
                        edges.insert(edge);
                    }
                }
            }
        }
        for (const Vertex_pair& pair : selection.edges) {
            if ((pair.first >= vertex_slot_count) || (pair.second >= vertex_slot_count)) {
                continue;
            }
            const GEO::index_t edge = m_edit_mesh.find_edge(pair.first, pair.second);
            if (edge == GEO::NO_INDEX) {
                continue;
            }
            edges.insert(edge);
            vertices.insert(pair.first);
            vertices.insert(pair.second);
        }
        m_selected_vertices.assign(vertices.begin(), vertices.end());
        for (const GEO::index_t edge : edges) {
            if (m_edit_mesh.get_edge_facet_count(edge) < 2) {
                m_usable_edges.push_back(edge);
            }
        }
        get_selection_facets(source, selection, m_selected_facets);
    }

    // Selected vertices on no usable edge.
    [[nodiscard]] auto get_free_vertices() const -> std::vector<GEO::index_t>
    {
        std::set<GEO::index_t> on_edges;
        for (const GEO::index_t edge : m_usable_edges) {
            const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
            on_edges.insert(edit_edge.vertices[0]);
            on_edges.insert(edit_edge.vertices[1]);
        }
        std::vector<GEO::index_t> free_vertices;
        for (const GEO::index_t vertex : m_selected_vertices) {
            if (!on_edges.contains(vertex)) {
                free_vertices.push_back(vertex);
            }
        }
        return free_vertices;
    }

    // The usable edges as chains (open paths from their lower-numbered end
    // first, then closed cycles). False when a vertex has more than two
    // usable edges (an edge net).
    [[nodiscard]] auto get_chains(std::vector<Chain>& out_chains) const -> bool
    {
        out_chains.clear();
        std::map<GEO::index_t, std::vector<GEO::index_t>> adjacency;
        for (const GEO::index_t edge : m_usable_edges) {
            const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
            adjacency[edit_edge.vertices[0]].push_back(edit_edge.vertices[1]);
            adjacency[edit_edge.vertices[1]].push_back(edit_edge.vertices[0]);
        }
        for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            if (entry.second.size() > 2) {
                return false;
            }
        }
        std::set<GEO::index_t> visited;
        const auto walk = [&](const GEO::index_t start, Chain& chain) {
            GEO::index_t previous = GEO::NO_INDEX;
            GEO::index_t current  = start;
            for (;;) {
                chain.vertices.push_back(current);
                visited.insert(current);
                GEO::index_t next = GEO::NO_INDEX;
                for (const GEO::index_t other : adjacency.at(current)) {
                    if ((other != previous) && !visited.contains(other)) {
                        next = other;
                        break;
                    }
                }
                if (next == GEO::NO_INDEX) {
                    return;
                }
                previous = current;
                current  = next;
            }
        };
        for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            if ((entry.second.size() == 1) && !visited.contains(entry.first)) {
                Chain chain{};
                walk(entry.first, chain);
                out_chains.push_back(std::move(chain));
            }
        }
        for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            if (!visited.contains(entry.first)) {
                Chain chain{};
                chain.closed = true;
                walk(entry.first, chain);
                out_chains.push_back(std::move(chain));
            }
        }
        return true;
    }

    // Case 1: two vertices joined by the shortest boundary chain.
    [[nodiscard]] auto fill_two_vertices() -> bool
    {
        if ((m_selected_vertices.size() != 2) || !m_usable_edges.empty()) {
            return false;
        }
        const GEO::index_t a = m_selected_vertices[0];
        const GEO::index_t b = m_selected_vertices[1];
        std::map<GEO::index_t, GEO::index_t> parent;
        std::queue<GEO::index_t> front;
        parent[a] = a;
        front.push(a);
        while (!front.empty() && !parent.contains(b)) {
            const GEO::index_t vertex = front.front();
            front.pop();
            for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
                if (m_edit_mesh.get_edge_facet_count(edge) != 1) {
                    continue;
                }
                const GEO::index_t other = m_edit_mesh.get_edge_other_vertex(edge, vertex);
                if ((vertex == a) && (other == b)) {
                    continue; // the single edge between them closes nothing
                }
                if (!parent.contains(other)) {
                    parent[other] = vertex;
                    front.push(other);
                }
            }
        }
        if (!parent.contains(b)) {
            return false;
        }
        std::vector<GEO::index_t> cycle;
        for (GEO::index_t vertex = b; vertex != a; vertex = parent.at(vertex)) {
            cycle.push_back(vertex);
        }
        cycle.push_back(a);
        std::reverse(cycle.begin(), cycle.end());
        return create_fill_facet(cycle);
    }

    // Case 2: one free vertex plus one open chain.
    [[nodiscard]] auto fill_free_vertex_and_chain() -> bool
    {
        const std::vector<GEO::index_t> free_vertices = get_free_vertices();
        if ((free_vertices.size() != 1) || m_usable_edges.empty()) {
            return false;
        }
        std::vector<Chain> chains;
        if (!get_chains(chains) || (chains.size() != 1) || chains.front().closed) {
            return false;
        }
        std::vector<GEO::index_t> cycle = chains.front().vertices;
        cycle.push_back(free_vertices.front());
        return create_fill_facet(cycle);
    }

    // Case 3: cycles and up to two open chains, else the edge net.
    [[nodiscard]] auto fill_edges() -> bool
    {
        if (m_usable_edges.empty()) {
            return false;
        }
        std::vector<Chain> chains;
        if (get_chains(chains)) {
            std::vector<const Chain*> open_chains;
            for (const Chain& chain : chains) {
                if (!chain.closed) {
                    open_chains.push_back(&chain);
                }
            }
            if (open_chains.size() <= 2) {
                bool created = false;
                for (const Chain& chain : chains) {
                    if (chain.closed) {
                        created = create_fill_facet(chain.vertices) || created;
                    }
                }
                if (open_chains.size() == 1) {
                    created = create_fill_facet(open_chains.front()->vertices) || created;
                } else if (open_chains.size() == 2) {
                    const std::vector<GEO::index_t>& first  = open_chains[0]->vertices;
                    const std::vector<GEO::index_t>& second = open_chains[1]->vertices;
                    std::vector<GEO::index_t> joined_forward = first;
                    joined_forward.insert(joined_forward.end(), second.begin(), second.end());
                    std::vector<GEO::index_t> joined_reverse = first;
                    joined_reverse.insert(joined_reverse.end(), second.rbegin(), second.rend());
                    const float forward_area = GEO::length(newell_vector(m_edit_mesh, joined_forward));
                    const float reverse_area = GEO::length(newell_vector(m_edit_mesh, joined_reverse));
                    created = create_fill_facet((forward_area >= reverse_area) ? joined_forward : joined_reverse) || created;
                }
                return created;
            }
        }
        return fill_edge_net();
    }

    // The edge net: each usable edge takes the shortest directed cycle of
    // usable edges through it, traversing every edge opposite to its facet.
    [[nodiscard]] auto fill_edge_net() -> bool
    {
        const GEO::index_t edge_slot_count = m_edit_mesh.get_edge_slot_count();
        std::vector<std::uint8_t> in_net(edge_slot_count, 0);
        for (const GEO::index_t edge : m_usable_edges) {
            in_net[edge] = 1;
        }
        // The one facet of a net edge, when it still has exactly one.
        const auto single_facet = [this, &in_net](const GEO::index_t edge) -> GEO::index_t {
            if ((edge >= in_net.size()) || (in_net[edge] == 0) || !m_edit_mesh.is_edge_alive(edge)) {
                return GEO::NO_INDEX;
            }
            const std::vector<GEO::index_t>& facets = m_edit_mesh.get_edge(edge).facets;
            return (facets.size() == 1) ? facets.front() : GEO::NO_INDEX;
        };

        std::set<GEO::index_t> tried;
        bool created_any = false;
        bool progress    = true;
        while (progress) {
            progress = false;
            for (const GEO::index_t edge : m_usable_edges) {
                const GEO::index_t facet = single_facet(edge);
                if ((facet == GEO::NO_INDEX) || tried.contains(edge)) {
                    continue;
                }
                const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
                // The existing facet traverses u -> v; the new one v -> u,
                // then a path u -> ... -> v.
                const bool forward = traverses(m_edit_mesh, facet, edit_edge.vertices[0], edit_edge.vertices[1]);
                const GEO::index_t u = forward ? edit_edge.vertices[0] : edit_edge.vertices[1];
                const GEO::index_t v = forward ? edit_edge.vertices[1] : edit_edge.vertices[0];

                std::map<GEO::index_t, GEO::index_t> parent;
                std::queue<GEO::index_t> front;
                parent[u] = u;
                front.push(u);
                while (!front.empty() && !parent.contains(v)) {
                    const GEO::index_t x = front.front();
                    front.pop();
                    for (const GEO::index_t next_edge : m_edit_mesh.get_vertex_edges(x)) {
                        if (next_edge == edge) {
                            continue;
                        }
                        const GEO::index_t next_facet = single_facet(next_edge);
                        if (next_facet == GEO::NO_INDEX) {
                            continue;
                        }
                        const GEO::index_t y = m_edit_mesh.get_edge_other_vertex(next_edge, x);
                        // The new facet goes x -> y: the existing one must go y -> x.
                        if (!traverses(m_edit_mesh, next_facet, y, x) || parent.contains(y)) {
                            continue;
                        }
                        parent[y] = x;
                        front.push(y);
                    }
                }
                if (!parent.contains(v)) {
                    tried.insert(edge);
                    continue;
                }
                std::vector<GEO::index_t> path; // v, ..., u (backwards)
                for (GEO::index_t x = v; x != u; x = parent.at(x)) {
                    path.push_back(x);
                }
                path.push_back(u);
                // cycle: v -> u -> ... -> (before v)
                std::vector<GEO::index_t> cycle{v};
                for (std::size_t i = path.size(); i > 1; --i) {
                    cycle.push_back(path[i - 1]);
                }
                if (create_fill_facet(cycle)) {
                    created_any = true;
                    progress    = true;
                } else {
                    tried.insert(edge);
                }
            }
        }
        return created_any;
    }

    // Case 4: the selected facets dissolve, one join per edge-connected group.
    [[nodiscard]] auto dissolve_selected_facets() -> bool
    {
        if (m_selected_facets.empty()) {
            return false;
        }
        const GEO::index_t facet_slot_count = m_edit_mesh.get_facet_slot_count();
        std::vector<std::uint8_t> selected(facet_slot_count, 0);
        for (const GEO::index_t facet : m_selected_facets) {
            if ((facet < facet_slot_count) && m_edit_mesh.is_facet_alive(facet)) {
                selected[facet] = 1;
            }
        }
        std::vector<GEO::index_t> parent(facet_slot_count);
        std::iota(parent.begin(), parent.end(), GEO::index_t{0});
        const auto find_root = [&parent](GEO::index_t x) -> GEO::index_t {
            while (parent[x] != x) {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        };
        for (GEO::index_t edge = 0, end = m_edit_mesh.get_edge_slot_count(); edge < end; ++edge) {
            if (!m_edit_mesh.is_edge_alive(edge)) {
                continue;
            }
            const std::vector<GEO::index_t>& edge_facets = m_edit_mesh.get_edge(edge).facets;
            if ((edge_facets.size() != 2) || (selected[edge_facets[0]] == 0) || (selected[edge_facets[1]] == 0)) {
                continue;
            }
            const GEO::index_t root_a = find_root(edge_facets[0]);
            const GEO::index_t root_b = find_root(edge_facets[1]);
            if (root_a != root_b) {
                parent[root_a] = root_b;
            }
        }
        std::map<GEO::index_t, std::vector<GEO::index_t>> groups;
        for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
            if (selected[facet] != 0) {
                groups[find_root(facet)].push_back(facet);
            }
        }
        bool joined_any = false;
        for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : groups) {
            if (entry.second.size() < 2) {
                continue;
            }
            GEO::index_t joined = GEO::NO_INDEX;
            const Join_result join_result = m_edit_mesh.join_facets(entry.second, joined);
            if (join_result == Join_result::joined) {
                m_new_facets.push_back(joined);
                joined_any = true;
            } else {
                log_operation->debug("fill: group of {} facets kept (join result {})", entry.second.size(), static_cast<unsigned int>(join_result));
            }
        }
        return joined_any;
    }

    // Case 5: three or more vertices sorted radially in their fitted plane.
    [[nodiscard]] auto fill_radial() -> bool
    {
        if (m_selected_vertices.size() < 3) {
            return false;
        }
        std::vector<GEO::vec3f> points;
        GEO::vec3f centroid{0.0f, 0.0f, 0.0f};
        for (const GEO::index_t vertex : m_selected_vertices) {
            points.push_back(m_edit_mesh.get_position(vertex));
            centroid += points.back();
        }
        centroid = centroid / static_cast<float>(points.size());
        const GEO::vec3f normal = fit_plane_normal(points);
        const GEO::vec3f axis_u = GEO::normalize(
            (std::abs(normal.x) < 0.9f)
                ? GEO::cross(normal, GEO::vec3f{1.0f, 0.0f, 0.0f})
                : GEO::cross(normal, GEO::vec3f{0.0f, 1.0f, 0.0f})
        );
        const GEO::vec3f axis_v = GEO::cross(normal, axis_u);
        std::vector<std::pair<float, GEO::index_t>> by_angle;
        for (std::size_t i = 0; i < points.size(); ++i) {
            const GEO::vec3f d = points[i] - centroid;
            by_angle.emplace_back(std::atan2(GEO::dot(d, axis_v), GEO::dot(d, axis_u)), m_selected_vertices[i]);
        }
        std::sort(by_angle.begin(), by_angle.end());
        std::vector<GEO::index_t> cycle;
        for (const std::pair<float, GEO::index_t>& entry : by_angle) {
            cycle.push_back(entry.second);
        }
        return create_fill_facet(cycle);
    }

    // Orients the cycle by its neighbours and creates the facet with corner
    // provenance from neighbouring facets. False (nothing created) when the
    // cycle is invalid, uses an edge with two facets or repeats a facet.
    [[nodiscard]] auto create_fill_facet(const std::span<const GEO::index_t> cycle_in) -> bool
    {
        const std::size_t n = cycle_in.size();
        if (n < 3) {
            return false;
        }
        std::vector<GEO::index_t> cycle{cycle_in.begin(), cycle_in.end()};
        std::size_t keep_votes    = 0;
        std::size_t reverse_votes = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const GEO::index_t u    = cycle[i];
            const GEO::index_t v    = cycle[(i + 1) % n];
            const GEO::index_t edge = m_edit_mesh.find_edge(u, v);
            if (edge == GEO::NO_INDEX) {
                continue;
            }
            if (m_edit_mesh.get_edge_facet_count(edge) >= 2) {
                return false;
            }
            for (const GEO::index_t facet : m_edit_mesh.get_edge(edge).facets) {
                if (traverses(m_edit_mesh, facet, u, v)) {
                    ++reverse_votes;
                } else {
                    ++keep_votes;
                }
            }
        }
        bool reverse = (reverse_votes > keep_votes);
        if (reverse_votes == keep_votes) {
            GEO::vec3f normal_sum{0.0f, 0.0f, 0.0f};
            for (const GEO::index_t vertex : cycle) {
                for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(vertex)) {
                    normal_sum += facet_unit_normal(m_edit_mesh, facet);
                }
            }
            if (GEO::length(normal_sum) > 0.0f) {
                reverse = (GEO::dot(newell_vector(m_edit_mesh, cycle), normal_sum) < 0.0f);
            }
        }
        if (reverse) {
            std::reverse(cycle.begin(), cycle.end());
        }

        // Corner provenance from a facet on the new facet's edges at each
        // vertex, else any facet at the vertex.
        GEO::index_t reference_facet = GEO::NO_INDEX;
        std::vector<Edit_corner> corners(n);
        for (std::size_t i = 0; i < n; ++i) {
            const GEO::index_t vertex   = cycle[i];
            const GEO::index_t previous = cycle[(i + n - 1) % n];
            const GEO::index_t next     = cycle[(i + 1) % n];
            GEO::index_t from_facet = GEO::NO_INDEX;
            for (const GEO::index_t other : {next, previous}) {
                const GEO::index_t edge = m_edit_mesh.find_edge(vertex, other);
                if ((edge != GEO::NO_INDEX) && !m_edit_mesh.get_edge(edge).facets.empty()) {
                    from_facet = m_edit_mesh.get_edge(edge).facets.front();
                    break;
                }
            }
            if (from_facet != GEO::NO_INDEX) {
                if (reference_facet == GEO::NO_INDEX) {
                    reference_facet = from_facet;
                }
            } else if (!m_edit_mesh.get_vertex_facets(vertex).empty()) {
                from_facet = m_edit_mesh.get_vertex_facets(vertex).front();
            }
            corners[i].vertex = vertex;
            if (from_facet != GEO::NO_INDEX) {
                const GEO::index_t local_corner = m_edit_mesh.find_facet_corner(from_facet, vertex);
                corners[i].sources = m_edit_mesh.get_facet_corners(from_facet)[local_corner].sources;
            }
        }
        if (reference_facet == GEO::NO_INDEX) {
            for (const GEO::index_t vertex : cycle) {
                if (!m_edit_mesh.get_vertex_facets(vertex).empty()) {
                    reference_facet = m_edit_mesh.get_vertex_facets(vertex).front();
                    break;
                }
            }
        }
        const GEO::index_t source_facet = (reference_facet != GEO::NO_INDEX) ? m_edit_mesh.get_facet(reference_facet).source_facet : GEO::NO_INDEX;
        const GEO::index_t facet = m_edit_mesh.create_facet_from_corners(corners, source_facet);
        if (facet == GEO::NO_INDEX) {
            return false;
        }
        m_new_facets.push_back(facet);
        return true;
    }

    std::vector<GEO::index_t> m_selected_vertices; // scratch vertices, ascending
    std::vector<GEO::index_t> m_usable_edges;      // scratch edges, ascending
    std::set<GEO::index_t>    m_selected_facets;
    std::vector<GEO::index_t> m_new_facets;        // scratch facets
    bool                      m_filled{false};
};

} // anonymous namespace

void fill_selection(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Fill_result*                        result,
    Component_remap*                    remap
)
{
    Fill operation{source, destination};
    operation.build(selection);
    operation.make_result(result, remap);
}

} // namespace erhe::geometry::operation
