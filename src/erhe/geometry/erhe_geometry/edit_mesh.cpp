#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_verify/verify.hpp"

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace erhe::geometry {

namespace {

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

void erase_value(std::vector<GEO::index_t>& values, const GEO::index_t value)
{
    const std::vector<GEO::index_t>::iterator i = std::find(values.begin(), values.end(), value);
    if (i != values.end()) {
        values.erase(i);
    }
}

void push_unique(std::vector<GEO::index_t>& values, const GEO::index_t value)
{
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

// Proper crossing of 2D segments (p0, p1) and (q0, q1).
auto segments_cross(const GEO::vec2f& p0, const GEO::vec2f& p1, const GEO::vec2f& q0, const GEO::vec2f& q1) -> bool
{
    const auto orient = [](const GEO::vec2f& a, const GEO::vec2f& b, const GEO::vec2f& c) -> float {
        return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
    };
    const float o1 = orient(p0, p1, q0);
    const float o2 = orient(p0, p1, q1);
    const float o3 = orient(q0, q1, p0);
    const float o4 = orient(q0, q1, p1);
    return (((o1 > 0.0f) && (o2 < 0.0f)) || ((o1 < 0.0f) && (o2 > 0.0f))) &&
           (((o3 > 0.0f) && (o4 < 0.0f)) || ((o3 < 0.0f) && (o4 > 0.0f)));
}

} // anonymous namespace

void compute_mean_value_weights(
    const std::span<const GEO::vec3f> polygon,
    const GEO::vec3f&                 normal,
    const GEO::vec3f&                 p,
    std::vector<float>&               out_weights
)
{
    const std::size_t n = polygon.size();
    out_weights.assign(n, 0.0f);
    std::vector<GEO::vec3f> d(n);
    std::vector<float>      r(n);
    for (std::size_t i = 0; i < n; ++i) {
        d[i] = polygon[i] - p;
        r[i] = GEO::length(d[i]);
        if (r[i] < 1e-7f) {
            out_weights[i] = 1.0f;
            return;
        }
    }
    std::vector<float> tan_half(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j       = (i + 1) % n;
        const float       sin_rr  = GEO::dot(GEO::cross(d[i], d[j]), normal);
        const float       cos_rr  = GEO::dot(d[i], d[j]);
        const float       rr      = r[i] * r[j];
        if ((std::abs(sin_rr) <= (1e-6f * rr)) && (cos_rr < 0.0f)) {
            // p on edge (i, j): linear weights
            const float length = r[i] + r[j];
            out_weights.assign(n, 0.0f);
            out_weights[i] = r[j] / length;
            out_weights[j] = r[i] / length;
            return;
        }
        tan_half[i] = sin_rr / (rr + cos_rr);
    }
    float sum = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t prev = (i + n - 1) % n;
        out_weights[i] = (tan_half[prev] + tan_half[i]) / r[i];
        sum += out_weights[i];
    }
    if (sum != 0.0f) {
        for (float& w : out_weights) {
            w /= sum;
        }
    }
}

auto compute_newell_normal(const std::span<const GEO::vec3f> polygon) -> GEO::vec3f
{
    GEO::vec3f normal{0.0f, 0.0f, 0.0f};
    const std::size_t n = polygon.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::vec3f& a = polygon[i];
        const GEO::vec3f& b = polygon[(i + 1) % n];
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    const float length = GEO::length(normal);
    return (length > 0.0f) ? (normal / length) : GEO::vec3f{0.0f, 0.0f, 1.0f};
}

auto Edit_mesh::make_edge_key(const GEO::index_t vertex_a, const GEO::index_t vertex_b) -> std::uint64_t
{
    const GEO::index_t lo = std::min(vertex_a, vertex_b);
    const GEO::index_t hi = std::max(vertex_a, vertex_b);
    return (static_cast<std::uint64_t>(lo) << 32u) | static_cast<std::uint64_t>(hi);
}

void Edit_mesh::clear()
{
    m_vertices.clear();
    m_facets.clear();
    m_edges.clear();
    m_edge_map.clear();
    m_live_vertex_count = 0;
    m_live_edge_count   = 0;
    m_live_facet_count  = 0;
}

void Edit_mesh::load(const Geometry& source)
{
    ERHE_VERIFY(source.has_edge_connectivity());
    clear();

    const GEO::Mesh& mesh = source.get_mesh();
    const GEO::index_t vertex_count = mesh.vertices.nb();
    const GEO::index_t facet_count  = mesh.facets.nb();

    m_vertices.resize(vertex_count);
    for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
        Edit_vertex& edit_vertex = m_vertices[vertex];
        edit_vertex.position = get_pointf(mesh.vertices, vertex);
        edit_vertex.sources.emplace_back(1.0f, vertex);
    }
    m_live_vertex_count = vertex_count;

    m_facets.resize(facet_count);
    for (GEO::index_t facet = 0; facet < facet_count; ++facet) {
        Edit_facet& edit_facet = m_facets[facet];
        edit_facet.source_facet = facet;
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        edit_facet.corners.resize(corner_count);
        for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
            const GEO::index_t corner = mesh.facets.corner(facet, local_corner);
            Edit_corner& edit_corner = edit_facet.corners[local_corner];
            edit_corner.vertex = mesh.facet_corners.vertex(corner);
            edit_corner.sources.emplace_back(1.0f, corner);
        }
        link_facet(facet);
    }
    m_live_facet_count = facet_count;

    const Mesh_attributes& attributes = source.get_attributes();
    for (const GEO::index_t edge : mesh.edges) {
        const GEO::index_t   a         = mesh.edges.vertex(edge, 0);
        const GEO::index_t   b         = mesh.edges.vertex(edge, 1);
        const GEO::index_t   edit_edge = ensure_edge(a, b);
        const std::optional<float> sharpness = attributes.edge_sharpness.try_get(edge);
        if (sharpness.has_value()) {
            m_edges[edit_edge].sharpness = sharpness;
        }
    }
}

auto Edit_mesh::get_vertex_count() const -> GEO::index_t { return m_live_vertex_count; }
auto Edit_mesh::get_edge_count  () const -> GEO::index_t { return m_live_edge_count; }
auto Edit_mesh::get_facet_count () const -> GEO::index_t { return m_live_facet_count; }

auto Edit_mesh::get_vertex_slot_count() const -> GEO::index_t { return static_cast<GEO::index_t>(m_vertices.size()); }
auto Edit_mesh::get_edge_slot_count  () const -> GEO::index_t { return static_cast<GEO::index_t>(m_edges.size()); }
auto Edit_mesh::get_facet_slot_count () const -> GEO::index_t { return static_cast<GEO::index_t>(m_facets.size()); }

auto Edit_mesh::is_vertex_alive(const GEO::index_t vertex) const -> bool
{
    return (vertex < m_vertices.size()) && !m_vertices[vertex].deleted;
}

auto Edit_mesh::is_edge_alive(const GEO::index_t edge) const -> bool
{
    return (edge < m_edges.size()) && !m_edges[edge].deleted;
}

auto Edit_mesh::is_facet_alive(const GEO::index_t facet) const -> bool
{
    return (facet < m_facets.size()) && !m_facets[facet].deleted;
}

auto Edit_mesh::get_vertex(const GEO::index_t vertex) const -> const Edit_vertex& { return m_vertices[vertex]; }
auto Edit_mesh::get_edge  (const GEO::index_t edge  ) const -> const Edit_edge&   { return m_edges[edge]; }
auto Edit_mesh::get_facet (const GEO::index_t facet ) const -> const Edit_facet&  { return m_facets[facet]; }

auto Edit_mesh::get_position(const GEO::index_t vertex) const -> GEO::vec3f
{
    return m_vertices[vertex].position;
}

void Edit_mesh::set_position(const GEO::index_t vertex, const GEO::vec3f& position)
{
    m_vertices[vertex].position = position;
}

void Edit_mesh::set_vertex_sources(const GEO::index_t vertex, const std::span<const Edit_source> sources)
{
    m_vertices[vertex].sources.assign(sources.begin(), sources.end());
}

auto Edit_mesh::find_edge(const GEO::index_t vertex_a, const GEO::index_t vertex_b) const -> GEO::index_t
{
    const std::unordered_map<std::uint64_t, GEO::index_t>::const_iterator i = m_edge_map.find(make_edge_key(vertex_a, vertex_b));
    return (i != m_edge_map.end()) ? i->second : GEO::NO_INDEX;
}

auto Edit_mesh::get_facet_corners(const GEO::index_t facet) const -> std::span<const Edit_corner>
{
    return m_facets[facet].corners;
}

auto Edit_mesh::get_vertex_facets(const GEO::index_t vertex) const -> std::span<const GEO::index_t>
{
    return m_vertices[vertex].facets;
}

auto Edit_mesh::get_vertex_edges(const GEO::index_t vertex) const -> std::span<const GEO::index_t>
{
    return m_vertices[vertex].edges;
}

auto Edit_mesh::get_edge_facet_count(const GEO::index_t edge) const -> std::size_t
{
    return m_edges[edge].facets.size();
}

auto Edit_mesh::get_edge_other_vertex(const GEO::index_t edge, const GEO::index_t vertex) const -> GEO::index_t
{
    const Edit_edge& edit_edge = m_edges[edge];
    if (edit_edge.vertices[0] == vertex) {
        return edit_edge.vertices[1];
    }
    if (edit_edge.vertices[1] == vertex) {
        return edit_edge.vertices[0];
    }
    return GEO::NO_INDEX;
}

auto Edit_mesh::find_facet_corner(const GEO::index_t facet, const GEO::index_t vertex) const -> GEO::index_t
{
    const std::vector<Edit_corner>& corners = m_facets[facet].corners;
    for (std::size_t i = 0, end = corners.size(); i < end; ++i) {
        if (corners[i].vertex == vertex) {
            return static_cast<GEO::index_t>(i);
        }
    }
    return GEO::NO_INDEX;
}

auto Edit_mesh::are_adjacent_in_facet(const GEO::index_t facet, const GEO::index_t vertex_a, const GEO::index_t vertex_b) const -> bool
{
    const std::vector<Edit_corner>& corners = m_facets[facet].corners;
    const std::size_t n = corners.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::index_t u = corners[i].vertex;
        const GEO::index_t v = corners[(i + 1) % n].vertex;
        if (((u == vertex_a) && (v == vertex_b)) || ((u == vertex_b) && (v == vertex_a))) {
            return true;
        }
    }
    return false;
}

auto Edit_mesh::get_edge_sharpness(const GEO::index_t edge) const -> std::optional<float>
{
    return m_edges[edge].sharpness;
}

void Edit_mesh::set_edge_sharpness(const GEO::index_t edge, const std::optional<float> sharpness)
{
    m_edges[edge].sharpness = sharpness;
}

auto Edit_mesh::ensure_edge(const GEO::index_t vertex_a, const GEO::index_t vertex_b) -> GEO::index_t
{
    ERHE_VERIFY(vertex_a != vertex_b);
    const std::uint64_t key = make_edge_key(vertex_a, vertex_b);
    const std::unordered_map<std::uint64_t, GEO::index_t>::const_iterator i = m_edge_map.find(key);
    if (i != m_edge_map.end()) {
        return i->second;
    }
    const GEO::index_t edge = static_cast<GEO::index_t>(m_edges.size());
    Edit_edge& edit_edge = m_edges.emplace_back();
    edit_edge.vertices = {std::min(vertex_a, vertex_b), std::max(vertex_a, vertex_b)};
    m_edge_map.emplace(key, edge);
    m_vertices[vertex_a].edges.push_back(edge);
    m_vertices[vertex_b].edges.push_back(edge);
    ++m_live_edge_count;
    return edge;
}

void Edit_mesh::remove_edge(const GEO::index_t edge)
{
    Edit_edge& edit_edge = m_edges[edge];
    ERHE_VERIFY(!edit_edge.deleted);
    ERHE_VERIFY(edit_edge.facets.empty());
    m_edge_map.erase(make_edge_key(edit_edge.vertices[0], edit_edge.vertices[1]));
    erase_value(m_vertices[edit_edge.vertices[0]].edges, edge);
    erase_value(m_vertices[edit_edge.vertices[1]].edges, edge);
    edit_edge.deleted = true;
    --m_live_edge_count;
}

void Edit_mesh::link_facet(const GEO::index_t facet)
{
    const std::size_t n = m_facets[facet].corners.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::index_t u = m_facets[facet].corners[i].vertex;
        const GEO::index_t v = m_facets[facet].corners[(i + 1) % n].vertex;
        push_unique(m_vertices[u].facets, facet);
        const GEO::index_t edge = ensure_edge(u, v);
        push_unique(m_edges[edge].facets, facet);
    }
}

void Edit_mesh::unlink_facet(const GEO::index_t facet)
{
    const std::vector<Edit_corner>& corners = m_facets[facet].corners;
    const std::size_t n = corners.size();
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::index_t u = corners[i].vertex;
        const GEO::index_t v = corners[(i + 1) % n].vertex;
        erase_value(m_vertices[u].facets, facet);
        if (u == v) {
            continue;
        }
        const GEO::index_t edge = find_edge(u, v);
        if (edge != GEO::NO_INDEX) {
            erase_value(m_edges[edge].facets, facet);
        }
    }
}

auto Edit_mesh::make_facet(std::vector<Edit_corner>&& corners, const GEO::index_t source_facet) -> GEO::index_t
{
    const GEO::index_t facet = static_cast<GEO::index_t>(m_facets.size());
    Edit_facet& edit_facet = m_facets.emplace_back();
    edit_facet.corners      = std::move(corners);
    edit_facet.source_facet = source_facet;
    ++m_live_facet_count;
    link_facet(facet);
    return facet;
}

void Edit_mesh::kill_facet(const GEO::index_t facet)
{
    unlink_facet(facet);
    kill_unlinked_facet(facet);
}

void Edit_mesh::kill_unlinked_facet(const GEO::index_t facet)
{
    Edit_facet& edit_facet = m_facets[facet];
    ERHE_VERIFY(!edit_facet.deleted);
    edit_facet.deleted = true;
    --m_live_facet_count;
}

void Edit_mesh::kill_vertex(const GEO::index_t vertex)
{
    Edit_vertex& edit_vertex = m_vertices[vertex];
    ERHE_VERIFY(!edit_vertex.deleted);
    ERHE_VERIFY(edit_vertex.edges.empty());
    ERHE_VERIFY(edit_vertex.facets.empty());
    edit_vertex.deleted = true;
    --m_live_vertex_count;
}

auto Edit_mesh::has_facet_with_vertex_set(const std::span<const Edit_corner> corners, const GEO::index_t ignore_facet) const -> bool
{
    if (corners.empty()) {
        return false;
    }
    for (const GEO::index_t candidate : m_vertices[corners.front().vertex].facets) {
        if ((candidate == ignore_facet) || m_facets[candidate].deleted) {
            continue;
        }
        const std::vector<Edit_corner>& candidate_corners = m_facets[candidate].corners;
        if (candidate_corners.size() != corners.size()) {
            continue;
        }
        bool same = true;
        for (const Edit_corner& corner : corners) {
            if (find_facet_corner(candidate, corner.vertex) == GEO::NO_INDEX) {
                same = false;
                break;
            }
        }
        if (same) {
            return true;
        }
    }
    return false;
}

auto Edit_mesh::add_vertex(const GEO::vec3f& position, const std::span<const Edit_source> sources) -> GEO::index_t
{
    const GEO::index_t vertex = static_cast<GEO::index_t>(m_vertices.size());
    Edit_vertex& edit_vertex = m_vertices.emplace_back();
    edit_vertex.position = position;
    edit_vertex.sources.assign(sources.begin(), sources.end());
    ++m_live_vertex_count;
    return vertex;
}

auto Edit_mesh::split_edge(const GEO::index_t edge, const float t) -> GEO::index_t
{
    ERHE_VERIFY(is_edge_alive(edge));
    const GEO::index_t               a         = m_edges[edge].vertices[0];
    const GEO::index_t               b         = m_edges[edge].vertices[1];
    const std::optional<float>       sharpness = m_edges[edge].sharpness;
    const std::vector<GEO::index_t>  facets    = m_edges[edge].facets;

    std::vector<Edit_source> vertex_sources;
    accumulate_sources(vertex_sources, m_vertices[a].sources, 1.0f - t);
    accumulate_sources(vertex_sources, m_vertices[b].sources, t);
    const GEO::vec3f position = ((1.0f - t) * m_vertices[a].position) + (t * m_vertices[b].position);
    const GEO::index_t new_vertex = add_vertex(position, vertex_sources);

    for (const GEO::index_t facet : facets) {
        unlink_facet(facet);
    }
    remove_edge(edge);

    for (const GEO::index_t facet : facets) {
        std::vector<Edit_corner>& corners = m_facets[facet].corners;
        const std::size_t n = corners.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t  j = (i + 1) % n;
            const GEO::index_t u = corners[i].vertex;
            const GEO::index_t v = corners[j].vertex;
            if (!(((u == a) && (v == b)) || ((u == b) && (v == a)))) {
                continue;
            }
            Edit_corner new_corner;
            new_corner.vertex = new_vertex;
            const float weight_i = (u == a) ? (1.0f - t) : t;
            accumulate_sources(new_corner.sources, corners[i].sources, weight_i);
            accumulate_sources(new_corner.sources, corners[j].sources, 1.0f - weight_i);
            corners.insert(corners.begin() + static_cast<std::ptrdiff_t>(i + 1), std::move(new_corner));
            break;
        }
        link_facet(facet);
    }

    const GEO::index_t edge_a = ensure_edge(a, new_vertex);
    const GEO::index_t edge_b = ensure_edge(new_vertex, b);
    m_edges[edge_a].sharpness = sharpness;
    m_edges[edge_b].sharpness = sharpness;
    return new_vertex;
}

auto Edit_mesh::split_facet(const GEO::index_t facet, const GEO::index_t corner_a, const GEO::index_t corner_b) -> GEO::index_t
{
    if (!is_facet_alive(facet)) {
        return GEO::NO_INDEX;
    }
    const std::size_t n = m_facets[facet].corners.size();
    if ((corner_a >= n) || (corner_b >= n) || (corner_a == corner_b)) {
        return GEO::NO_INDEX;
    }
    if ((((corner_a + 1) % n) == corner_b) || (((corner_b + 1) % n) == corner_a)) {
        return GEO::NO_INDEX;
    }
    if (m_facets[facet].corners[corner_a].vertex == m_facets[facet].corners[corner_b].vertex) {
        return GEO::NO_INDEX;
    }

    unlink_facet(facet);
    const std::vector<Edit_corner> corners = m_facets[facet].corners;
    std::vector<Edit_corner> first_part;
    std::vector<Edit_corner> second_part;
    for (std::size_t i = corner_a; ; i = (i + 1) % n) {
        first_part.push_back(corners[i]);
        if (i == corner_b) {
            break;
        }
    }
    for (std::size_t i = corner_b; ; i = (i + 1) % n) {
        second_part.push_back(corners[i]);
        if (i == corner_a) {
            break;
        }
    }
    const GEO::index_t source_facet = m_facets[facet].source_facet;
    m_facets[facet].corners = std::move(first_part);
    link_facet(facet);
    return make_facet(std::move(second_part), source_facet);
}

auto Edit_mesh::split_facet_edgenet(
    const GEO::index_t                                           facet,
    const std::span<const std::pair<GEO::index_t, GEO::index_t>> edges,
    std::vector<GEO::index_t>&                                   out_facets
) -> Edgenet_result
{
    out_facets.clear();
    if (!is_facet_alive(facet)) {
        return Edgenet_result::invalid_edges;
    }
    const std::vector<Edit_corner> original = m_facets[facet].corners;
    const std::size_t              n        = original.size();

    // Graph nodes: the facet's corners first (node i == local corner i),
    // then the interior vertices the edges name.
    std::vector<GEO::index_t>                      nodes;
    std::unordered_map<GEO::index_t, std::size_t>  node_of_vertex;
    std::vector<std::vector<std::size_t>>          adjacency;
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::index_t vertex = original[i].vertex;
        if (!node_of_vertex.emplace(vertex, i).second) {
            return Edgenet_result::invalid_edges; // repeated vertex in the facet
        }
        nodes.push_back(vertex);
    }
    adjacency.resize(n);
    const auto connect = [&adjacency](const std::size_t i, const std::size_t j) {
        if (std::find(adjacency[i].begin(), adjacency[i].end(), j) == adjacency[i].end()) {
            adjacency[i].push_back(j);
            adjacency[j].push_back(i);
        }
    };
    const auto disconnect = [&adjacency](const std::size_t i, const std::size_t j) {
        adjacency[i].erase(std::find(adjacency[i].begin(), adjacency[i].end(), j));
        adjacency[j].erase(std::find(adjacency[j].begin(), adjacency[j].end(), i));
    };
    for (std::size_t i = 0; i < n; ++i) {
        connect(i, (i + 1) % n);
    }
    const auto node_for = [&](const GEO::index_t vertex) -> std::size_t {
        const std::unordered_map<GEO::index_t, std::size_t>::const_iterator i = node_of_vertex.find(vertex);
        if (i != node_of_vertex.end()) {
            return i->second;
        }
        const std::size_t node = nodes.size();
        nodes.push_back(vertex);
        adjacency.emplace_back();
        node_of_vertex.emplace(vertex, node);
        return node;
    };
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : edges) {
        if ((edge.first == edge.second) || !is_vertex_alive(edge.first) || !is_vertex_alive(edge.second)) {
            return Edgenet_result::invalid_edges;
        }
        connect(node_for(edge.first), node_for(edge.second));
    }

    const auto is_boundary = [n](const std::size_t node) -> bool { return node < n; };

    // Drop dangling edges: interior nodes with one edge, repeatedly.
    const auto prune = [&]() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t node = n; node < nodes.size(); ++node) {
                if (adjacency[node].size() == 1) {
                    disconnect(node, adjacency[node].front());
                    changed = true;
                }
            }
        }
    };
    prune();

    // Plane of the facet: 2D coordinates for angular ordering and crossings.
    std::vector<GEO::vec3f> polygon(n);
    for (std::size_t i = 0; i < n; ++i) {
        polygon[i] = m_vertices[original[i].vertex].position;
    }
    const GEO::vec3f normal = compute_newell_normal(polygon);
    const GEO::vec3f axis_u = GEO::normalize(
        (std::abs(normal.x) < 0.9f)
            ? GEO::cross(normal, GEO::vec3f{1.0f, 0.0f, 0.0f})
            : GEO::cross(normal, GEO::vec3f{0.0f, 1.0f, 0.0f})
    );
    const GEO::vec3f axis_v = GEO::cross(normal, axis_u);
    std::vector<GEO::vec2f> plane(nodes.size());
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        const GEO::vec3f p = m_vertices[nodes[node]].position;
        plane[node] = GEO::vec2f{GEO::dot(p, axis_u), GEO::dot(p, axis_v)};
    }

    // Floating islands: components not reached from the boundary.
    {
        std::vector<std::uint8_t> reached(nodes.size(), 0);
        std::vector<std::size_t>  stack;
        const auto flood = [&](const std::size_t seed, std::vector<std::size_t>& component) {
            component.clear();
            stack.clear();
            stack.push_back(seed);
            reached[seed] = 1;
            while (!stack.empty()) {
                const std::size_t node = stack.back();
                stack.pop_back();
                component.push_back(node);
                for (const std::size_t next : adjacency[node]) {
                    if (reached[next] == 0) {
                        reached[next] = 1;
                        stack.push_back(next);
                    }
                }
            }
        };
        std::vector<std::size_t> component;
        flood(0, component);
        const auto crosses_any = [&](const std::size_t a, const std::size_t b) -> bool {
            for (std::size_t node = 0; node < nodes.size(); ++node) {
                for (const std::size_t other : adjacency[node]) {
                    if (other < node) {
                        continue;
                    }
                    if ((node == a) || (node == b) || (other == a) || (other == b)) {
                        continue;
                    }
                    if (segments_cross(plane[a], plane[b], plane[node], plane[other])) {
                        return true;
                    }
                }
            }
            return false;
        };
        for (std::size_t seed = n; seed < nodes.size(); ++seed) {
            if ((reached[seed] != 0) || adjacency[seed].empty()) {
                continue;
            }
            flood(seed, component);
            // First connecting edge: the nearest island / boundary vertex pair.
            float       best_distance = std::numeric_limits<float>::max();
            std::size_t best_island   = component.front();
            std::size_t best_boundary = 0;
            for (const std::size_t island_node : component) {
                for (std::size_t boundary_node = 0; boundary_node < n; ++boundary_node) {
                    const float distance = GEO::length(m_vertices[nodes[island_node]].position - m_vertices[nodes[boundary_node]].position);
                    if (distance < best_distance) {
                        best_distance = distance;
                        best_island   = island_node;
                        best_boundary = boundary_node;
                    }
                }
            }
            connect(best_island, best_boundary);
            // Second connecting edge: the nearest remaining pair that crosses
            // no edge, so the ring facet has no repeated vertex.
            float       second_distance = std::numeric_limits<float>::max();
            std::size_t second_island   = best_island;
            std::size_t second_boundary = best_boundary;
            for (const std::size_t island_node : component) {
                if (island_node == best_island) {
                    continue;
                }
                for (std::size_t boundary_node = 0; boundary_node < n; ++boundary_node) {
                    if (boundary_node == best_boundary) {
                        continue;
                    }
                    const float distance = GEO::length(m_vertices[nodes[island_node]].position - m_vertices[nodes[boundary_node]].position);
                    if ((distance < second_distance) && !crosses_any(island_node, boundary_node)) {
                        second_distance = distance;
                        second_island   = island_node;
                        second_boundary = boundary_node;
                    }
                }
            }
            if (second_island != best_island) {
                connect(second_island, second_boundary);
            }
        }
    }

    // Angular order of each node's neighbours (counter-clockwise around the
    // facet normal).
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        std::vector<std::size_t>& neighbours = adjacency[node];
        const GEO::vec2f centre = plane[node];
        std::sort(neighbours.begin(), neighbours.end(), [&](const std::size_t lhs, const std::size_t rhs) {
            const float angle_lhs = std::atan2(plane[lhs].y - centre.y, plane[lhs].x - centre.x);
            const float angle_rhs = std::atan2(plane[rhs].y - centre.y, plane[rhs].x - centre.x);
            return angle_lhs < angle_rhs;
        });
    }

    // Trace the facets left of every half-edge inside the facet: boundary
    // half-edges in facet order, new edges in both directions.
    const auto half_edge_key = [](const std::size_t from, const std::size_t to) -> std::uint64_t {
        return (static_cast<std::uint64_t>(from) << 32u) | static_cast<std::uint64_t>(to);
    };
    const auto is_outer = [&](const std::size_t from, const std::size_t to) -> bool {
        return is_boundary(from) && is_boundary(to) && (to == ((from + n - 1) % n)) && (n > 2);
    };
    std::size_t half_edge_count = 0;
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        half_edge_count += adjacency[node].size();
    }
    std::unordered_set<std::uint64_t>     used;
    std::vector<std::vector<std::size_t>> faces;
    for (std::size_t start_from = 0; start_from < nodes.size(); ++start_from) {
        for (const std::size_t start_to : adjacency[start_from]) {
            if (is_outer(start_from, start_to) || used.contains(half_edge_key(start_from, start_to))) {
                continue;
            }
            std::vector<std::size_t>& face = faces.emplace_back();
            std::size_t from = start_from;
            std::size_t to   = start_to;
            for (std::size_t step = 0; ; ++step) {
                if (step > half_edge_count) {
                    return Edgenet_result::invalid_edges;
                }
                face.push_back(from);
                used.insert(half_edge_key(from, to));
                const std::vector<std::size_t>& around = adjacency[to];
                const std::size_t k     = around.size();
                const std::size_t index = static_cast<std::size_t>(std::find(around.begin(), around.end(), from) - around.begin());
                const std::size_t next  = around[(index + k - 1) % k];
                from = to;
                to   = next;
                if ((from == start_from) && (to == start_to)) {
                    break;
                }
                if (is_outer(from, to)) {
                    return Edgenet_result::invalid_edges;
                }
            }
        }
    }

    for (std::vector<std::size_t>& face : faces) {
        if (face.size() < 3) {
            return Edgenet_result::invalid_edges;
        }
        std::vector<std::size_t> sorted = face;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
            return Edgenet_result::invalid_edges;
        }
    }
    if (faces.size() < 2) {
        return Edgenet_result::unchanged;
    }

    // Corner provenance: boundary nodes keep their corner, interior nodes
    // interpolate the facet's corners by mean value coordinates.
    std::vector<float> weights;
    const auto make_corner = [&](const std::size_t node) -> Edit_corner {
        if (is_boundary(node)) {
            return original[node];
        }
        Edit_corner corner;
        corner.vertex = nodes[node];
        compute_mean_value_weights(polygon, normal, m_vertices[nodes[node]].position, weights);
        for (std::size_t i = 0; i < n; ++i) {
            accumulate_sources(corner.sources, original[i].sources, weights[i]);
        }
        return corner;
    };

    const GEO::index_t source_facet = m_facets[facet].source_facet;
    unlink_facet(facet);
    for (std::size_t face_index = 0; face_index < faces.size(); ++face_index) {
        std::vector<Edit_corner> corners;
        corners.reserve(faces[face_index].size());
        for (const std::size_t node : faces[face_index]) {
            corners.push_back(make_corner(node));
        }
        if (face_index == 0) {
            m_facets[facet].corners = std::move(corners);
            link_facet(facet);
            out_facets.push_back(facet);
        } else {
            out_facets.push_back(make_facet(std::move(corners), source_facet));
        }
    }
    return Edgenet_result::split;
}

auto Edit_mesh::join_facets(const std::span<const GEO::index_t> region_in, GEO::index_t& out_facet) -> Join_result
{
    out_facet = GEO::NO_INDEX;
    std::vector<GEO::index_t> region;
    for (const GEO::index_t facet : region_in) {
        if (is_facet_alive(facet)) {
            push_unique(region, facet);
        }
    }
    if (region.empty()) {
        return Join_result::empty_region;
    }
    if (region.size() == 1) {
        out_facet = region.front();
        return Join_result::joined;
    }
    const auto region_index = [&region](const GEO::index_t facet) -> std::size_t {
        return static_cast<std::size_t>(std::find(region.begin(), region.end(), facet) - region.begin());
    };

    std::vector<std::size_t> parent(region.size());
    std::iota(parent.begin(), parent.end(), std::size_t{0});
    const auto find_root = [&parent](std::size_t x) -> std::size_t {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    class Half_edge
    {
    public:
        GEO::index_t start;
        GEO::index_t end;
        GEO::index_t facet;
        std::size_t  local_corner;
    };
    std::vector<Half_edge>    boundary;
    std::vector<GEO::index_t> interior_edges;
    std::vector<GEO::index_t> region_vertices;
    for (std::size_t r = 0; r < region.size(); ++r) {
        const GEO::index_t              facet   = region[r];
        const std::vector<Edit_corner>& corners = m_facets[facet].corners;
        const std::size_t               n       = corners.size();
        for (std::size_t i = 0; i < n; ++i) {
            const GEO::index_t a = corners[i].vertex;
            const GEO::index_t b = corners[(i + 1) % n].vertex;
            push_unique(region_vertices, a);
            const GEO::index_t edge = find_edge(a, b);
            ERHE_VERIFY(edge != GEO::NO_INDEX);
            const std::vector<GEO::index_t>& edge_facets = m_edges[edge].facets;
            std::size_t in_region = 0;
            for (const GEO::index_t edge_facet : edge_facets) {
                if (region_index(edge_facet) < region.size()) {
                    ++in_region;
                }
            }
            if ((edge_facets.size() == 2) && (in_region == 2)) {
                const GEO::index_t other = (edge_facets[0] == facet) ? edge_facets[1] : edge_facets[0];
                // The neighbour must traverse the edge b -> a (consistent winding).
                const std::vector<Edit_corner>& other_corners = m_facets[other].corners;
                const std::size_t               m             = other_corners.size();
                bool opposite = false;
                for (std::size_t j = 0; j < m; ++j) {
                    if ((other_corners[j].vertex == b) && (other_corners[(j + 1) % m].vertex == a)) {
                        opposite = true;
                        break;
                    }
                }
                if (!opposite) {
                    return Join_result::invalid_boundary;
                }
                const std::size_t root_a = find_root(r);
                const std::size_t root_b = find_root(region_index(other));
                if (root_a != root_b) {
                    parent[root_a] = root_b;
                }
                push_unique(interior_edges, edge);
            } else if (in_region >= 2) {
                return Join_result::non_manifold;
            } else {
                boundary.push_back(Half_edge{a, b, facet, i});
            }
        }
    }

    const std::size_t root = find_root(0);
    for (std::size_t r = 1; r < region.size(); ++r) {
        if (find_root(r) != root) {
            return Join_result::not_edge_connected;
        }
    }

    // The boundary must be one loop through distinct vertices.
    std::unordered_map<GEO::index_t, std::size_t> half_edge_from_start;
    for (std::size_t i = 0; i < boundary.size(); ++i) {
        if (!half_edge_from_start.emplace(boundary[i].start, i).second) {
            return Join_result::invalid_boundary;
        }
    }
    if (boundary.size() < 3) {
        return Join_result::invalid_boundary;
    }
    std::vector<Edit_corner> corners;
    std::size_t current = 0;
    for (std::size_t step = 0; step < boundary.size(); ++step) {
        const Half_edge& half_edge = boundary[current];
        corners.push_back(m_facets[half_edge.facet].corners[half_edge.local_corner]);
        const std::unordered_map<GEO::index_t, std::size_t>::const_iterator next = half_edge_from_start.find(half_edge.end);
        if (next == half_edge_from_start.end()) {
            return Join_result::invalid_boundary;
        }
        current = next->second;
        if ((current == 0) && ((step + 1) != boundary.size())) {
            return Join_result::invalid_boundary;
        }
    }
    if (current != 0) {
        return Join_result::invalid_boundary;
    }

    const GEO::index_t source_facet = m_facets[region.front()].source_facet;
    for (const GEO::index_t facet : region) {
        kill_facet(facet);
    }
    for (const GEO::index_t edge : interior_edges) {
        remove_edge(edge);
    }
    out_facet = make_facet(std::move(corners), source_facet);
    for (const GEO::index_t vertex : region_vertices) {
        if (m_vertices[vertex].edges.empty() && m_vertices[vertex].facets.empty()) {
            kill_vertex(vertex);
        }
    }
    return Join_result::joined;
}

auto Edit_mesh::join_facet_pair(const GEO::index_t edge, GEO::index_t& out_facet) -> Join_result
{
    out_facet = GEO::NO_INDEX;
    if (!is_edge_alive(edge)) {
        return Join_result::empty_region;
    }
    const std::vector<GEO::index_t>& facets = m_edges[edge].facets;
    if (facets.size() != 2) {
        return (facets.size() > 2) ? Join_result::non_manifold : Join_result::invalid_boundary;
    }
    const std::array<GEO::index_t, 2> pair{facets[0], facets[1]};
    return join_facets(pair, out_facet);
}

auto Edit_mesh::collapse_vertex(const GEO::index_t vertex) -> Collapse_result
{
    if (!is_vertex_alive(vertex) || (m_vertices[vertex].edges.size() != 2)) {
        return Collapse_result::not_two_valent;
    }
    const GEO::index_t edge_0 = m_vertices[vertex].edges[0];
    const GEO::index_t edge_1 = m_vertices[vertex].edges[1];
    const GEO::index_t prev   = get_edge_other_vertex(edge_0, vertex);
    const GEO::index_t next   = get_edge_other_vertex(edge_1, vertex);
    if (prev == next) {
        return Collapse_result::not_two_valent;
    }
    const auto max_sharpness = [](const std::optional<float> lhs, const std::optional<float> rhs) -> std::optional<float> {
        if (!lhs.has_value()) {
            return rhs;
        }
        if (!rhs.has_value()) {
            return lhs;
        }
        return std::max(lhs.value(), rhs.value());
    };
    const std::optional<float> sharpness = max_sharpness(m_edges[edge_0].sharpness, m_edges[edge_1].sharpness);

    const std::vector<GEO::index_t> facets = m_vertices[vertex].facets;
    for (const GEO::index_t facet : facets) {
        unlink_facet(facet);
        std::vector<Edit_corner>& corners = m_facets[facet].corners;
        corners.erase(
            std::remove_if(corners.begin(), corners.end(), [vertex](const Edit_corner& corner) { return corner.vertex == vertex; }),
            corners.end()
        );
    }
    remove_edge(edge_0);
    remove_edge(edge_1);
    kill_vertex(vertex);
    for (const GEO::index_t facet : facets) {
        if (m_facets[facet].corners.size() < 3) {
            kill_unlinked_facet(facet);
        } else {
            link_facet(facet);
        }
    }
    const GEO::index_t merged = ensure_edge(prev, next);
    m_edges[merged].sharpness = max_sharpness(m_edges[merged].sharpness, sharpness);
    return Collapse_result::collapsed;
}

void Edit_mesh::weld_vertices(const std::span<const std::pair<GEO::index_t, GEO::index_t>> merges)
{
    if (merges.empty()) {
        return;
    }
    const std::size_t vertex_slot_count = m_vertices.size();
    std::vector<GEO::index_t> target(vertex_slot_count);
    std::iota(target.begin(), target.end(), GEO::index_t{0});
    for (const std::pair<GEO::index_t, GEO::index_t>& merge : merges) {
        ERHE_VERIFY(is_vertex_alive(merge.first) && is_vertex_alive(merge.second));
        if (merge.first != merge.second) {
            target[merge.first] = merge.second;
        }
    }
    for (std::size_t v = 0; v < vertex_slot_count; ++v) {
        GEO::index_t t = target[v];
        for (std::size_t guard = 0; (target[t] != t) && (guard < vertex_slot_count); ++guard) {
            t = target[t];
        }
        target[v] = t;
    }
    std::vector<GEO::index_t> merged_vertices;
    for (std::size_t v = 0; v < vertex_slot_count; ++v) {
        if (target[v] != static_cast<GEO::index_t>(v)) {
            merged_vertices.push_back(static_cast<GEO::index_t>(v));
        }
    }
    if (merged_vertices.empty()) {
        return;
    }
    const auto is_merged = [&target](const GEO::index_t v) -> bool { return target[v] != v; };

    // 1. Split every facet holding a non-adjacent merge pair between the two
    //    (recursively), so each facet only holds adjacent merge pairs.
    std::vector<GEO::index_t> worklist;
    for (const GEO::index_t v : merged_vertices) {
        for (const GEO::index_t facet : m_vertices[v].facets) {
            push_unique(worklist, facet);
        }
    }
    while (!worklist.empty()) {
        const GEO::index_t facet = worklist.back();
        worklist.pop_back();
        if (!is_facet_alive(facet)) {
            continue;
        }
        const std::vector<Edit_corner>& corners = m_facets[facet].corners;
        const std::size_t n = corners.size();
        GEO::index_t split_a = GEO::NO_INDEX;
        GEO::index_t split_b = GEO::NO_INDEX;
        for (std::size_t i = 0; (i < n) && (split_a == GEO::NO_INDEX); ++i) {
            for (std::size_t j = i + 2; j < n; ++j) {
                if ((i == 0) && (j == (n - 1))) {
                    continue; // adjacent across the wrap
                }
                if (target[corners[i].vertex] == target[corners[j].vertex]) {
                    split_a = static_cast<GEO::index_t>(i);
                    split_b = static_cast<GEO::index_t>(j);
                    break;
                }
            }
        }
        if (split_a == GEO::NO_INDEX) {
            continue;
        }
        const GEO::index_t new_facet = split_facet(facet, split_a, split_b);
        ERHE_VERIFY(new_facet != GEO::NO_INDEX);
        worklist.push_back(facet);
        worklist.push_back(new_facet);
    }

    // 2. Unlink every facet touching a merged vertex.
    std::vector<GEO::index_t> affected_facets;
    for (const GEO::index_t v : merged_vertices) {
        for (const GEO::index_t facet : m_vertices[v].facets) {
            push_unique(affected_facets, facet);
        }
    }
    for (const GEO::index_t facet : affected_facets) {
        unlink_facet(facet);
    }

    // 3. Map every edge at a merged vertex: a collapsed edge disappears, an
    //    edge mapping onto an existing edge is replaced by it.
    class Pending_edge
    {
    public:
        GEO::index_t         a;
        GEO::index_t         b;
        std::optional<float> sharpness;
    };
    std::vector<Pending_edge> pending_edges;
    std::vector<GEO::index_t> merged_edges;
    for (const GEO::index_t v : merged_vertices) {
        for (const GEO::index_t edge : m_vertices[v].edges) {
            push_unique(merged_edges, edge);
        }
    }
    for (const GEO::index_t edge : merged_edges) {
        const Edit_edge& edit_edge = m_edges[edge];
        const GEO::index_t a = target[edit_edge.vertices[0]];
        const GEO::index_t b = target[edit_edge.vertices[1]];
        if (a != b) {
            pending_edges.push_back(Pending_edge{a, b, edit_edge.sharpness});
        }
        remove_edge(edge);
    }
    for (const Pending_edge& pending : pending_edges) {
        if (find_edge(pending.a, pending.b) != GEO::NO_INDEX) {
            continue;
        }
        const GEO::index_t edge = ensure_edge(pending.a, pending.b);
        m_edges[edge].sharpness = pending.sharpness;
    }

    // 4. Rebuild the affected facets from their mapped corners.
    for (const GEO::index_t facet : affected_facets) {
        std::vector<Edit_corner>& corners = m_facets[facet].corners;
        std::vector<Edit_corner> rebuilt;
        rebuilt.reserve(corners.size());
        for (Edit_corner& corner : corners) {
            corner.vertex = target[corner.vertex];
            if (!rebuilt.empty() && (rebuilt.back().vertex == corner.vertex)) {
                continue; // consecutive duplicate: the first corner of the run is kept
            }
            rebuilt.push_back(std::move(corner));
        }
        while ((rebuilt.size() > 1) && (rebuilt.back().vertex == rebuilt.front().vertex)) {
            rebuilt.pop_back();
        }
        bool repeats = false;
        for (std::size_t i = 0; (i < rebuilt.size()) && !repeats; ++i) {
            for (std::size_t j = i + 1; j < rebuilt.size(); ++j) {
                if (rebuilt[i].vertex == rebuilt[j].vertex) {
                    repeats = true;
                    break;
                }
            }
        }
        corners = std::move(rebuilt);
        if ((corners.size() < 3) || repeats || has_facet_with_vertex_set(corners, facet)) {
            corners.clear();
            kill_unlinked_facet(facet);
            continue;
        }
        link_facet(facet);
    }

    // 5. The merged vertices are now unused.
    for (const GEO::index_t v : merged_vertices) {
        ERHE_VERIFY(!is_merged(target[v]));
        kill_vertex(v);
    }
}

void Edit_mesh::separate_vertex(
    const GEO::index_t                  vertex,
    const std::span<const GEO::index_t> edges,
    std::vector<GEO::index_t>&          out_vertices
)
{
    out_vertices.clear();
    ERHE_VERIFY(is_vertex_alive(vertex));
    out_vertices.push_back(vertex);
    const std::vector<GEO::index_t> facets = m_vertices[vertex].facets;
    if (facets.size() < 2) {
        return;
    }
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
    const std::vector<GEO::index_t> vertex_edges = m_vertices[vertex].edges;
    for (const GEO::index_t edge : vertex_edges) {
        if (std::find(edges.begin(), edges.end(), edge) != edges.end()) {
            continue;
        }
        const std::vector<GEO::index_t>& edge_facets = m_edges[edge].facets;
        for (std::size_t i = 1; i < edge_facets.size(); ++i) {
            const std::size_t root_a = find_root(facet_index(edge_facets[0]));
            const std::size_t root_b = find_root(facet_index(edge_facets[i]));
            if (root_a != root_b) {
                parent[root_a] = root_b;
            }
        }
    }

    // Fans in the order of their first facet; the first fan keeps the vertex.
    std::vector<std::size_t> fan_roots;
    for (std::size_t i = 0; i < facets.size(); ++i) {
        const std::size_t root = find_root(i);
        if (std::find(fan_roots.begin(), fan_roots.end(), root) == fan_roots.end()) {
            fan_roots.push_back(root);
        }
    }
    if (fan_roots.size() < 2) {
        return;
    }

    std::vector<GEO::index_t> edges_with_facets;
    for (const GEO::index_t edge : vertex_edges) {
        if (!m_edges[edge].facets.empty()) {
            edges_with_facets.push_back(edge);
        }
    }

    for (std::size_t fan = 1; fan < fan_roots.size(); ++fan) {
        const std::vector<Edit_source> sources  = m_vertices[vertex].sources;
        const GEO::vec3f               position = m_vertices[vertex].position; // copy: add_vertex() may reallocate m_vertices
        const GEO::index_t             copy     = add_vertex(position, sources);
        for (std::size_t i = 0; i < facets.size(); ++i) {
            if (find_root(i) != fan_roots[fan]) {
                continue;
            }
            const GEO::index_t facet = facets[i];
            unlink_facet(facet);
            for (Edit_corner& corner : m_facets[facet].corners) {
                if (corner.vertex == vertex) {
                    corner.vertex = copy;
                }
            }
            link_facet(facet);
        }
        for (const GEO::index_t edge : vertex_edges) {
            const GEO::index_t other     = get_edge_other_vertex(edge, vertex);
            const GEO::index_t copy_edge = find_edge(copy, other);
            if (copy_edge != GEO::NO_INDEX) {
                m_edges[copy_edge].sharpness = m_edges[edge].sharpness;
            }
        }
        out_vertices.push_back(copy);
    }

    // An edge of the vertex that had facets and lost all of them moved to a copy.
    for (const GEO::index_t edge : edges_with_facets) {
        if (m_edges[edge].facets.empty()) {
            remove_edge(edge);
        }
    }
}

void Edit_mesh::delete_elements(const std::span<const GEO::index_t> elements, const Delete_context context)
{
    switch (context) {
        case Delete_context::vertices: {
            for (const GEO::index_t vertex : elements) {
                if (!is_vertex_alive(vertex)) {
                    continue;
                }
                const std::vector<GEO::index_t> facets = m_vertices[vertex].facets;
                for (const GEO::index_t facet : facets) {
                    kill_facet(facet);
                }
                const std::vector<GEO::index_t> edges = m_vertices[vertex].edges;
                for (const GEO::index_t edge : edges) {
                    remove_edge(edge);
                }
                kill_vertex(vertex);
            }
            break;
        }

        case Delete_context::edges:
        case Delete_context::only_edges_and_faces: {
            std::vector<GEO::index_t> endpoints;
            for (const GEO::index_t edge : elements) {
                if (!is_edge_alive(edge)) {
                    continue;
                }
                const std::vector<GEO::index_t> facets = m_edges[edge].facets;
                for (const GEO::index_t facet : facets) {
                    kill_facet(facet);
                }
                push_unique(endpoints, m_edges[edge].vertices[0]);
                push_unique(endpoints, m_edges[edge].vertices[1]);
                remove_edge(edge);
            }
            if (context == Delete_context::edges) {
                for (const GEO::index_t vertex : endpoints) {
                    if (m_vertices[vertex].edges.empty() && m_vertices[vertex].facets.empty()) {
                        kill_vertex(vertex);
                    }
                }
            }
            break;
        }

        case Delete_context::faces: {
            std::vector<GEO::index_t> facet_edges;
            std::vector<GEO::index_t> facet_vertices;
            for (const GEO::index_t facet : elements) {
                if (!is_facet_alive(facet)) {
                    continue;
                }
                const std::vector<Edit_corner>& corners = m_facets[facet].corners;
                const std::size_t n = corners.size();
                for (std::size_t i = 0; i < n; ++i) {
                    push_unique(facet_vertices, corners[i].vertex);
                    const GEO::index_t edge = find_edge(corners[i].vertex, corners[(i + 1) % n].vertex);
                    if (edge != GEO::NO_INDEX) {
                        push_unique(facet_edges, edge);
                    }
                }
                kill_facet(facet);
            }
            for (const GEO::index_t edge : facet_edges) {
                if (is_edge_alive(edge) && m_edges[edge].facets.empty()) {
                    remove_edge(edge);
                }
            }
            for (const GEO::index_t vertex : facet_vertices) {
                if (is_vertex_alive(vertex) && m_vertices[vertex].edges.empty() && m_vertices[vertex].facets.empty()) {
                    kill_vertex(vertex);
                }
            }
            break;
        }

        case Delete_context::only_faces: {
            for (const GEO::index_t facet : elements) {
                if (is_facet_alive(facet)) {
                    kill_facet(facet);
                }
            }
            break;
        }
    }
}

auto Edit_mesh::create_facet(const std::span<const GEO::index_t> vertices, const GEO::index_t reference_facet) -> GEO::index_t
{
    const std::size_t n = vertices.size();
    if (n < 3) {
        return GEO::NO_INDEX;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!is_vertex_alive(vertices[i])) {
            return GEO::NO_INDEX;
        }
        for (std::size_t j = i + 1; j < n; ++j) {
            if (vertices[i] == vertices[j]) {
                return GEO::NO_INDEX;
            }
        }
    }
    const bool has_reference = is_facet_alive(reference_facet);
    std::vector<Edit_corner> corners(n);
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::index_t vertex = vertices[i];
        corners[i].vertex = vertex;
        GEO::index_t from_facet = GEO::NO_INDEX;
        if (has_reference && (find_facet_corner(reference_facet, vertex) != GEO::NO_INDEX)) {
            from_facet = reference_facet;
        } else if (!m_vertices[vertex].facets.empty()) {
            from_facet = m_vertices[vertex].facets.front();
        }
        if (from_facet != GEO::NO_INDEX) {
            corners[i].sources = m_facets[from_facet].corners[find_facet_corner(from_facet, vertex)].sources;
        }
    }
    if (has_facet_with_vertex_set(corners, GEO::NO_INDEX)) {
        return GEO::NO_INDEX;
    }
    const GEO::index_t source_facet = has_reference ? m_facets[reference_facet].source_facet : GEO::NO_INDEX;
    return make_facet(std::move(corners), source_facet);
}

void Edit_mesh::set_facet_vertices(const GEO::index_t facet, const std::span<const GEO::index_t> vertices)
{
    ERHE_VERIFY(is_facet_alive(facet));
    std::vector<Edit_corner>& corners = m_facets[facet].corners;
    ERHE_VERIFY(vertices.size() == corners.size());
    for (const GEO::index_t vertex : vertices) {
        ERHE_VERIFY(is_vertex_alive(vertex));
    }
    // The edges the facet uses now; any of them left without facets goes.
    const std::size_t n = corners.size();
    std::vector<GEO::index_t> old_edges;
    old_edges.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const GEO::index_t edge = find_edge(corners[i].vertex, corners[(i + 1) % n].vertex);
        if (edge != GEO::NO_INDEX) {
            old_edges.push_back(edge);
        }
    }
    unlink_facet(facet);
    for (std::size_t i = 0; i < n; ++i) {
        corners[i].vertex = vertices[i];
    }
    link_facet(facet);
    for (const GEO::index_t edge : old_edges) {
        if (!m_edges[edge].deleted && m_edges[edge].facets.empty()) {
            remove_edge(edge);
        }
    }
}

void Edit_mesh::set_corner_sources(const GEO::index_t facet, const GEO::index_t local_corner, const std::span<const Edit_source> sources)
{
    ERHE_VERIFY(is_facet_alive(facet));
    ERHE_VERIFY(local_corner < m_facets[facet].corners.size());
    m_facets[facet].corners[local_corner].sources.assign(sources.begin(), sources.end());
}

auto Edit_mesh::create_facet_from_corners(const std::span<const Edit_corner> corners, const GEO::index_t source_facet) -> GEO::index_t
{
    const std::size_t n = corners.size();
    if (n < 3) {
        return GEO::NO_INDEX;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!is_vertex_alive(corners[i].vertex)) {
            return GEO::NO_INDEX;
        }
        for (std::size_t j = i + 1; j < n; ++j) {
            if (corners[i].vertex == corners[j].vertex) {
                return GEO::NO_INDEX;
            }
        }
    }
    if (has_facet_with_vertex_set(corners, GEO::NO_INDEX)) {
        return GEO::NO_INDEX;
    }
    return make_facet(std::vector<Edit_corner>{corners.begin(), corners.end()}, source_facet);
}

} // namespace erhe::geometry
