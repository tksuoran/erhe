#include "erhe_geometry/operation/split_components.hpp"
#include "erhe_geometry/operation/dissolve.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
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

[[nodiscard]] auto safe_normalize(const GEO::vec3f& v) -> GEO::vec3f
{
    const float length = GEO::length(v);
    return (length > 0.0f) ? (v / length) : GEO::vec3f{0.0f, 0.0f, 0.0f};
}

[[nodiscard]] auto facet_centroid(const Edit_mesh& edit_mesh, const GEO::index_t facet) -> GEO::vec3f
{
    const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
    GEO::vec3f sum{0.0f, 0.0f, 0.0f};
    for (const Edit_corner& corner : corners) {
        sum += edit_mesh.get_position(corner.vertex);
    }
    return corners.empty() ? sum : (sum / static_cast<float>(corners.size()));
}

// The facet of the edge that traverses it from u to v, GEO::NO_INDEX when none.
[[nodiscard]] auto find_directed_facet(const Edit_mesh& edit_mesh, const GEO::index_t edge, const GEO::index_t u, const GEO::index_t v) -> GEO::index_t
{
    for (const GEO::index_t facet : edit_mesh.get_edge(edge).facets) {
        const std::span<const Edit_corner> corners = edit_mesh.get_facet_corners(facet);
        const std::size_t n = corners.size();
        for (std::size_t i = 0; i < n; ++i) {
            if ((corners[i].vertex == u) && (corners[(i + 1) % n].vertex == v)) {
                return facet;
            }
        }
    }
    return GEO::NO_INDEX;
}

// The fans of facets around the vertex (Edit_mesh::separate_vertex() rule): two
// facets are in one fan when they share an edge at the vertex that is not in
// cut_edges. Fans are in the order of their first facet in the vertex's facet
// list.
void compute_fans(
    const Edit_mesh&                        edit_mesh,
    const GEO::index_t                      vertex,
    const std::vector<GEO::index_t>&        cut_edges,
    std::vector<std::vector<GEO::index_t>>& out_fans
)
{
    out_fans.clear();
    const std::span<const GEO::index_t> facets = edit_mesh.get_vertex_facets(vertex);
    const auto facet_index = [&facets](const GEO::index_t facet) -> std::size_t {
        return static_cast<std::size_t>(std::find(facets.begin(), facets.end(), facet) - facets.begin());
    };
    std::vector<std::size_t> parent(facets.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find_root = [&parent](std::size_t x) -> std::size_t {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for (const GEO::index_t edge : edit_mesh.get_vertex_edges(vertex)) {
        if (std::find(cut_edges.begin(), cut_edges.end(), edge) != cut_edges.end()) {
            continue;
        }
        const std::vector<GEO::index_t>& edge_facets = edit_mesh.get_edge(edge).facets;
        for (std::size_t i = 1; i < edge_facets.size(); ++i) {
            const std::size_t root_a = find_root(facet_index(edge_facets[0]));
            const std::size_t root_b = find_root(facet_index(edge_facets[i]));
            if (root_a != root_b) {
                parent[root_a] = root_b;
            }
        }
    }
    std::vector<std::size_t> fan_roots;
    for (std::size_t i = 0; i < facets.size(); ++i) {
        const std::size_t root = find_root(i);
        auto fan_i = std::find(fan_roots.begin(), fan_roots.end(), root);
        if (fan_i == fan_roots.end()) {
            fan_roots.push_back(root);
            out_fans.emplace_back();
            fan_i = std::prev(fan_roots.end());
        }
        out_fans[static_cast<std::size_t>(fan_i - fan_roots.begin())].push_back(facets[i]);
    }
}

void remap_selection(const Geometry_operation& operation, Component_remap* remap)
{
    if ((remap == nullptr) || (remap->source == nullptr) || (remap->destination == nullptr)) {
        return;
    }
    operation.remap_component_selection(*remap->source, *remap->destination);
}

// Which sets a tear's remap fills.
enum class Tear_selection : unsigned int
{
    vertices,
    edges,
    vertices_and_edges
};

[[nodiscard]] auto has_remap(const Component_remap* remap) -> bool
{
    return (remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr);
}

////////////////////////////////////////////////////////////////////////////////

class Split_components : public Edit_mesh_operation
{
public:
    Split_components(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        m_origin.resize(vertex_slot_count);
        std::iota(m_origin.begin(), m_origin.end(), GEO::index_t{0});
        for (GEO::index_t edge = 0, end = m_edit_mesh.get_edge_slot_count(); edge < end; ++edge) {
            if (!m_edit_mesh.is_edge_alive(edge)) {
                continue;
            }
            const std::optional<float> sharpness = m_edit_mesh.get_edge_sharpness(edge);
            if (sharpness.has_value()) {
                const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
                m_sharpness.emplace(make_vertex_pair(edit_edge.vertices[0], edit_edge.vertices[1]), sharpness.value());
            }
        }
    }

    // Region split.
    void build_region(const std::set<GEO::index_t>& facets)
    {
        const GEO::index_t facet_slot_count = m_edit_mesh.get_facet_slot_count();
        std::vector<std::uint8_t> in_region(facet_slot_count, 0);
        std::set<GEO::index_t> region_vertices;
        for (const GEO::index_t facet : facets) {
            if ((facet >= facet_slot_count) || !m_edit_mesh.is_facet_alive(facet)) {
                continue;
            }
            in_region[facet] = 1;
            m_region_facets.push_back(facet);
            for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                region_vertices.insert(corner.vertex);
            }
        }

        std::vector<GEO::index_t> region_facets_at_vertex;
        for (const GEO::index_t vertex : region_vertices) {
            region_facets_at_vertex.clear();
            bool has_outside = false;
            for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(vertex)) {
                if (in_region[facet] != 0) {
                    region_facets_at_vertex.push_back(facet);
                } else {
                    has_outside = true;
                }
            }
            if (!has_outside || region_facets_at_vertex.empty()) {
                continue;
            }
            const GEO::index_t copy = copy_vertex(vertex);
            move_facets(vertex, region_facets_at_vertex, copy);
        }
        restore_sharpness();
        emit();
    }

    void remap_region(Component_remap* remap) const
    {
        if (!has_remap(remap)) {
            return;
        }
        Geometry_component_selection& dst = *remap->destination;
        dst.vertices.clear();
        dst.facets.clear();
        dst.edges.clear();
        const Geometry_component_selection& src = *remap->source;
        const std::vector<GEO::index_t> facet_to_dst = make_facet_to_dst();
        for (const GEO::index_t facet : m_region_facets) {
            if (!src.facets.empty()) {
                dst.facets.insert(facet_to_dst[facet]);
            }
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
            const std::size_t n = corners.size();
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t a = get_emitted_vertex(corners[i].vertex);
                const GEO::index_t b = get_emitted_vertex(corners[(i + 1) % n].vertex);
                if (!src.vertices.empty()) {
                    dst.vertices.insert(a);
                }
                if (!src.edges.empty()) {
                    dst.edges.insert(make_vertex_pair(a, b));
                }
            }
        }
    }

    // Tear (rip and edge split).
    void build_tear(const std::set<GEO::index_t>& torn_vertices, const std::set<Vertex_pair>& requested_cut_pairs, const GEO::vec3f& direction)
    {
        // The cut pairs that name an edge, and their adjacency.
        std::map<GEO::index_t, std::vector<GEO::index_t>> adjacency;
        for (const Vertex_pair& pair : requested_cut_pairs) {
            if ((pair.first >= m_edit_mesh.get_vertex_slot_count()) || (pair.second >= m_edit_mesh.get_vertex_slot_count())) {
                continue;
            }
            if (m_edit_mesh.find_edge(pair.first, pair.second) == GEO::NO_INDEX) {
                continue;
            }
            m_cut_pairs.insert(pair);
            adjacency[pair.first].push_back(pair.second);
            adjacency[pair.second].push_back(pair.first);
        }
        for (std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            std::sort(entry.second.begin(), entry.second.end());
        }

        // Orientation: walks from the lowest end vertex (one cut edge) of each
        // run, then from the lowest vertex of each closed run left.
        std::map<Vertex_pair, Vertex_pair> oriented; // canonical pair -> (from, to)
        const auto walk = [&](const GEO::index_t start) {
            std::vector<GEO::index_t> stack{start};
            while (!stack.empty()) {
                const GEO::index_t vertex = stack.back();
                bool advanced = false;
                for (const GEO::index_t other : adjacency[vertex]) {
                    const Vertex_pair key = make_vertex_pair(vertex, other);
                    if (oriented.contains(key)) {
                        continue;
                    }
                    oriented.emplace(key, Vertex_pair{vertex, other});
                    stack.push_back(other);
                    advanced = true;
                    break;
                }
                if (!advanced) {
                    stack.pop_back();
                }
            }
        };
        const auto has_unoriented = [&](const GEO::index_t vertex) -> bool {
            for (const GEO::index_t other : adjacency[vertex]) {
                if (!oriented.contains(make_vertex_pair(vertex, other))) {
                    return true;
                }
            }
            return false;
        };
        for (std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            if ((entry.second.size() == 1) && has_unoriented(entry.first)) {
                walk(entry.first);
            }
        }
        for (std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            if (has_unoriented(entry.first)) {
                walk(entry.first);
            }
        }

        // With a direction, each connected run rips the side lying toward it.
        if (GEO::length(direction) > 0.0f) {
            std::map<GEO::index_t, GEO::index_t> parent;
            for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
                parent[entry.first] = entry.first;
            }
            const auto find_root = [&parent](GEO::index_t x) -> GEO::index_t {
                while (parent[x] != x) {
                    parent[x] = parent[parent[x]];
                    x = parent[x];
                }
                return x;
            };
            for (const Vertex_pair& pair : m_cut_pairs) {
                const GEO::index_t root_a = find_root(pair.first);
                const GEO::index_t root_b = find_root(pair.second);
                if (root_a != root_b) {
                    parent[root_a] = root_b;
                }
            }
            std::map<GEO::index_t, float> score;
            for (const std::pair<const Vertex_pair, Vertex_pair>& entry : oriented) {
                const GEO::index_t edge  = m_edit_mesh.find_edge(entry.first.first, entry.first.second);
                const GEO::index_t facet = find_directed_facet(m_edit_mesh, edge, entry.second.first, entry.second.second);
                if (facet == GEO::NO_INDEX) {
                    continue;
                }
                const GEO::vec3f midpoint = 0.5f * (m_edit_mesh.get_position(entry.first.first) + m_edit_mesh.get_position(entry.first.second));
                score[find_root(entry.first.first)] += GEO::dot(facet_centroid(m_edit_mesh, facet) - midpoint, direction);
            }
            for (std::pair<const Vertex_pair, Vertex_pair>& entry : oriented) {
                if (score[find_root(entry.first.first)] < 0.0f) {
                    std::swap(entry.second.first, entry.second.second);
                }
            }
        }

        // Per torn vertex, on the unmodified topology: the ripped-side facets
        // of its cut edges and the preferred direction for extra cuts.
        class Torn_vertex
        {
        public:
            GEO::index_t              vertex{GEO::NO_INDEX};
            std::vector<GEO::index_t> side_facets;
            GEO::vec3f                preferred{0.0f, 0.0f, 0.0f};
            bool                      has_cut_edges{false};
        };
        std::vector<Torn_vertex> torn;
        for (const GEO::index_t vertex : torn_vertices) {
            if ((vertex >= m_edit_mesh.get_vertex_slot_count()) || !m_edit_mesh.is_vertex_alive(vertex)) {
                continue;
            }
            Torn_vertex entry{};
            entry.vertex = vertex;
            const GEO::vec3f position = m_edit_mesh.get_position(vertex);
            const auto adjacency_i = adjacency.find(vertex);
            if (adjacency_i != adjacency.end()) {
                entry.has_cut_edges = true;
                for (const GEO::index_t other : adjacency_i->second) {
                    const Vertex_pair  key   = make_vertex_pair(vertex, other);
                    const Vertex_pair& from_to = oriented.at(key);
                    const GEO::index_t edge  = m_edit_mesh.find_edge(key.first, key.second);
                    const GEO::index_t facet = find_directed_facet(m_edit_mesh, edge, from_to.first, from_to.second);
                    if (facet != GEO::NO_INDEX) {
                        entry.side_facets.push_back(facet);
                    }
                }
                if (adjacency_i->second.size() == 1) {
                    // A run end: continue the tear straight through.
                    entry.preferred = safe_normalize(position - m_edit_mesh.get_position(adjacency_i->second.front()));
                }
            } else {
                GEO::vec3f preferred = direction;
                if (GEO::length(preferred) == 0.0f) {
                    const std::span<const GEO::index_t> facets = m_edit_mesh.get_vertex_facets(vertex);
                    if (!facets.empty()) {
                        preferred = facet_centroid(m_edit_mesh, facets.front()) - position;
                    }
                }
                entry.preferred = safe_normalize(preferred);
            }
            torn.push_back(std::move(entry));
        }

        std::vector<GEO::index_t>              cut_edges;
        std::vector<std::vector<GEO::index_t>> fans;
        for (const Torn_vertex& entry : torn) {
            const GEO::index_t vertex   = entry.vertex;
            const GEO::vec3f   position = m_edit_mesh.get_position(vertex);
            std::vector<GEO::index_t>& ripped = m_ripped[vertex];

            // The cut at the vertex: the edges whose origin pair is cut (an
            // earlier tear may have duplicated them).
            cut_edges.clear();
            for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
                const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
                if (m_cut_pairs.contains(make_vertex_pair(m_origin[edit_edge.vertices[0]], m_origin[edit_edge.vertices[1]]))) {
                    cut_edges.push_back(edge);
                }
            }
            compute_fans(m_edit_mesh, vertex, cut_edges, fans);
            while (fans.size() < 2) {
                GEO::index_t best_edge = GEO::NO_INDEX;
                float        best_dot  = -std::numeric_limits<float>::max();
                for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
                    if (m_edit_mesh.get_edge_facet_count(edge) < 2) {
                        continue;
                    }
                    if (std::find(cut_edges.begin(), cut_edges.end(), edge) != cut_edges.end()) {
                        continue;
                    }
                    const GEO::index_t other = m_edit_mesh.get_edge_other_vertex(edge, vertex);
                    const float        dot   = GEO::dot(safe_normalize(m_edit_mesh.get_position(other) - position), entry.preferred);
                    if ((best_edge == GEO::NO_INDEX) || (dot > best_dot)) {
                        best_edge = edge;
                        best_dot  = dot;
                    }
                }
                if (best_edge == GEO::NO_INDEX) {
                    break;
                }
                cut_edges.push_back(best_edge);
                compute_fans(m_edit_mesh, vertex, cut_edges, fans);
            }
            if (fans.size() < 2) {
                ripped.push_back(vertex);
                continue;
            }

            std::vector<std::uint8_t> is_ripped(fans.size(), 0);
            if (entry.has_cut_edges) {
                for (std::size_t fan = 0; fan < fans.size(); ++fan) {
                    for (const GEO::index_t facet : fans[fan]) {
                        if (std::find(entry.side_facets.begin(), entry.side_facets.end(), facet) != entry.side_facets.end()) {
                            is_ripped[fan] = 1;
                        }
                    }
                }
            } else {
                std::size_t best_fan = 0;
                float       best_dot = -std::numeric_limits<float>::max();
                for (std::size_t fan = 0; fan < fans.size(); ++fan) {
                    for (const GEO::index_t facet : fans[fan]) {
                        const float dot = GEO::dot(safe_normalize(facet_centroid(m_edit_mesh, facet) - position), entry.preferred);
                        if (dot > best_dot) {
                            best_dot = dot;
                            best_fan = fan;
                        }
                    }
                }
                is_ripped[best_fan] = 1;
            }

            std::size_t keeper = 0;
            for (std::size_t fan = 0; fan < fans.size(); ++fan) {
                if (is_ripped[fan] == 0) {
                    keeper = fan;
                    break;
                }
            }
            for (std::size_t fan = 0; fan < fans.size(); ++fan) {
                if (fan == keeper) {
                    if (is_ripped[fan] != 0) {
                        ripped.push_back(vertex);
                    }
                    continue;
                }
                const GEO::index_t copy = copy_vertex(vertex);
                move_facets(vertex, fans[fan], copy);
                if (is_ripped[fan] != 0) {
                    ripped.push_back(copy);
                }
            }
            if (ripped.empty()) {
                ripped.push_back(vertex);
            }
        }
        restore_sharpness();
        emit();
    }

    // Selects the ripped vertices (vertices) and the ripped copies of the cut
    // edges (edges).
    void remap_tear(Component_remap* remap, const Tear_selection tear_selection) const
    {
        const bool select_vertices = (tear_selection == Tear_selection::vertices) || (tear_selection == Tear_selection::vertices_and_edges);
        const bool select_edges    = (tear_selection == Tear_selection::edges)    || (tear_selection == Tear_selection::vertices_and_edges);
        if (!has_remap(remap)) {
            return;
        }
        Geometry_component_selection& dst = *remap->destination;
        dst.vertices.clear();
        dst.facets.clear();
        dst.edges.clear();
        if (select_vertices) {
            for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : m_ripped) {
                for (const GEO::index_t vertex : entry.second) {
                    const GEO::index_t dst_vertex = get_emitted_vertex(vertex);
                    if (dst_vertex != GEO::NO_INDEX) {
                        dst.vertices.insert(dst_vertex);
                    }
                }
            }
        }
        if (select_edges) {
            const auto ripped_of = [this](const GEO::index_t vertex) -> std::vector<GEO::index_t> {
                const auto i = m_ripped.find(vertex);
                return (i != m_ripped.end()) ? i->second : std::vector<GEO::index_t>{vertex};
            };
            for (const Vertex_pair& pair : m_cut_pairs) {
                for (const GEO::index_t a : ripped_of(pair.first)) {
                    for (const GEO::index_t b : ripped_of(pair.second)) {
                        const GEO::index_t edge = m_edit_mesh.find_edge(a, b);
                        if ((edge == GEO::NO_INDEX) || (m_edit_mesh.get_edge_facet_count(edge) == 0)) {
                            continue;
                        }
                        dst.edges.insert(make_vertex_pair(get_emitted_vertex(a), get_emitted_vertex(b)));
                    }
                }
            }
        }
    }

private:
    [[nodiscard]] auto copy_vertex(const GEO::index_t vertex) -> GEO::index_t
    {
        const std::vector<Edit_source> sources  = m_edit_mesh.get_vertex(vertex).sources;
        const GEO::vec3f               position = m_edit_mesh.get_position(vertex); // copies: add_vertex() may reallocate
        const GEO::index_t             copy     = m_edit_mesh.add_vertex(position, sources);
        if (m_origin.size() <= copy) {
            m_origin.resize(static_cast<std::size_t>(copy) + 1, GEO::NO_INDEX);
        }
        m_origin[copy] = m_origin[vertex];
        return copy;
    }

    // Moves the facets' corners at vertex onto new_vertex.
    void move_facets(const GEO::index_t vertex, const std::span<const GEO::index_t> facets, const GEO::index_t new_vertex)
    {
        std::vector<GEO::index_t> vertices;
        for (const GEO::index_t facet : facets) {
            vertices.clear();
            for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                vertices.push_back((corner.vertex == vertex) ? new_vertex : corner.vertex);
            }
            m_edit_mesh.set_facet_vertices(facet, vertices);
        }
    }

    // A duplicated edge takes the sharpness of the source edge it copies.
    void restore_sharpness()
    {
        if (m_sharpness.empty()) {
            return;
        }
        for (GEO::index_t edge = 0, end = m_edit_mesh.get_edge_slot_count(); edge < end; ++edge) {
            if (!m_edit_mesh.is_edge_alive(edge) || m_edit_mesh.get_edge_sharpness(edge).has_value()) {
                continue;
            }
            const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
            const auto i = m_sharpness.find(make_vertex_pair(m_origin[edit_edge.vertices[0]], m_origin[edit_edge.vertices[1]]));
            if (i != m_sharpness.end()) {
                m_edit_mesh.set_edge_sharpness(edge, i->second);
            }
        }
    }

    // emit() writes the live facets in slot order.
    [[nodiscard]] auto make_facet_to_dst() const -> std::vector<GEO::index_t>
    {
        std::vector<GEO::index_t> facet_to_dst(m_edit_mesh.get_facet_slot_count(), GEO::NO_INDEX);
        GEO::index_t next = 0;
        for (GEO::index_t facet = 0, end = m_edit_mesh.get_facet_slot_count(); facet < end; ++facet) {
            if (m_edit_mesh.is_facet_alive(facet)) {
                facet_to_dst[facet] = next++;
            }
        }
        return facet_to_dst;
    }

    std::vector<GEO::index_t>                         m_origin;        // scratch vertex -> source vertex it copies
    std::map<Vertex_pair, float>                      m_sharpness;     // source edge sharpness
    std::vector<GEO::index_t>                         m_region_facets;
    std::set<Vertex_pair>                             m_cut_pairs;
    std::map<GEO::index_t, std::vector<GEO::index_t>> m_ripped;        // torn vertex -> its ripped vertices
};

class Extract_facets : public Edit_mesh_operation
{
public:
    Extract_facets(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
    }

    void build(const std::set<GEO::index_t>& selected_facets)
    {
        std::vector<GEO::index_t> others;
        for (GEO::index_t facet = 0, end = m_edit_mesh.get_facet_slot_count(); facet < end; ++facet) {
            if (m_edit_mesh.is_facet_alive(facet) && !selected_facets.contains(facet)) {
                others.push_back(facet);
            }
        }
        m_edit_mesh.delete_elements(others, Delete_context::faces);
        emit();
    }
};

} // anonymous namespace

void get_selection_facets(
    const Geometry&                     source,
    const Geometry_component_selection& selection,
    std::set<GEO::index_t>&             out_facets
)
{
    out_facets.clear();
    const GEO::Mesh& mesh = source.get_mesh();
    for (const GEO::index_t facet : mesh.facets) {
        if (selection.facets.contains(facet)) {
            out_facets.insert(facet);
            continue;
        }
        const GEO::index_t n = mesh.facets.nb_vertices(facet);
        if (n == 0) {
            continue;
        }
        bool all_vertices = !selection.vertices.empty();
        bool all_edges    = !selection.edges.empty();
        for (GEO::index_t i = 0; i < n; ++i) {
            const GEO::index_t a = mesh.facets.vertex(facet, i);
            const GEO::index_t b = mesh.facets.vertex(facet, (i + 1) % n);
            if (all_vertices && !selection.vertices.contains(a)) {
                all_vertices = false;
            }
            if (all_edges && !selection.edges.contains(make_vertex_pair(a, b))) {
                all_edges = false;
            }
        }
        if (all_vertices || all_edges) {
            out_facets.insert(facet);
        }
    }
}

void split_facets(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Component_remap*                    remap
)
{
    std::set<GEO::index_t> facets;
    get_selection_facets(source, selection, facets);
    Split_components operation{source, destination};
    operation.build_region(facets);
    operation.remap_region(remap);
}

void split_edges(
    const Geometry&                                        source,
    Geometry&                                              destination,
    const std::set<std::pair<GEO::index_t, GEO::index_t>>& selected_edges,
    Component_remap*                                       remap
)
{
    std::set<GEO::index_t> torn_vertices;
    std::set<Vertex_pair>  cut_pairs;
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : selected_edges) {
        torn_vertices.insert(edge.first);
        torn_vertices.insert(edge.second);
        cut_pairs.insert(make_vertex_pair(edge.first, edge.second));
    }
    Split_components operation{source, destination};
    operation.build_tear(torn_vertices, cut_pairs, GEO::vec3f{0.0f, 0.0f, 0.0f});
    const bool source_vertices = has_remap(remap) && !remap->source->vertices.empty();
    operation.remap_tear(remap, source_vertices ? Tear_selection::vertices_and_edges : Tear_selection::edges);
}

void rip_vertices(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    const Rip_options                   options,
    Component_remap*                    remap
)
{
    std::set<GEO::index_t> torn_vertices;
    std::set<Vertex_pair>  cut_pairs;
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : selection.edges) {
        cut_pairs.insert(make_vertex_pair(edge.first, edge.second));
    }
    if (!selection.vertices.empty()) {
        torn_vertices = selection.vertices;
        const GEO::Mesh& mesh = source.get_mesh();
        for (const GEO::index_t facet : mesh.facets) {
            const GEO::index_t n = mesh.facets.nb_vertices(facet);
            for (GEO::index_t i = 0; i < n; ++i) {
                const GEO::index_t a = mesh.facets.vertex(facet, i);
                const GEO::index_t b = mesh.facets.vertex(facet, (i + 1) % n);
                if (selection.vertices.contains(a) && selection.vertices.contains(b)) {
                    cut_pairs.insert(make_vertex_pair(a, b));
                }
            }
        }
    } else {
        for (const std::pair<GEO::index_t, GEO::index_t>& edge : selection.edges) {
            torn_vertices.insert(edge.first);
            torn_vertices.insert(edge.second);
        }
    }
    Split_components operation{source, destination};
    operation.build_tear(torn_vertices, cut_pairs, options.direction);
    const bool source_vertices = has_remap(remap) && !remap->source->vertices.empty();
    const bool source_edges    = has_remap(remap) && !remap->source->edges.empty();
    const Tear_selection tear_selection =
        (source_vertices && source_edges) ? Tear_selection::vertices_and_edges :
        source_edges                      ? Tear_selection::edges              :
                                            Tear_selection::vertices;
    operation.remap_tear(remap, tear_selection);
}

void extract_facets(
    const Geometry&               source,
    Geometry&                     destination_kept,
    Geometry&                     destination_extracted,
    const std::set<GEO::index_t>& selected_facets,
    Component_remap*              kept_remap,
    Component_remap*              extracted_remap
)
{
    Geometry_component_selection selection{};
    selection.facets = selected_facets;
    delete_components(source, destination_kept, selection, Delete_context::faces, kept_remap);

    Extract_facets operation{source, destination_extracted};
    operation.build(selected_facets);
    remap_selection(operation, extracted_remap);
}

} // namespace erhe::geometry::operation
