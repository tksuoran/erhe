#include "erhe_geometry/operation/inset_faces.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

namespace {

// Even offset, edge rail and shell factors are reciprocals of cosines; a
// cosine below this (a near-degenerate corner) is clamped to it.
constexpr float c_min_cosine = 0.1f;

[[nodiscard]] auto safe_normalize(const GEO::vec3f& v, const GEO::vec3f& fallback) -> GEO::vec3f
{
    const float length = GEO::length(v);
    return (length > 1e-12f) ? (v / length) : fallback;
}

[[nodiscard]] auto same_position(const GEO::vec3f& a, const GEO::vec3f& b) -> bool
{
    return (a.x == b.x) && (a.y == b.y) && (a.z == b.z);
}

[[nodiscard]] auto reciprocal_cosine(const float cosine) -> float
{
    return 1.0f / std::max(cosine, c_min_cosine);
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

// A region (or individual) facet as it was before the inset.
class Facet_record
{
public:
    GEO::index_t                          facet{GEO::NO_INDEX};
    std::vector<GEO::index_t>             vertices;       // original corner vertices
    std::vector<GEO::index_t>             moved_vertices; // corner vertices after the inset
    std::vector<GEO::vec3f>               positions;      // original corner positions
    std::vector<std::vector<Edit_source>> corner_sources; // original corner provenance
    std::vector<std::optional<float>>     sharpness;      // of edge (vertices[i], vertices[i + 1])
    GEO::vec3f                            normal{0.0f, 0.0f, 1.0f};
};

// An edge of a region facet on the region boundary: (vertices[corner],
// vertices[corner + 1]) of the record.
class Boundary_edge
{
public:
    std::size_t  record{0};
    std::size_t  corner{0};
    GEO::index_t a{GEO::NO_INDEX};
    GEO::index_t b{GEO::NO_INDEX};
    GEO::vec3f   tangent{0.0f, 0.0f, 0.0f};
    float        length{0.0f};
};

// A vertex of the inset facets: its position with thickness and depth 0, and
// its two directions.
class Inset_vertex
{
public:
    GEO::vec3f origin         {0.0f, 0.0f, 0.0f};
    GEO::vec3f direction      {0.0f, 0.0f, 0.0f};
    GEO::vec3f depth_direction{0.0f, 0.0f, 0.0f};
};

class Inset_faces : public Edit_mesh_operation
{
public:
    Inset_faces(
        const Geometry&               source,
        Geometry&                     destination,
        const std::set<GEO::index_t>& selected_facets,
        const Inset_faces_options&    options
    )
        : Edit_mesh_operation{source, destination}
        , m_options          {options}
    {
        const GEO::index_t facet_count = m_edit_mesh.get_facet_slot_count();
        m_selected.assign(facet_count, 0);
        for (const GEO::index_t facet : selected_facets) {
            if ((facet < facet_count) && m_edit_mesh.is_facet_alive(facet)) {
                m_selected[facet] = 1;
            }
        }
    }

    void build(Inset_faces_result* result)
    {
        if (m_options.individual) {
            build_individual();
        } else {
            build_region();
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

        m_dst_rim_facets.clear();
        for (const GEO::index_t facet : m_rim_facets) {
            m_dst_rim_facets.insert(facet_to_dst[facet]);
        }
        if (result == nullptr) {
            return;
        }
        result->inset_vertices.clear();
        result->inset_directions.clear();
        result->depth_directions.clear();
        result->inset_facets.clear();
        result->rim_facets.clear();
        // m_inset_vertices is keyed by scratch vertex; emit() keeps the order.
        for (const std::pair<const GEO::index_t, Inset_vertex>& entry : m_inset_vertices) {
            const GEO::index_t dst_vertex = get_emitted_vertex(entry.first);
            if (dst_vertex == GEO::NO_INDEX) {
                continue;
            }
            result->inset_vertices  .push_back(dst_vertex);
            result->inset_directions.push_back(entry.second.direction);
            result->depth_directions.push_back(entry.second.depth_direction);
        }
        for (const Facet_record& record : m_records) {
            result->inset_facets.push_back(facet_to_dst[record.facet]);
        }
        result->rim_facets.assign(m_dst_rim_facets.begin(), m_dst_rim_facets.end());
        std::sort(result->inset_facets.begin(), result->inset_facets.end());
    }

    // The general remap, without the rim facets (they carry their region
    // facet as source facet).
    void remap(const Geometry_component_selection& src, Geometry_component_selection& dst) const
    {
        remap_component_selection(src, dst);
        for (const GEO::index_t facet : m_dst_rim_facets) {
            dst.facets.erase(facet);
        }
    }

private:
    [[nodiscard]] auto make_record(const GEO::index_t facet) const -> Facet_record
    {
        Facet_record record{};
        record.facet = facet;
        for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
            record.vertices.push_back(corner.vertex);
            record.positions.push_back(m_edit_mesh.get_position(corner.vertex));
            record.corner_sources.push_back(corner.sources);
        }
        const std::size_t n = record.vertices.size();
        for (std::size_t i = 0; i < n; ++i) {
            const GEO::index_t edge = m_edit_mesh.find_edge(record.vertices[i], record.vertices[(i + 1) % n]);
            record.sharpness.push_back((edge != GEO::NO_INDEX) ? m_edit_mesh.get_edge_sharpness(edge) : std::optional<float>{});
        }
        record.moved_vertices = record.vertices;
        record.normal         = compute_newell_normal(record.positions);
        return record;
    }

    // Tangent of the record's edge (corner, corner + 1): in the facet plane,
    // perpendicular to the edge, pointing into the facet.
    [[nodiscard]] static auto edge_tangent(const Facet_record& record, const std::size_t corner) -> GEO::vec3f
    {
        const std::size_t n = record.positions.size();
        const GEO::vec3f  d = record.positions[(corner + 1) % n] - record.positions[corner];
        return safe_normalize(GEO::cross(record.normal, d), GEO::vec3f{0.0f, 0.0f, 0.0f});
    }

    [[nodiscard]] static auto edge_length(const Facet_record& record, const std::size_t corner) -> float
    {
        const std::size_t n = record.positions.size();
        return GEO::length(record.positions[(corner + 1) % n] - record.positions[corner]);
    }

    void build_region()
    {
        const GEO::index_t facet_count = m_edit_mesh.get_facet_slot_count();
        m_region.assign(facet_count, 0);
        m_record_of_facet.assign(facet_count, s_no_record);
        for (GEO::index_t facet = 0; facet < facet_count; ++facet) {
            if (!m_edit_mesh.is_facet_alive(facet)) {
                continue;
            }
            const bool selected = (m_selected[facet] != 0);
            if (selected != m_options.outset) {
                m_region[facet] = 1;
            }
        }
        for (GEO::index_t facet = 0; facet < facet_count; ++facet) {
            if (m_region[facet] != 0) {
                m_record_of_facet[facet] = m_records.size();
                m_records.push_back(make_record(facet));
            }
        }

        // Boundary edges.
        for (std::size_t record_index = 0; record_index < m_records.size(); ++record_index) {
            const Facet_record& record = m_records[record_index];
            const std::size_t   n      = record.vertices.size();
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t a    = record.vertices[i];
                const GEO::index_t b    = record.vertices[(i + 1) % n];
                const GEO::index_t edge = m_edit_mesh.find_edge(a, b);
                if (edge == GEO::NO_INDEX) {
                    continue;
                }
                const std::vector<GEO::index_t>& edge_facets = m_edit_mesh.get_edge(edge).facets;
                bool outside = false;
                for (const GEO::index_t facet : edge_facets) {
                    if (m_region[facet] == 0) {
                        outside = true;
                        break;
                    }
                }
                const bool mesh_boundary = (edge_facets.size() == 1);
                if (!outside && !(mesh_boundary && m_options.boundary)) {
                    continue;
                }
                m_boundary.push_back(Boundary_edge{
                    .record  = record_index,
                    .corner  = i,
                    .a       = a,
                    .b       = b,
                    .tangent = edge_tangent(record, i),
                    .length  = edge_length(record, i)
                });
            }
        }
        if (m_boundary.empty()) {
            m_records.clear(); // nothing moves
            return;
        }

        // The vertices of the boundary edges, each split into its fans.
        std::vector<GEO::index_t> boundary_vertices;
        for (const Boundary_edge& edge : m_boundary) {
            boundary_vertices.push_back(edge.a);
            boundary_vertices.push_back(edge.b);
        }
        std::sort(boundary_vertices.begin(), boundary_vertices.end());
        boundary_vertices.erase(std::unique(boundary_vertices.begin(), boundary_vertices.end()), boundary_vertices.end());
        for (const GEO::index_t vertex : boundary_vertices) {
            split_vertex(vertex);
        }

        // Move the region facets onto their inset vertices.
        for (Facet_record& record : m_records) {
            if (record.moved_vertices != record.vertices) {
                m_edit_mesh.set_facet_vertices(record.facet, record.moved_vertices);
            }
        }

        // Every vertex of the region facets; the unsplit ones have no
        // thickness direction.
        for (const Facet_record& record : m_records) {
            for (const GEO::index_t vertex : record.moved_vertices) {
                if (!m_inset_vertices.contains(vertex)) {
                    m_inset_vertices.emplace(vertex, Inset_vertex{.origin = m_edit_mesh.get_position(vertex)});
                }
            }
        }
        for (const std::pair<const GEO::index_t, GEO::vec3f>& entry : m_split_directions) {
            m_inset_vertices[entry.first].direction = entry.second;
        }
        compute_region_depth_directions();
        place_and_finish();
    }

    // The fans of the region facets around vertex (see the header); every fan
    // with a boundary edge at the vertex gets an inset vertex and its
    // direction.
    void split_vertex(const GEO::index_t vertex)
    {
        std::vector<GEO::index_t> facets;
        for (const GEO::index_t facet : m_edit_mesh.get_vertex_facets(vertex)) {
            if (m_region[facet] != 0) {
                facets.push_back(facet);
            }
        }
        if (facets.empty()) {
            return;
        }
        const auto local_index = [&facets](const GEO::index_t facet) -> std::size_t {
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
        // Interior edges at the vertex: every facet in the region.
        std::vector<GEO::index_t> interior_edges;
        for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
            const std::vector<GEO::index_t>& edge_facets = m_edit_mesh.get_edge(edge).facets;
            if (edge_facets.size() < 2) {
                continue;
            }
            bool all_region = true;
            for (const GEO::index_t facet : edge_facets) {
                if (m_region[facet] == 0) {
                    all_region = false;
                    break;
                }
            }
            if (!all_region) {
                continue;
            }
            interior_edges.push_back(edge);
            for (std::size_t i = 1; i < edge_facets.size(); ++i) {
                const std::size_t root_a = find_root(local_index(edge_facets[0]));
                const std::size_t root_b = find_root(local_index(edge_facets[i]));
                if (root_a != root_b) {
                    parent[root_a] = root_b;
                }
            }
        }

        std::vector<std::size_t> roots;
        for (std::size_t i = 0; i < facets.size(); ++i) {
            const std::size_t root = find_root(i);
            if (std::find(roots.begin(), roots.end(), root) == roots.end()) {
                roots.push_back(root);
            }
        }

        const GEO::vec3f vertex_position = m_edit_mesh.get_position(vertex);
        for (const std::size_t root : roots) {
            const auto in_fan = [&](const GEO::index_t facet) -> bool {
                const std::size_t i = local_index(facet);
                return (i < facets.size()) && (find_root(i) == root);
            };
            // The fan's boundary edges at the vertex.
            std::vector<const Boundary_edge*> fan_boundary;
            for (const Boundary_edge& edge : m_boundary) {
                if (((edge.a == vertex) || (edge.b == vertex)) && in_fan(m_records[edge.record].facet)) {
                    fan_boundary.push_back(&edge);
                }
            }
            if (fan_boundary.empty()) {
                continue; // the fan keeps the vertex
            }

            GEO::vec3f direction{0.0f, 0.0f, 0.0f};
            float      scale = 1.0f;
            const auto mean_cosine = [&fan_boundary](const GEO::vec3f& d) -> float {
                float sum = 0.0f;
                for (const Boundary_edge* edge : fan_boundary) {
                    sum += GEO::dot(d, edge->tangent);
                }
                return sum / static_cast<float>(fan_boundary.size());
            };
            if (fan_boundary.size() == 1) {
                // Flush: along the fan's unsplit mesh boundary edge.
                const GEO::vec3f& tangent = fan_boundary.front()->tangent;
                direction = tangent;
                for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
                    const std::vector<GEO::index_t>& edge_facets = m_edit_mesh.get_edge(edge).facets;
                    if ((edge_facets.size() != 1) || !in_fan(edge_facets.front()) || is_boundary_edge(edge)) {
                        continue;
                    }
                    const GEO::index_t other = m_edit_mesh.get_edge_other_vertex(edge, vertex);
                    direction = safe_normalize(m_edit_mesh.get_position(other) - vertex_position, tangent);
                    break;
                }
                if (m_options.even_offset) {
                    scale = reciprocal_cosine(GEO::dot(direction, tangent));
                }
            } else {
                GEO::vec3f sum{0.0f, 0.0f, 0.0f};
                for (const Boundary_edge* edge : fan_boundary) {
                    sum += edge->tangent;
                }
                direction = safe_normalize(sum, fan_boundary.front()->tangent);
                bool railed = false;
                if (m_options.edge_rail) {
                    GEO::index_t rail_edge  = GEO::NO_INDEX;
                    std::size_t  rail_count = 0;
                    for (const GEO::index_t edge : interior_edges) {
                        if (in_fan(m_edit_mesh.get_edge(edge).facets.front())) {
                            rail_edge = edge;
                            ++rail_count;
                        }
                    }
                    if (rail_count == 1) {
                        const GEO::index_t other = m_edit_mesh.get_edge_other_vertex(rail_edge, vertex);
                        direction = safe_normalize(m_edit_mesh.get_position(other) - vertex_position, direction);
                        railed    = true;
                    }
                }
                if (m_options.even_offset || railed) {
                    scale = m_options.even_offset ? reciprocal_cosine(mean_cosine(direction)) : 1.0f;
                }
            }
            if (m_options.relative_offset) {
                float length_sum = 0.0f;
                for (const Boundary_edge* edge : fan_boundary) {
                    length_sum += edge->length;
                }
                scale *= length_sum / static_cast<float>(fan_boundary.size());
            }

            const std::vector<Edit_source> sources  = m_edit_mesh.get_vertex(vertex).sources;
            const GEO::index_t             inset    = m_edit_mesh.add_vertex(vertex_position, sources);
            m_split_directions.emplace(inset, direction * scale);
            for (std::size_t i = 0; i < facets.size(); ++i) {
                if (find_root(i) != root) {
                    continue;
                }
                Facet_record& record = m_records[m_record_of_facet[facets[i]]];
                for (std::size_t corner = 0; corner < record.vertices.size(); ++corner) {
                    if (record.vertices[corner] == vertex) {
                        record.moved_vertices[corner] = inset;
                    }
                }
            }
        }
    }

    [[nodiscard]] auto is_boundary_edge(const GEO::index_t edge) const -> bool
    {
        const Edit_edge& edit_edge = m_edit_mesh.get_edge(edge);
        for (const Boundary_edge& boundary : m_boundary) {
            if (
                ((boundary.a == edit_edge.vertices[0]) && (boundary.b == edit_edge.vertices[1])) ||
                ((boundary.a == edit_edge.vertices[1]) && (boundary.b == edit_edge.vertices[0]))
            ) {
                return true;
            }
        }
        return false;
    }

    // Region depth: the normalized sum of the normals of the region facets
    // using the vertex, scaled by the shell factor.
    void compute_region_depth_directions()
    {
        std::map<GEO::index_t, GEO::vec3f> sums;
        for (const Facet_record& record : m_records) {
            for (const GEO::index_t vertex : record.moved_vertices) {
                const std::pair<std::map<GEO::index_t, GEO::vec3f>::iterator, bool> entry = sums.try_emplace(vertex, GEO::vec3f{0.0f, 0.0f, 0.0f});
                entry.first->second += record.normal;
            }
        }
        std::map<GEO::index_t, std::pair<float, float>> cosines; // (sum, count)
        for (const Facet_record& record : m_records) {
            for (const GEO::index_t vertex : record.moved_vertices) {
                const GEO::vec3f direction = safe_normalize(sums[vertex], record.normal);
                std::pair<float, float>& entry = cosines[vertex];
                entry.first  += GEO::dot(direction, record.normal);
                entry.second += 1.0f;
            }
        }
        for (std::pair<const GEO::index_t, Inset_vertex>& entry : m_inset_vertices) {
            const GEO::vec3f              direction = safe_normalize(sums[entry.first], GEO::vec3f{0.0f, 0.0f, 0.0f});
            const std::pair<float, float> cosine    = cosines[entry.first];
            const float                   mean      = (cosine.second > 0.0f) ? (cosine.first / cosine.second) : 1.0f;
            entry.second.depth_direction = direction * reciprocal_cosine(mean);
        }
    }

    void build_individual()
    {
        const GEO::index_t facet_count = m_edit_mesh.get_facet_slot_count();
        for (GEO::index_t facet = 0; facet < facet_count; ++facet) {
            if ((m_selected[facet] != 0) && m_edit_mesh.is_facet_alive(facet)) {
                m_records.push_back(make_record(facet));
            }
        }
        for (std::size_t record_index = 0; record_index < m_records.size(); ++record_index) {
            Facet_record&     record = m_records[record_index];
            const std::size_t n      = record.vertices.size();
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t prev         = (i + n - 1) % n;
                const GEO::vec3f  tangent_prev = edge_tangent(record, prev);
                const GEO::vec3f  tangent_next = edge_tangent(record, i);
                const GEO::vec3f  direction    = safe_normalize(tangent_prev + tangent_next, tangent_next);
                float scale = 1.0f;
                if (m_options.even_offset) {
                    scale = reciprocal_cosine(0.5f * (GEO::dot(direction, tangent_prev) + GEO::dot(direction, tangent_next)));
                }
                if (m_options.relative_offset) {
                    scale *= 0.5f * (edge_length(record, prev) + edge_length(record, i));
                }
                const std::vector<Edit_source> sources = m_edit_mesh.get_vertex(record.vertices[i]).sources;
                const GEO::index_t             copy    = m_edit_mesh.add_vertex(record.positions[i], sources);
                record.moved_vertices[i] = copy;
                m_inset_vertices.emplace(
                    copy,
                    Inset_vertex{
                        .origin          = record.positions[i],
                        .direction       = direction * scale,
                        .depth_direction = record.normal
                    }
                );
            }
            m_edit_mesh.set_facet_vertices(record.facet, record.moved_vertices);
            for (std::size_t i = 0; i < n; ++i) {
                m_boundary.push_back(Boundary_edge{
                    .record = record_index,
                    .corner = i,
                    .a      = record.vertices[i],
                    .b      = record.vertices[(i + 1) % n]
                });
            }
        }
        place_and_finish();
    }

    // Places the inset vertices for the options' thickness and depth,
    // re-samples the moved facets' corners (interpolate), builds the rim
    // facets and carries the edge sharpness.
    void place_and_finish()
    {
        for (const std::pair<const GEO::index_t, Inset_vertex>& entry : m_inset_vertices) {
            const Inset_vertex& inset = entry.second;
            m_edit_mesh.set_position(
                entry.first,
                inset.origin + (m_options.thickness * inset.direction) + (m_options.depth * inset.depth_direction)
            );
        }

        if (m_options.interpolate) {
            std::vector<float>       weights;
            std::vector<Edit_source> sources;
            for (const Facet_record& record : m_records) {
                const std::size_t n = record.vertices.size();
                for (std::size_t i = 0; i < n; ++i) {
                    const GEO::vec3f position = m_edit_mesh.get_position(record.moved_vertices[i]);
                    if (same_position(position, record.positions[i])) {
                        continue;
                    }
                    // In the facet's pre-move plane.
                    const GEO::vec3f in_plane = position - (GEO::dot(position - record.positions[0], record.normal) * record.normal);
                    compute_mean_value_weights(record.positions, record.normal, in_plane, weights);
                    sources.clear();
                    for (std::size_t j = 0; j < n; ++j) {
                        accumulate_sources(sources, record.corner_sources[j], weights[j]);
                    }
                    m_edit_mesh.set_corner_sources(record.facet, static_cast<GEO::index_t>(i), sources);
                }
            }
        }

        // Rim facets: outer corners from the pre-move facet, inner corners
        // from the moved facet.
        std::vector<Edit_corner> corners;
        for (const Boundary_edge& edge : m_boundary) {
            const Facet_record&                record        = m_records[edge.record];
            const std::size_t                  n             = record.vertices.size();
            const std::size_t                  next          = (edge.corner + 1) % n;
            const std::span<const Edit_corner> moved_corners = m_edit_mesh.get_facet_corners(record.facet);
            corners.clear();
            corners.push_back(Edit_corner{.vertex = record.vertices[edge.corner], .sources = record.corner_sources[edge.corner]});
            corners.push_back(Edit_corner{.vertex = record.vertices[next],        .sources = record.corner_sources[next]});
            if (record.moved_vertices[next] != record.vertices[next]) {
                corners.push_back(moved_corners[next]);
            }
            if (record.moved_vertices[edge.corner] != record.vertices[edge.corner]) {
                corners.push_back(moved_corners[edge.corner]);
            }
            if (corners.size() < 3) {
                continue;
            }
            const GEO::index_t rim = m_edit_mesh.create_facet_from_corners(corners, m_edit_mesh.get_facet(record.facet).source_facet);
            if (rim == GEO::NO_INDEX) {
                log_operation->warn("inset_faces: rim facet of edge ({}, {}) was not created", edge.a, edge.b);
                continue;
            }
            m_rim_facets.push_back(rim);
        }

        // Sharpness: each facet edge's to its moved copy, and back onto the
        // original edge when the rim recreated it.
        for (const Facet_record& record : m_records) {
            const std::size_t n = record.vertices.size();
            for (std::size_t i = 0; i < n; ++i) {
                if (!record.sharpness[i].has_value()) {
                    continue;
                }
                const GEO::index_t moved = m_edit_mesh.find_edge(record.moved_vertices[i], record.moved_vertices[(i + 1) % n]);
                if (moved != GEO::NO_INDEX) {
                    m_edit_mesh.set_edge_sharpness(moved, record.sharpness[i]);
                }
                const GEO::index_t original = m_edit_mesh.find_edge(record.vertices[i], record.vertices[(i + 1) % n]);
                if (original != GEO::NO_INDEX) {
                    m_edit_mesh.set_edge_sharpness(original, record.sharpness[i]);
                }
            }
        }
    }

    static constexpr std::size_t s_no_record = static_cast<std::size_t>(-1);

    Inset_faces_options                  m_options;
    std::vector<std::uint8_t>            m_selected;         // per scratch facet
    std::vector<std::uint8_t>            m_region;           // per scratch facet (region mode)
    std::vector<std::size_t>             m_record_of_facet;  // per scratch facet (region mode)
    std::vector<Facet_record>            m_records;          // the moved facets
    std::vector<Boundary_edge>           m_boundary;         // one rim facet each
    std::map<GEO::index_t, GEO::vec3f>   m_split_directions; // region inset vertex -> scaled direction
    std::map<GEO::index_t, Inset_vertex> m_inset_vertices;   // keyed by scratch vertex
    std::vector<GEO::index_t>            m_rim_facets;       // scratch facets
    std::set<GEO::index_t>               m_dst_rim_facets;
};

} // anonymous namespace

void inset_faces(
    const Geometry&               source,
    Geometry&                     destination,
    const std::set<GEO::index_t>& selected_facets,
    const Inset_faces_options     options,
    Inset_faces_result* const     result,
    Component_remap* const        remap
)
{
    Inset_faces operation{source, destination, selected_facets, options};
    operation.build(result);
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
