#include "erhe_geometry/operation/dissolve.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/geometry.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <numeric>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

namespace {

auto degrees_to_radians(const float degrees) -> float
{
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

auto facet_normal(const Edit_mesh& edit_mesh, const GEO::index_t facet) -> GEO::vec3f
{
    const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
    const std::size_t n = corners.size();
    GEO::vec3f normal{0.0f, 0.0f, 0.0f};
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

auto angle_between(const GEO::vec3f& a, const GEO::vec3f& b) -> float
{
    const float length_a = GEO::length(a);
    const float length_b = GEO::length(b);
    if ((length_a == 0.0f) || (length_b == 0.0f)) {
        return 0.0f;
    }
    const float cosine = std::clamp(GEO::dot(a, b) / (length_a * length_b), -1.0f, 1.0f);
    return std::acos(cosine);
}

// The plain angle between the two edges of a two-valent vertex: the angle
// between the direction into the vertex and the direction out of it, 0 when
// the edges are collinear.
auto two_valent_edge_angle(const Edit_mesh& edit_mesh, const GEO::index_t vertex) -> float
{
    const std::span<const GEO::index_t> edges = edit_mesh.get_vertex_edges(vertex);
    const GEO::index_t prev = edit_mesh.get_edge_other_vertex(edges[0], vertex);
    const GEO::index_t next = edit_mesh.get_edge_other_vertex(edges[1], vertex);
    const GEO::vec3f   p    = edit_mesh.get_position(vertex);
    return angle_between(p - edit_mesh.get_position(prev), edit_mesh.get_position(next) - p);
}

// Splits the facet along the chord between the two corners neighbouring the
// vertex's corner, leaving the corner in a triangle. No-op for a triangle.
void split_off_corner(Edit_mesh& edit_mesh, const GEO::index_t vertex, const GEO::index_t facet)
{
    if (!edit_mesh.is_facet_alive(facet)) {
        return;
    }
    const GEO::index_t n = static_cast<GEO::index_t>(edit_mesh.get_facet_corners(facet).size());
    const GEO::index_t i = edit_mesh.find_facet_corner(facet, vertex);
    if ((n <= 3) || (i == GEO::NO_INDEX)) {
        return;
    }
    const GEO::index_t prev = (i + n - 1) % n;
    const GEO::index_t next = (i + 1) % n;
    static_cast<void>(edit_mesh.split_facet(facet, prev, next));
}

// Splits off the vertex's corner in each facet around it whose two
// neighbouring corners are not marked.
void split_off_corners(Edit_mesh& edit_mesh, const GEO::index_t vertex, const std::vector<std::uint8_t>& marked)
{
    const std::span<const GEO::index_t> facet_span = edit_mesh.get_vertex_facets(vertex);
    const std::vector<GEO::index_t> facets{facet_span.begin(), facet_span.end()};
    for (const GEO::index_t facet : facets) {
        const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
        const std::size_t  n = corners.size();
        const GEO::index_t i = edit_mesh.find_facet_corner(facet, vertex);
        if ((n <= 3) || (i == GEO::NO_INDEX)) {
            continue;
        }
        const GEO::index_t prev_vertex = corners[(i + n - 1) % n].vertex;
        const GEO::index_t next_vertex = corners[(i + 1) % n].vertex;
        const auto is_marked = [&marked](const GEO::index_t v) -> bool {
            return (v < marked.size()) && (marked[v] != 0);
        };
        if (is_marked(prev_vertex) || is_marked(next_vertex)) {
            continue;
        }
        split_off_corner(edit_mesh, vertex, facet);
    }
}

// Carries the source selection through the operation's provenance.
void remap_selection(const Geometry_operation& operation, Component_remap* remap)
{
    if ((remap == nullptr) || (remap->source == nullptr) || (remap->destination == nullptr)) {
        return;
    }
    operation.remap_component_selection(*remap->source, *remap->destination);
}

////////////////////////////////////////////////////////////////////////////////

class Delete_components : public Edit_mesh_operation
{
public:
    Delete_components(const Geometry& source, Geometry& destination, const Geometry_component_selection& selection, const Delete_context context)
        : Edit_mesh_operation{source, destination}
        , m_selection        {selection}
        , m_context          {context}
    {
    }

    void build()
    {
        std::vector<GEO::index_t> elements;
        switch (m_context) {
            case Delete_context::vertices: {
                elements.assign(m_selection.vertices.begin(), m_selection.vertices.end());
                break;
            }
            case Delete_context::edges:
            case Delete_context::only_edges_and_faces: {
                for (const std::pair<GEO::index_t, GEO::index_t>& edge : m_selection.edges) {
                    const GEO::index_t edit_edge = m_edit_mesh.find_edge(edge.first, edge.second);
                    if (edit_edge != GEO::NO_INDEX) {
                        elements.push_back(edit_edge);
                    }
                }
                break;
            }
            case Delete_context::faces:
            case Delete_context::only_faces: {
                elements.assign(m_selection.facets.begin(), m_selection.facets.end());
                break;
            }
        }
        m_edit_mesh.delete_elements(elements, m_context);
        emit();
    }

private:
    const Geometry_component_selection& m_selection;
    Delete_context                      m_context;
};

////////////////////////////////////////////////////////////////////////////////

class Dissolve_faces : public Edit_mesh_operation
{
public:
    Dissolve_faces(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>& selected_facets, const Dissolve_faces_options& options)
        : Edit_mesh_operation{source, destination}
        , m_selected_facets  {selected_facets}
        , m_options          {options}
    {
    }

    void build()
    {
        const GEO::index_t facet_slot_count  = m_edit_mesh.get_facet_slot_count();
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();

        std::vector<std::uint8_t> selected(facet_slot_count, 0);
        for (const GEO::index_t facet : m_selected_facets) {
            if (m_edit_mesh.is_facet_alive(facet)) {
                selected[facet] = 1;
            }
        }

        // Edge-connected groups: union through edges with exactly two
        // facets, both selected.
        std::vector<GEO::index_t> parent(facet_slot_count);
        std::iota(parent.begin(), parent.end(), GEO::index_t{0});
        const auto find_root = [&parent](GEO::index_t x) -> GEO::index_t {
            while (parent[x] != x) {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        };
        const GEO::index_t edge_slot_count = m_edit_mesh.get_edge_slot_count();
        for (GEO::index_t edge = 0; edge < edge_slot_count; ++edge) {
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
        std::vector<GEO::index_t>              roots;
        std::vector<std::vector<GEO::index_t>> groups;
        for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
            if (selected[facet] == 0) {
                continue;
            }
            const GEO::index_t root = find_root(facet);
            const std::vector<GEO::index_t>::iterator i = std::find(roots.begin(), roots.end(), root);
            if (i == roots.end()) {
                roots.push_back(root);
                groups.push_back({facet});
            } else {
                groups[static_cast<std::size_t>(i - roots.begin())].push_back(facet);
            }
        }

        std::vector<std::size_t> valence_before(vertex_slot_count);
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            valence_before[vertex] = m_edit_mesh.get_vertex(vertex).edges.size();
        }

        std::vector<GEO::index_t> joined_facets;
        for (const std::vector<GEO::index_t>& group : groups) {
            if (group.size() < 2) {
                continue;
            }
            GEO::index_t      joined = GEO::NO_INDEX;
            const Join_result result = m_edit_mesh.join_facets(group, joined);
            if (result == Join_result::joined) {
                joined_facets.push_back(joined);
            } else {
                log_operation->debug("dissolve_faces: group of {} facets kept (join result {})", group.size(), static_cast<unsigned int>(result));
            }
        }

        if (m_options.dissolve_vertices) {
            std::vector<GEO::index_t> vertices;
            for (const GEO::index_t facet : joined_facets) {
                for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                    vertices.push_back(corner.vertex);
                }
            }
            for (const GEO::index_t vertex : vertices) {
                if (
                    m_edit_mesh.is_vertex_alive(vertex) &&
                    (m_edit_mesh.get_vertex(vertex).edges.size() == 2) &&
                    (valence_before[vertex] > 2)
                ) {
                    static_cast<void>(m_edit_mesh.collapse_vertex(vertex));
                }
            }
        }
        emit();
    }

private:
    const std::set<GEO::index_t>& m_selected_facets;
    Dissolve_faces_options        m_options;
};

////////////////////////////////////////////////////////////////////////////////

class Dissolve_edges : public Edit_mesh_operation
{
public:
    Dissolve_edges(
        const Geometry&                                        source,
        Geometry&                                              destination,
        const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
        const Dissolve_edges_options&                          options
    )
        : Edit_mesh_operation{source, destination}
        , m_selected_edges   {selected_edges}
        , m_options          {options}
    {
    }

    void build()
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        const GEO::index_t edge_slot_count   = m_edit_mesh.get_edge_slot_count();
        const GEO::index_t facet_slot_count  = m_edit_mesh.get_facet_slot_count();

        std::vector<std::uint8_t> edge_selected(edge_slot_count, 0);
        std::vector<GEO::index_t> manifold_edges;
        std::vector<unsigned int> selected_edge_count(vertex_slot_count, 0);
        std::vector<unsigned int> manifold_edge_count(vertex_slot_count, 0);
        for (const std::pair<GEO::index_t, GEO::index_t>& pair : m_selected_edges) {
            const GEO::index_t edge = m_edit_mesh.find_edge(pair.first, pair.second);
            if ((edge == GEO::NO_INDEX) || (edge_selected[edge] != 0)) {
                continue;
            }
            edge_selected[edge] = 1;
            const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
            ++selected_edge_count[edit_edge.vertices[0]];
            ++selected_edge_count[edit_edge.vertices[1]];
            if (edit_edge.facets.size() == 2) {
                manifold_edges.push_back(edge);
                ++manifold_edge_count[edit_edge.vertices[0]];
                ++manifold_edge_count[edit_edge.vertices[1]];
            }
        }

        // A facet is selected when every one of its edges is.
        std::vector<std::uint8_t> facet_selected(facet_slot_count, 0);
        for (GEO::index_t facet = 0; facet < facet_slot_count; ++facet) {
            if (!m_edit_mesh.is_facet_alive(facet)) {
                continue;
            }
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
            const std::size_t n = corners.size();
            bool all = true;
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t edge = m_edit_mesh.find_edge(corners[i].vertex, corners[(i + 1) % n].vertex);
                if ((edge == GEO::NO_INDEX) || (edge_selected[edge] == 0)) {
                    all = false;
                    break;
                }
            }
            facet_selected[facet] = all ? 1 : 0;
        }

        std::vector<std::uint8_t> protected_vertex(vertex_slot_count, 0);
        if (m_options.preserve_quads) {
            for (const GEO::index_t edge : manifold_edges) {
                const std::vector<GEO::index_t>& edge_facets = m_edit_mesh.get_edge(edge).facets;
                if (
                    (m_edit_mesh.get_facet_corners(edge_facets[0]).size() != 3) ||
                    (m_edit_mesh.get_facet_corners(edge_facets[1]).size() != 3)
                ) {
                    continue;
                }
                for (const GEO::index_t facet : edge_facets) {
                    for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                        protected_vertex[corner.vertex] = 1;
                    }
                }
            }
        }

        std::vector<std::size_t> valence_before(vertex_slot_count);
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            valence_before[vertex] = m_edit_mesh.get_vertex(vertex).edges.size();
        }

        // Measured on the facets before any split or join.
        std::vector<std::uint8_t> touches_unselected(vertex_slot_count, 0);
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(vertex)) {
                if (facet_selected[facet] == 0) {
                    touches_unselected[vertex] = 1;
                    break;
                }
            }
        }

        // The vertex rules that do not depend on the joins' outcome.
        const auto is_eligible = [&](const GEO::index_t vertex) -> bool {
            if (m_options.preserve_quads && (protected_vertex[vertex] != 0)) {
                return false;
            }
            return !((selected_edge_count[vertex] > 1) && (touches_unselected[vertex] != 0));
        };

        std::vector<GEO::index_t> candidates;
        for (const GEO::index_t edge : manifold_edges) {
            for (const GEO::index_t vertex : m_edit_mesh.get_edge(edge).vertices) {
                if (std::find(candidates.begin(), candidates.end(), vertex) == candidates.end()) {
                    candidates.push_back(vertex);
                }
            }
        }

        if (m_options.dissolve_vertices && m_options.face_split) {
            // Predicted collapses: the joins take away the vertex's manifold
            // selected edges, leaving two.
            std::vector<std::uint8_t> marked(vertex_slot_count, 0);
            for (const GEO::index_t vertex : candidates) {
                if (((valence_before[vertex] - manifold_edge_count[vertex]) == 2) && (valence_before[vertex] > 2) && is_eligible(vertex)) {
                    marked[vertex] = 1;
                }
            }
            for (const GEO::index_t vertex : candidates) {
                if (marked[vertex] != 0) {
                    split_off_corners(m_edit_mesh, vertex, marked);
                }
            }
        }

        for (const GEO::index_t edge : manifold_edges) {
            if (!m_edit_mesh.is_edge_alive(edge)) {
                continue; // removed by an earlier join of the same facet pair
            }
            GEO::index_t      joined = GEO::NO_INDEX;
            const Join_result result = m_edit_mesh.join_facet_pair(edge, joined);
            if (result != Join_result::joined) {
                log_operation->debug("dissolve_edges: edge kept (join result {})", static_cast<unsigned int>(result));
            }
        }

        if (!m_options.dissolve_vertices) {
            emit();
            return;
        }

        const bool  collapse_all = (m_options.angle_threshold_degrees >= 180.0f);
        const float threshold    = degrees_to_radians(m_options.angle_threshold_degrees);
        std::vector<GEO::index_t> collapses;
        for (const GEO::index_t vertex : candidates) {
            if (
                !m_edit_mesh.is_vertex_alive(vertex) ||
                (m_edit_mesh.get_vertex(vertex).edges.size() != 2) ||
                (valence_before[vertex] <= 2) ||
                !is_eligible(vertex)
            ) {
                continue;
            }
            if (collapse_all || (two_valent_edge_angle(m_edit_mesh, vertex) < threshold)) {
                collapses.push_back(vertex);
            }
        }
        for (const GEO::index_t vertex : collapses) {
            static_cast<void>(m_edit_mesh.collapse_vertex(vertex));
        }
        emit();
    }

private:
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& m_selected_edges;
    Dissolve_edges_options                                 m_options;
};

////////////////////////////////////////////////////////////////////////////////

class Dissolve_vertices : public Edit_mesh_operation
{
public:
    Dissolve_vertices(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>& selected_vertices, const Dissolve_vertices_options& options)
        : Edit_mesh_operation{source, destination}
        , m_selected_vertices{selected_vertices}
        , m_options          {options}
    {
    }

    void build()
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        std::vector<std::uint8_t> marked(vertex_slot_count, 0);
        for (const GEO::index_t vertex : m_selected_vertices) {
            if (m_edit_mesh.is_vertex_alive(vertex)) {
                marked[vertex] = 1;
            }
        }

        std::vector<GEO::index_t> copies;
        for (const GEO::index_t vertex : m_selected_vertices) {
            if (!m_edit_mesh.is_vertex_alive(vertex)) {
                continue;
            }
            if (m_options.boundary_tear && is_boundary(vertex)) {
                const std::span<const GEO::index_t> edge_span = m_edit_mesh.get_vertex_edges(vertex);
                const std::vector<GEO::index_t> edges{edge_span.begin(), edge_span.end()};
                m_edit_mesh.separate_vertex(vertex, edges, copies);
                for (const GEO::index_t copy : copies) {
                    finish_vertex(copy);
                }
                continue;
            }
            if (m_options.face_split) {
                split_off_corners(m_edit_mesh, vertex, marked);
            }
            if (m_edit_mesh.get_vertex(vertex).edges.size() >= 3) {
                join_around(vertex);
            }
            finish_vertex(vertex);
        }
        emit();
    }

private:
    [[nodiscard]] auto is_boundary(const GEO::index_t vertex) const -> bool
    {
        for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
            if (m_edit_mesh.get_edge(edge).facets.size() == 1) {
                return true;
            }
        }
        return false;
    }

    // Joins facet pairs across the vertex's edges until no pair joins.
    void join_around(const GEO::index_t vertex)
    {
        std::vector<GEO::index_t> failed;
        std::vector<GEO::index_t> edges;
        bool progress = true;
        while (progress && m_edit_mesh.is_vertex_alive(vertex)) {
            progress = false;
            const std::span<const GEO::index_t> edge_span = m_edit_mesh.get_vertex_edges(vertex);
            edges.assign(edge_span.begin(), edge_span.end());
            for (const GEO::index_t edge : edges) {
                if (
                    !m_edit_mesh.is_edge_alive(edge) ||
                    (m_edit_mesh.get_edge(edge).facets.size() != 2) ||
                    (std::find(failed.begin(), failed.end(), edge) != failed.end())
                ) {
                    continue;
                }
                GEO::index_t joined = GEO::NO_INDEX;
                if (m_edit_mesh.join_facet_pair(edge, joined) == Join_result::joined) {
                    failed.clear(); // a failed pair may join once its facets changed
                    progress = true;
                    break;
                }
                failed.push_back(edge);
            }
        }
    }

    // Collapses a two-valent vertex, deletes a loose one.
    void finish_vertex(const GEO::index_t vertex)
    {
        if (!m_edit_mesh.is_vertex_alive(vertex)) {
            return;
        }
        const Edit_vertex& edit_vertex = m_edit_mesh.get_vertex(vertex);
        if (edit_vertex.edges.size() == 2) {
            static_cast<void>(m_edit_mesh.collapse_vertex(vertex));
        } else if (edit_vertex.edges.empty() && edit_vertex.facets.empty()) {
            const std::array<GEO::index_t, 1> elements{vertex};
            m_edit_mesh.delete_elements(elements, Delete_context::vertices);
        }
    }

    const std::set<GEO::index_t>& m_selected_vertices;
    Dissolve_vertices_options     m_options;
};

////////////////////////////////////////////////////////////////////////////////

class Dissolve_limited : public Edit_mesh_operation
{
public:
    Dissolve_limited(const Geometry& source, Geometry& destination, const Geometry_component_selection* selection, const Dissolve_limited_options& options)
        : Edit_mesh_operation{source, destination}
        , m_selection        {selection}
        , m_options          {options}
    {
    }

    void build()
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        const GEO::index_t edge_slot_count   = m_edit_mesh.get_edge_slot_count();
        const float        limit             = degrees_to_radians(m_options.angle_limit_degrees);

        m_edge_candidate  .assign(edge_slot_count,   (m_selection == nullptr) ? 1 : 0);
        m_vertex_candidate.assign(vertex_slot_count, (m_selection == nullptr) ? 1 : 0);
        if (m_selection != nullptr) {
            const auto add_edge = [&](const GEO::index_t a, const GEO::index_t b) {
                const GEO::index_t edge = m_edit_mesh.find_edge(a, b);
                if (edge != GEO::NO_INDEX) {
                    m_edge_candidate[edge] = 1;
                    m_vertex_candidate[a]  = 1;
                    m_vertex_candidate[b]  = 1;
                }
            };
            for (const std::pair<GEO::index_t, GEO::index_t>& edge : m_selection->edges) {
                add_edge(edge.first, edge.second);
            }
            for (const GEO::index_t facet : m_selection->facets) {
                if (!m_edit_mesh.is_facet_alive(facet)) {
                    continue;
                }
                const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
                const std::size_t n = corners.size();
                for (std::size_t i = 0; i < n; ++i) {
                    add_edge(corners[i].vertex, corners[(i + 1) % n].vertex);
                }
            }
            for (const GEO::index_t vertex : m_selection->vertices) {
                if (m_edit_mesh.is_vertex_alive(vertex)) {
                    m_vertex_candidate[vertex] = 1;
                }
            }
        }

        using Entry = std::pair<float, GEO::index_t>;
        using Heap  = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;

        // Edge pass
        Heap edge_heap;
        const auto push_edge = [&](const GEO::index_t edge) {
            if (!is_edge_candidate(edge) || is_delimited(edge)) {
                return;
            }
            const float angle = get_dihedral_angle(edge);
            if (angle < limit) {
                edge_heap.emplace(angle, edge);
            }
        };
        for (GEO::index_t edge = 0; edge < edge_slot_count; ++edge) {
            push_edge(edge);
        }
        while (!edge_heap.empty()) {
            const Entry entry = edge_heap.top();
            edge_heap.pop();
            const GEO::index_t edge = entry.second;
            if (!is_edge_candidate(edge) || is_delimited(edge)) {
                continue;
            }
            const float angle = get_dihedral_angle(edge);
            if (angle >= limit) {
                continue;
            }
            if (angle > (entry.first + 1e-6f)) {
                edge_heap.emplace(angle, edge); // re-scored after a join
                continue;
            }
            GEO::index_t joined = GEO::NO_INDEX;
            if (m_edit_mesh.join_facet_pair(edge, joined) != Join_result::joined) {
                continue;
            }
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(joined);
            const std::size_t n = corners.size();
            for (std::size_t i = 0; i < n; ++i) {
                push_edge(m_edit_mesh.find_edge(corners[i].vertex, corners[(i + 1) % n].vertex));
            }
        }

        // Vertex pass
        Heap vertex_heap;
        const auto push_vertex = [&](const GEO::index_t vertex) {
            if (
                (vertex >= m_vertex_candidate.size()) ||
                (m_vertex_candidate[vertex] == 0) ||
                !m_edit_mesh.is_vertex_alive(vertex) ||
                (m_edit_mesh.get_vertex(vertex).edges.size() != 2)
            ) {
                return;
            }
            const float angle = two_valent_edge_angle(m_edit_mesh, vertex);
            if (angle < limit) {
                vertex_heap.emplace(angle, vertex);
            }
        };
        for (GEO::index_t vertex = 0; vertex < vertex_slot_count; ++vertex) {
            push_vertex(vertex);
        }
        while (!vertex_heap.empty()) {
            const Entry entry = vertex_heap.top();
            vertex_heap.pop();
            const GEO::index_t vertex = entry.second;
            if (!m_edit_mesh.is_vertex_alive(vertex) || (m_edit_mesh.get_vertex(vertex).edges.size() != 2)) {
                continue;
            }
            const float angle = two_valent_edge_angle(m_edit_mesh, vertex);
            if (angle >= limit) {
                continue;
            }
            if (angle > (entry.first + 1e-6f)) {
                vertex_heap.emplace(angle, vertex);
                continue;
            }
            const std::span<const GEO::index_t> edges = m_edit_mesh.get_vertex_edges(vertex);
            const std::array<GEO::index_t, 2> neighbours{
                m_edit_mesh.get_edge_other_vertex(edges[0], vertex),
                m_edit_mesh.get_edge_other_vertex(edges[1], vertex)
            };
            if (!m_options.dissolve_boundaries && (is_delimited(edges[0]) || is_delimited(edges[1]))) {
                continue;
            }
            if (!collapse_keeps_corners_convex(vertex)) {
                continue;
            }
            if (m_edit_mesh.collapse_vertex(vertex) != Collapse_result::collapsed) {
                continue;
            }
            for (const GEO::index_t neighbour : neighbours) {
                push_vertex(neighbour);
            }
        }

        emit();
    }

private:
    [[nodiscard]] auto is_edge_candidate(const GEO::index_t edge) const -> bool
    {
        return
            (edge < m_edge_candidate.size()) &&
            (m_edge_candidate[edge] != 0) &&
            m_edit_mesh.is_edge_alive(edge) &&
            (m_edit_mesh.get_edge(edge).facets.size() == 2);
    }

    // Is the edge traversed in the same direction by both of its facets?
    [[nodiscard]] auto has_winding_flip(const GEO::index_t edge) const -> bool
    {
        const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
        if (edit_edge.facets.size() != 2) {
            return false;
        }
        std::array<bool, 2> forward{false, false};
        for (std::size_t f = 0; f < 2; ++f) {
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(edit_edge.facets[f]);
            const std::size_t n = corners.size();
            for (std::size_t i = 0; i < n; ++i) {
                if ((corners[i].vertex == edit_edge.vertices[0]) && (corners[(i + 1) % n].vertex == edit_edge.vertices[1])) {
                    forward[f] = true;
                    break;
                }
            }
        }
        return forward[0] == forward[1];
    }

    [[nodiscard]] auto is_delimited(const GEO::index_t edge) const -> bool
    {
        if (!m_edit_mesh.is_edge_alive(edge)) {
            return false;
        }
        if (m_options.delimit_winding && has_winding_flip(edge)) {
            return true;
        }
        if (m_options.delimit_crease) {
            const std::optional<float> sharpness = m_edit_mesh.get_edge_sharpness(edge);
            if (sharpness.has_value() && (sharpness.value() > 0.0f)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto get_dihedral_angle(const GEO::index_t edge) const -> float
    {
        const std::vector<GEO::index_t>& facets = m_edit_mesh.get_edge(edge).facets;
        return angle_between(facet_normal(m_edit_mesh, facets[0]), facet_normal(m_edit_mesh, facets[1]));
    }

    // Rejects a collapse that would flip the winding of an adjacent corner,
    // leave another vertex of a facet inside the triangle the vertex spans
    // with its two neighbours, or leave a facet with fewer than three corners.
    [[nodiscard]] auto collapse_keeps_corners_convex(const GEO::index_t vertex) const -> bool
    {
        for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(vertex)) {
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
            const std::size_t n = corners.size();
            if (n <= 3) {
                return false;
            }
            const std::size_t i = m_edit_mesh.find_facet_corner(facet, vertex);
            const auto position = [&](const std::size_t offset) -> GEO::vec3f {
                return m_edit_mesh.get_position(corners[(i + n + offset - 2) % n].vertex);
            };
            const GEO::vec3f prev_prev = position(0);
            const GEO::vec3f prev      = position(1);
            const GEO::vec3f current   = position(2);
            const GEO::vec3f next      = position(3);
            const GEO::vec3f next_next = position(4);
            const GEO::vec3f normal    = facet_normal(m_edit_mesh, facet);

            const auto turn = [&normal](const GEO::vec3f& a, const GEO::vec3f& b, const GEO::vec3f& c) -> float {
                const GEO::vec3f ab = b - a;
                const GEO::vec3f bc = c - b;
                const float      scale = GEO::length(ab) * GEO::length(bc);
                const float      value = GEO::dot(GEO::cross(ab, bc), normal);
                const float      epsilon = 1e-6f * scale;
                return (value > epsilon) ? 1.0f : ((value < -epsilon) ? -1.0f : 0.0f);
            };
            if ((turn(prev_prev, prev, current) >= 0.0f) && (turn(prev_prev, prev, next) < 0.0f)) {
                return false;
            }
            if ((turn(current, next, next_next) >= 0.0f) && (turn(prev, next, next_next) < 0.0f)) {
                return false;
            }

            const float orientation = turn(prev, current, next);
            if (orientation == 0.0f) {
                continue; // collinear: the triangle is degenerate and contains nothing
            }
            for (std::size_t k = 0; k < n; ++k) {
                if ((k == i) || (k == ((i + 1) % n)) || (k == ((i + n - 1) % n))) {
                    continue;
                }
                const GEO::vec3f x = m_edit_mesh.get_position(corners[k].vertex);
                if (
                    (turn(prev, current, x) == orientation) &&
                    (turn(current, next, x) == orientation) &&
                    (turn(next, prev, x) == orientation)
                ) {
                    return false;
                }
            }
        }
        return true;
    }

    const Geometry_component_selection* m_selection;
    Dissolve_limited_options            m_options;
    std::vector<std::uint8_t>           m_edge_candidate;
    std::vector<std::uint8_t>           m_vertex_candidate;
};

} // anonymous namespace

void delete_components(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    const Delete_context                context,
    Component_remap*                    remap
)
{
    Delete_components operation{source, destination, selection, context};
    operation.build();
    remap_selection(operation, remap);
}

void dissolve_faces(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    const Dissolve_faces_options  options,
    Component_remap*              remap
)
{
    Dissolve_faces operation{source, destination, selected_facets, options};
    operation.build();
    remap_selection(operation, remap);
}

void dissolve_edges(
    const Geometry&                                        source,
    Geometry&                                              destination,
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
    const Dissolve_edges_options                           options,
    Component_remap*                                       remap
)
{
    Dissolve_edges operation{source, destination, selected_edges, options};
    operation.build();
    remap_selection(operation, remap);
}

void dissolve_vertices(
    const Geometry&                 source,
    Geometry&                       destination,
    const std::set<GEO::index_t>&   selected_vertices,
    const Dissolve_vertices_options options,
    Component_remap*                remap
)
{
    Dissolve_vertices operation{source, destination, selected_vertices, options};
    operation.build();
    remap_selection(operation, remap);
}

void dissolve_limited(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection* selection,
    const Dissolve_limited_options      options,
    Component_remap*                    remap
)
{
    Dissolve_limited operation{source, destination, selection, options};
    operation.build();
    remap_selection(operation, remap);
}

} // namespace erhe::geometry::operation
