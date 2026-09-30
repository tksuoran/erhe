#include "erhe_geometry/operation/subdivide_edges.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

namespace erhe::geometry::operation {

namespace {

using Vertex_pair = std::pair<GEO::index_t, GEO::index_t>;

[[nodiscard]] auto make_pair_sorted(const GEO::index_t a, const GEO::index_t b) -> Vertex_pair
{
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
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

enum class Fill_pattern : unsigned int
{
    none,
    quad_opposite,
    quad_corner,
    quad_three,
    quad_grid,
    tri_fan,
    tri_lattice,
    pair
};

// A facet holding at least one split edge, recorded before any split.
class Facet_plan
{
public:
    GEO::index_t              facet{GEO::NO_INDEX};
    std::vector<GEO::index_t> vertices;   // original corner vertices
    std::vector<std::uint8_t> split;      // split[i]: edge (vertices[i], vertices[i + 1]) is split
    Fill_pattern              pattern{Fill_pattern::none};
};

class Subdivide_edges : public Edit_mesh_operation
{
public:
    Subdivide_edges(
        const Geometry&                        source,
        Geometry&                              destination,
        const std::set<Vertex_pair>&           selected_edges,
        const Subdivide_edges_options&         options
    )
        : Edit_mesh_operation{source, destination}
        , m_options          {options}
        , m_cuts             {std::clamp(options.cuts, 1, 500)}
    {
        const GEO::index_t vertex_count = m_edit_mesh.get_vertex_slot_count();
        for (const Vertex_pair& edge : selected_edges) {
            if ((edge.first == edge.second) || (edge.first >= vertex_count) || (edge.second >= vertex_count)) {
                continue;
            }
            const Vertex_pair key = make_pair_sorted(edge.first, edge.second);
            if (m_edit_mesh.find_edge(key.first, key.second) == GEO::NO_INDEX) {
                continue;
            }
            m_cut_vertices.emplace(key, std::vector<GEO::index_t>{});
        }
    }

    void build(Subdivide_edges_result* result)
    {
        plan_facets();
        if (m_options.smoothness != 0.0f) {
            compute_vertex_normals();
        }
        split_edges();
        for (const Facet_plan& plan : m_plans) {
            fill(plan);
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

        // The general remap carries a selected source edge to its sub-edges
        // through the edge's new vertices, stored from the second (larger)
        // endpoint toward the first (see get_src_edge_new_vertex()).
        for (const std::pair<const Vertex_pair, std::vector<GEO::index_t>>& entry : m_cut_vertices) {
            std::vector<GEO::index_t>& dst_vertices = m_src_edge_to_dst_vertex[entry.first];
            dst_vertices.clear();
            for (std::vector<GEO::index_t>::const_reverse_iterator i = entry.second.rbegin(); i != entry.second.rend(); ++i) {
                dst_vertices.push_back(get_emitted_vertex(*i));
            }
        }

        if (result == nullptr) {
            return;
        }
        result->inner_vertices.clear();
        result->inner_edges.clear();
        result->inner_facets.clear();
        for (const GEO::index_t vertex : m_inner_vertices) {
            const GEO::index_t dst_vertex = get_emitted_vertex(vertex);
            if (dst_vertex != GEO::NO_INDEX) {
                result->inner_vertices.push_back(dst_vertex);
            }
        }
        for (const Vertex_pair& edge : m_inner_edges) {
            const GEO::index_t a = get_emitted_vertex(edge.first);
            const GEO::index_t b = get_emitted_vertex(edge.second);
            if ((a != GEO::NO_INDEX) && (b != GEO::NO_INDEX) && (a != b)) {
                result->inner_edges.push_back(make_pair_sorted(a, b));
            }
        }
        for (const GEO::index_t facet : m_inner_facets) {
            if ((facet < facet_to_dst.size()) && (facet_to_dst[facet] != GEO::NO_INDEX)) {
                result->inner_facets.push_back(facet_to_dst[facet]);
            }
        }
        const auto sort_unique = [](auto& values) {
            std::sort(values.begin(), values.end());
            values.erase(std::unique(values.begin(), values.end()), values.end());
        };
        sort_unique(result->inner_vertices);
        sort_unique(result->inner_edges);
        sort_unique(result->inner_facets);
    }

private:
    [[nodiscard]] auto is_split(const GEO::index_t a, const GEO::index_t b) const -> bool
    {
        return m_cut_vertices.contains(make_pair_sorted(a, b));
    }

    // The cut vertices of the split edge (a, b), ordered from a to b.
    [[nodiscard]] auto get_run(const GEO::index_t a, const GEO::index_t b) const -> std::vector<GEO::index_t>
    {
        const std::map<Vertex_pair, std::vector<GEO::index_t>>::const_iterator i = m_cut_vertices.find(make_pair_sorted(a, b));
        if (i == m_cut_vertices.end()) {
            return {};
        }
        std::vector<GEO::index_t> run = i->second; // ordered from the smaller vertex
        if (a > b) {
            std::reverse(run.begin(), run.end());
        }
        return run;
    }

    void plan_facets()
    {
        std::set<GEO::index_t> facets;
        for (const std::pair<const Vertex_pair, std::vector<GEO::index_t>>& entry : m_cut_vertices) {
            const GEO::index_t edge = m_edit_mesh.find_edge(entry.first.first, entry.first.second);
            for (const GEO::index_t facet : m_edit_mesh.get_edge(edge).facets) {
                facets.insert(facet);
            }
        }
        for (const GEO::index_t facet : facets) {
            Facet_plan& plan = m_plans.emplace_back();
            plan.facet = facet;
            for (const Edit_corner& corner : m_edit_mesh.get_facet_corners(facet)) {
                plan.vertices.push_back(corner.vertex);
            }
            const std::size_t n = plan.vertices.size();
            plan.split.assign(n, 0);
            std::size_t split_count = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (is_split(plan.vertices[i], plan.vertices[(i + 1) % n])) {
                    plan.split[i] = 1;
                    ++split_count;
                }
            }
            plan.pattern = choose_pattern(plan, split_count);
        }
    }

    [[nodiscard]] auto has_collinear_split_pair(const Facet_plan& plan) const -> bool
    {
        const std::size_t n = plan.vertices.size();
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t next = (i + 1) % n;
            if ((plan.split[i] == 0) || (plan.split[next] == 0)) {
                continue;
            }
            // Edges i and next share the vertex vertices[next].
            const GEO::vec3f p0 = m_edit_mesh.get_position(plan.vertices[i]);
            const GEO::vec3f p1 = m_edit_mesh.get_position(plan.vertices[next]);
            const GEO::vec3f p2 = m_edit_mesh.get_position(plan.vertices[(next + 1) % n]);
            const GEO::vec3f d0 = p1 - p0;
            const GEO::vec3f d1 = p2 - p1;
            const float      l0 = GEO::length(d0);
            const float      l1 = GEO::length(d1);
            if ((l0 == 0.0f) || (l1 == 0.0f)) {
                return true;
            }
            if (std::abs(GEO::dot(d0, d1) / (l0 * l1)) > (1.0f - 5e-5f)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto choose_pattern(const Facet_plan& plan, const std::size_t split_count) const -> Fill_pattern
    {
        const std::size_t n = plan.vertices.size();
        if (m_options.only_quads && (n != 4)) {
            return Fill_pattern::none;
        }
        if (has_collinear_split_pair(plan)) {
            return Fill_pattern::none;
        }
        if (n == 4) {
            switch (split_count) {
                case 2:  return (plan.split[0] == plan.split[2]) ? Fill_pattern::quad_opposite : Fill_pattern::quad_corner;
                case 3:  return Fill_pattern::quad_three;
                case 4:  return Fill_pattern::quad_grid;
                default: return Fill_pattern::none;
            }
        }
        if (n == 3) {
            switch (split_count) {
                case 1:  return Fill_pattern::tri_fan;
                case 2:  return Fill_pattern::pair;
                case 3:  return Fill_pattern::tri_lattice;
                default: return Fill_pattern::none;
            }
        }
        return (split_count == 2) ? Fill_pattern::pair : Fill_pattern::none;
    }

    void compute_vertex_normals()
    {
        const Mesh_attributes& attributes   = source.get_attributes();
        const GEO::index_t     vertex_count = source_mesh.vertices.nb();
        m_vertex_normals.assign(vertex_count, GEO::vec3f{0.0f, 0.0f, 0.0f});
        std::vector<std::uint8_t> found(vertex_count, 0);
        bool missing = false;
        for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
            std::optional<GEO::vec3f> normal = attributes.vertex_normal.try_get(vertex);
            if (!normal.has_value()) {
                normal = attributes.vertex_normal_smooth.try_get(vertex);
            }
            if (normal.has_value() && (GEO::length(normal.value()) > 0.0f)) {
                m_vertex_normals[vertex] = GEO::normalize(normal.value());
                found[vertex] = 1;
            } else {
                missing = true;
            }
        }
        if (!missing) {
            return;
        }
        // Fallback: the mean of the facet normals around the vertex.
        std::vector<GEO::vec3f> sums(vertex_count, GEO::vec3f{0.0f, 0.0f, 0.0f});
        for (const GEO::index_t facet : source_mesh.facets) {
            const GEO::vec3f facet_normal = mesh_facet_normalf(source_mesh, facet);
            for (GEO::index_t local_corner = 0; local_corner < source_mesh.facets.nb_corners(facet); ++local_corner) {
                sums[source_mesh.facets.vertex(facet, local_corner)] += facet_normal;
            }
        }
        for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
            if ((found[vertex] == 0) && (GEO::length(sums[vertex]) > 0.0f)) {
                m_vertex_normals[vertex] = GEO::normalize(sums[vertex]);
            }
        }
    }

    // Position of the cut at s along (a, b), with smoothness applied.
    [[nodiscard]] auto get_cut_position(const GEO::index_t a, const GEO::index_t b, const float s) const -> GEO::vec3f
    {
        const GEO::vec3f p_a      = m_edit_mesh.get_position(a);
        const GEO::vec3f p_b      = m_edit_mesh.get_position(b);
        const GEO::vec3f straight = ((1.0f - s) * p_a) + (s * p_b);
        if ((m_options.smoothness == 0.0f) || (a >= m_vertex_normals.size()) || (b >= m_vertex_normals.size())) {
            return straight;
        }
        const GEO::vec3f d      = p_b - p_a;
        const float      length = GEO::length(d);
        if (length == 0.0f) {
            return straight;
        }
        // End tangents: the edge vector projected onto each endpoint's
        // tangent plane, scaled to the edge length.
        const auto tangent = [&](const GEO::vec3f& normal) -> GEO::vec3f {
            const GEO::vec3f projected = d - (GEO::dot(d, normal) * normal);
            const float      projected_length = GEO::length(projected);
            return (projected_length > 0.0f) ? (projected * (length / projected_length)) : d;
        };
        const GEO::vec3f t_a = tangent(m_vertex_normals[a]);
        const GEO::vec3f t_b = tangent(m_vertex_normals[b]);
        const float s2  = s * s;
        const float s3  = s2 * s;
        const float h00 = (2.0f * s3) - (3.0f * s2) + 1.0f;
        const float h10 = s3 - (2.0f * s2) + s;
        const float h01 = (-2.0f * s3) + (3.0f * s2);
        const float h11 = s3 - s2;
        const GEO::vec3f curve = (h00 * p_a) + (h10 * t_a) + (h01 * p_b) + (h11 * t_b);
        return straight + (m_options.smoothness * (curve - straight));
    }

    void split_edges()
    {
        const float denominator = static_cast<float>(m_cuts + 1);
        for (std::pair<const Vertex_pair, std::vector<GEO::index_t>>& entry : m_cut_vertices) {
            const GEO::index_t a = entry.first.first;
            const GEO::index_t b = entry.first.second;
            std::vector<GEO::index_t>& run = entry.second;
            run.clear();
            GEO::index_t from = a;
            GEO::index_t edge = m_edit_mesh.find_edge(a, b);
            for (int k = 0; k < m_cuts; ++k) {
                // The remaining edge runs from `from` (at k / (cuts + 1)) to b;
                // the next cut lies 1 / (cuts + 1 - k) along it.
                const float t_from = 1.0f / static_cast<float>(m_cuts + 1 - k);
                const float t      = (m_edit_mesh.get_edge(edge).vertices[0] == from) ? t_from : (1.0f - t_from);
                const GEO::index_t vertex = m_edit_mesh.split_edge(edge, t);
                const float s = static_cast<float>(k + 1) / denominator;
                m_edit_mesh.set_position(vertex, get_cut_position(a, b, s));
                run.push_back(vertex);
                m_inner_vertices.push_back(vertex);
                from = vertex;
                edge = m_edit_mesh.find_edge(vertex, b);
            }
        }
    }

    // Splits the piece of the filled facet holding both vertices along a new
    // edge between them. False when no piece holds both non-adjacent.
    auto connect(std::vector<GEO::index_t>& pieces, const GEO::index_t u, const GEO::index_t v) -> bool
    {
        if ((u == v) || (m_edit_mesh.find_edge(u, v) != GEO::NO_INDEX)) {
            return false;
        }
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const GEO::index_t piece    = pieces[i];
            const GEO::index_t corner_u = m_edit_mesh.find_facet_corner(piece, u);
            const GEO::index_t corner_v = m_edit_mesh.find_facet_corner(piece, v);
            if ((corner_u == GEO::NO_INDEX) || (corner_v == GEO::NO_INDEX)) {
                continue;
            }
            const GEO::index_t new_facet = m_edit_mesh.split_facet(piece, corner_u, corner_v);
            if (new_facet == GEO::NO_INDEX) {
                return false;
            }
            pieces.push_back(new_facet);
            m_inner_edges.push_back(make_pair_sorted(u, v));
            return true;
        }
        return false;
    }

    // Connects run_a[j] to run_b[c - 1 - j] for every j.
    void connect_runs(std::vector<GEO::index_t>& pieces, const std::vector<GEO::index_t>& run_a, const std::vector<GEO::index_t>& run_b)
    {
        const std::size_t c = run_a.size();
        if (run_b.size() != c) {
            return;
        }
        for (std::size_t j = 0; j < c; ++j) {
            connect(pieces, run_a[j], run_b[c - 1 - j]);
        }
    }

    void finish_pieces(const std::vector<GEO::index_t>& pieces)
    {
        if (pieces.size() > 1) {
            m_inner_facets.insert(m_inner_facets.end(), pieces.begin(), pieces.end());
        }
    }

    void fill(const Facet_plan& plan)
    {
        if ((plan.pattern == Fill_pattern::none) || !m_edit_mesh.is_facet_alive(plan.facet)) {
            return;
        }
        const std::size_t n = plan.vertices.size();
        const auto vertex = [&](const std::size_t i) -> GEO::index_t { return plan.vertices[i % n]; };
        const auto run    = [&](const std::size_t i) -> std::vector<GEO::index_t> { return get_run(vertex(i), vertex(i + 1)); };
        const auto split  = [&](const std::size_t i) -> bool { return plan.split[i % n] != 0; };
        std::vector<GEO::index_t> pieces{plan.facet};
        const std::size_t c = static_cast<std::size_t>(m_cuts);

        switch (plan.pattern) {
            case Fill_pattern::quad_opposite: {
                const std::size_t i = split(0) ? 0 : 1;
                connect_runs(pieces, run(i), run(i + 2));
                break;
            }
            case Fill_pattern::quad_corner: {
                std::size_t i = 0;
                while (!(split(i) && split(i + 1))) {
                    ++i;
                }
                connect_runs(pieces, run(i), run(i + 1));
                connect(pieces, vertex(i), vertex(i + 2));
                break;
            }
            case Fill_pattern::quad_three: {
                std::size_t i = 0;
                while (split(i + 3)) {
                    ++i;
                }
                const std::vector<GEO::index_t> run_a = run(i);
                const std::vector<GEO::index_t> run_m = run(i + 1);
                const std::vector<GEO::index_t> run_b = run(i + 2);
                connect_runs(pieces, run_a, run_b);
                const std::size_t half = (c + 1) / 2;
                for (std::size_t k = 0; k < c; ++k) {
                    connect(pieces, run_m[k], (k < half) ? run_a[c - 1] : run_b[0]);
                }
                break;
            }
            case Fill_pattern::quad_grid: {
                fill_grid(plan, pieces);
                break;
            }
            case Fill_pattern::tri_fan: {
                std::size_t i = 0;
                while (!split(i)) {
                    ++i;
                }
                for (const GEO::index_t cut : run(i)) {
                    connect(pieces, cut, vertex(i + 2));
                }
                break;
            }
            case Fill_pattern::tri_lattice: {
                fill_lattice(plan, pieces);
                break;
            }
            case Fill_pattern::pair: {
                std::size_t first = n;
                std::size_t second = n;
                for (std::size_t i = 0; i < n; ++i) {
                    if (split(i)) {
                        if (first == n) {
                            first = i;
                        } else {
                            second = i;
                        }
                    }
                }
                connect_runs(pieces, run(first), run(second));
                break;
            }
            default: {
                break;
            }
        }
        finish_pieces(pieces);
    }

    [[nodiscard]] auto blend_sources(std::span<const GEO::index_t> vertices, std::span<const float> weights) const -> std::vector<Edit_source>
    {
        std::vector<Edit_source> sources;
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            accumulate_sources(sources, m_edit_mesh.get_vertex(vertices[i]).sources, weights[i]);
        }
        return sources;
    }

    void split_along_net(std::vector<GEO::index_t>& pieces, const std::vector<Vertex_pair>& net)
    {
        std::vector<GEO::index_t> out_facets;
        const Edgenet_result net_result = m_edit_mesh.split_facet_edgenet(pieces.front(), net, out_facets);
        if (net_result != Edgenet_result::split) {
            return;
        }
        pieces = out_facets;
        for (const Vertex_pair& edge : net) {
            m_inner_edges.push_back(make_pair_sorted(edge.first, edge.second));
        }
    }

    // Grid fill of a quad with four split edges: g(i, j), i, j in [0, c + 1],
    // g(0, 0) = corner 0, g(c + 1, 0) = corner 1, g(c + 1, c + 1) = corner 2,
    // g(0, c + 1) = corner 3.
    void fill_grid(const Facet_plan& plan, std::vector<GEO::index_t>& pieces)
    {
        const std::size_t c    = static_cast<std::size_t>(m_cuts);
        const std::size_t side = c + 2;
        std::vector<GEO::index_t> grid(side * side, GEO::NO_INDEX);
        const auto g = [&](const std::size_t i, const std::size_t j) -> GEO::index_t& { return grid[(j * side) + i]; };
        const std::array<GEO::index_t, 4> corners{plan.vertices[0], plan.vertices[1], plan.vertices[2], plan.vertices[3]};
        g(0, 0)         = corners[0];
        g(c + 1, 0)     = corners[1];
        g(c + 1, c + 1) = corners[2];
        g(0, c + 1)     = corners[3];
        const std::vector<GEO::index_t> run_0 = get_run(corners[0], corners[1]);
        const std::vector<GEO::index_t> run_1 = get_run(corners[1], corners[2]);
        const std::vector<GEO::index_t> run_2 = get_run(corners[2], corners[3]);
        const std::vector<GEO::index_t> run_3 = get_run(corners[3], corners[0]);
        for (std::size_t k = 0; k < c; ++k) {
            g(k + 1, 0)     = run_0[k];
            g(c + 1, k + 1) = run_1[k];
            g(c - k, c + 1) = run_2[k];
            g(0, c - k)     = run_3[k];
        }
        const std::array<GEO::vec3f, 4> p{
            m_edit_mesh.get_position(corners[0]), m_edit_mesh.get_position(corners[1]),
            m_edit_mesh.get_position(corners[2]), m_edit_mesh.get_position(corners[3])
        };
        const float denominator = static_cast<float>(c + 1);
        for (std::size_t j = 1; j <= c; ++j) {
            for (std::size_t i = 1; i <= c; ++i) {
                const float u = static_cast<float>(i) / denominator;
                const float v = static_cast<float>(j) / denominator;
                const std::array<float, 4> weights{(1.0f - u) * (1.0f - v), u * (1.0f - v), u * v, (1.0f - u) * v};
                // Coons patch over the boundary: equals the bilinear
                // interpolation of the corners while every run is straight.
                const GEO::vec3f position =
                    ((1.0f - v) * m_edit_mesh.get_position(g(i, 0))) + (v * m_edit_mesh.get_position(g(i, c + 1))) +
                    ((1.0f - u) * m_edit_mesh.get_position(g(0, j))) + (u * m_edit_mesh.get_position(g(c + 1, j))) -
                    ((weights[0] * p[0]) + (weights[1] * p[1]) + (weights[2] * p[2]) + (weights[3] * p[3]));
                const std::vector<Edit_source> sources = blend_sources(corners, weights);
                const GEO::index_t new_vertex = m_edit_mesh.add_vertex(position, sources);
                g(i, j) = new_vertex;
                m_inner_vertices.push_back(new_vertex);
            }
        }
        std::vector<Vertex_pair> net;
        for (std::size_t j = 1; j <= c; ++j) {
            for (std::size_t i = 0; i <= c; ++i) {
                net.emplace_back(g(i, j), g(i + 1, j));
            }
        }
        for (std::size_t i = 1; i <= c; ++i) {
            for (std::size_t j = 0; j <= c; ++j) {
                net.emplace_back(g(i, j), g(i, j + 1));
            }
        }
        split_along_net(pieces, net);
    }

    // Lattice fill of a triangle with three split edges: l(i, j) with
    // i + j <= c + 1 at corner 0 + i / (c + 1) (corner 1 - corner 0) +
    // j / (c + 1) (corner 2 - corner 0).
    void fill_lattice(const Facet_plan& plan, std::vector<GEO::index_t>& pieces)
    {
        const std::size_t c    = static_cast<std::size_t>(m_cuts);
        const std::size_t side = c + 2;
        std::vector<GEO::index_t> lattice(side * side, GEO::NO_INDEX);
        const auto l = [&](const std::size_t i, const std::size_t j) -> GEO::index_t& { return lattice[(j * side) + i]; };
        const std::array<GEO::index_t, 3> corners{plan.vertices[0], plan.vertices[1], plan.vertices[2]};
        l(0, 0)     = corners[0];
        l(c + 1, 0) = corners[1];
        l(0, c + 1) = corners[2];
        const std::vector<GEO::index_t> run_0 = get_run(corners[0], corners[1]);
        const std::vector<GEO::index_t> run_1 = get_run(corners[1], corners[2]);
        const std::vector<GEO::index_t> run_2 = get_run(corners[2], corners[0]);
        for (std::size_t k = 0; k < c; ++k) {
            l(k + 1, 0)     = run_0[k];
            l(c - k, k + 1) = run_1[k];
            l(0, c - k)     = run_2[k];
        }
        const std::array<GEO::vec3f, 3> p{
            m_edit_mesh.get_position(corners[0]), m_edit_mesh.get_position(corners[1]), m_edit_mesh.get_position(corners[2])
        };
        const float denominator = static_cast<float>(c + 1);
        for (std::size_t j = 1; j <= c; ++j) {
            for (std::size_t i = 1; (i + j) <= c; ++i) {
                const float w1 = static_cast<float>(i) / denominator;
                const float w2 = static_cast<float>(j) / denominator;
                const std::array<float, 3> weights{1.0f - w1 - w2, w1, w2};
                const GEO::vec3f position = (weights[0] * p[0]) + (weights[1] * p[1]) + (weights[2] * p[2]);
                const std::vector<Edit_source> sources = blend_sources(corners, weights);
                const GEO::index_t new_vertex = m_edit_mesh.add_vertex(position, sources);
                l(i, j) = new_vertex;
                m_inner_vertices.push_back(new_vertex);
            }
        }
        // Every lattice edge not on the triangle's boundary.
        const std::size_t last = c + 1;
        std::vector<Vertex_pair> net;
        for (std::size_t j = 0; j <= last; ++j) {
            for (std::size_t i = 0; (i + j) <= last; ++i) {
                if ((i + j + 1) > last) {
                    continue;
                }
                if (j > 0) {
                    net.emplace_back(l(i, j), l(i + 1, j));         // along corner 0 -> 1
                }
                if (i > 0) {
                    net.emplace_back(l(i, j), l(i, j + 1));         // along corner 0 -> 2
                }
                if ((i + j + 1) < last) {
                    net.emplace_back(l(i + 1, j), l(i, j + 1));     // along corner 1 -> 2
                }
            }
        }
        split_along_net(pieces, net);
    }

    Subdivide_edges_options                          m_options;
    int                                              m_cuts{1};
    std::map<Vertex_pair, std::vector<GEO::index_t>> m_cut_vertices; // ordered from the smaller vertex
    std::vector<Facet_plan>                          m_plans;
    std::vector<GEO::vec3f>                          m_vertex_normals;
    std::vector<GEO::index_t>                        m_inner_vertices;
    std::vector<Vertex_pair>                         m_inner_edges;
    std::vector<GEO::index_t>                        m_inner_facets;
};

} // anonymous namespace

void get_selection_edges(
    const Geometry&                     geometry,
    const Geometry_component_selection& selection,
    std::set<Vertex_pair>&              out_edges
)
{
    out_edges.clear();
    const GEO::Mesh&   mesh         = geometry.get_mesh();
    const GEO::index_t vertex_count = mesh.vertices.nb();
    const auto add = [&](const GEO::index_t a, const GEO::index_t b) {
        if ((a == b) || (a >= vertex_count) || (b >= vertex_count)) {
            return;
        }
        if (geometry.get_edge(a, b) == GEO::NO_EDGE) {
            return;
        }
        out_edges.insert(make_pair_sorted(a, b));
    };
    for (const Vertex_pair& edge : selection.edges) {
        add(edge.first, edge.second);
    }
    for (const GEO::index_t facet : selection.facets) {
        if (facet >= mesh.facets.nb()) {
            continue;
        }
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
            add(mesh.facets.vertex(facet, local_corner), mesh.facets.vertex(facet, (local_corner + 1) % corner_count));
        }
    }
    if (!selection.vertices.empty()) {
        for (const GEO::index_t edge : mesh.edges) {
            const GEO::index_t a = mesh.edges.vertex(edge, 0);
            const GEO::index_t b = mesh.edges.vertex(edge, 1);
            if (selection.vertices.contains(a) && selection.vertices.contains(b)) {
                add(a, b);
            }
        }
    }
}

void subdivide_edges(
    const Geometry&                    source,
    Geometry&                          destination,
    const std::set<Vertex_pair>&       selected_edges,
    const Subdivide_edges_options      options,
    Subdivide_edges_result* const      result,
    Component_remap* const             remap
)
{
    Subdivide_edges operation{source, destination, selected_edges, options};
    operation.build(result);
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
