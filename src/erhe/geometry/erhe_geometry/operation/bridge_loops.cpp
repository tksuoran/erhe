#include "erhe_geometry/operation/bridge_loops.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/subdivide_edges.hpp"
#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
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

// The area vector of a closed vertex cycle (Newell, not normalized).
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

[[nodiscard]] auto normalized_or_zero(const GEO::vec3f& v) -> GEO::vec3f
{
    const float length = GEO::length(v);
    return (length > 1e-12f) ? (v / length) : GEO::vec3f{0.0f, 0.0f, 0.0f};
}

// Index sequence of length target over count entries (count <= target), each
// entry at least once, in order: every entry doubled while that stays within
// target, the remaining repeats spread evenly.
void expand_indices(const std::size_t count, const std::size_t target, std::vector<std::size_t>& out_indices)
{
    out_indices.clear();
    std::vector<std::size_t> repeats(count, 1);
    std::size_t total = count;
    while ((total * 2) <= target) {
        for (std::size_t& repeat : repeats) {
            repeat *= 2;
        }
        total *= 2;
    }
    const std::size_t extra = target - total;
    for (std::size_t k = 0; k < extra; ++k) {
        const std::size_t index = (((2 * k) + 1) * count) / (2 * extra);
        repeats[std::min(index, count - 1)] += 1;
    }
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t r = 0; r < repeats[i]; ++r) {
            out_indices.push_back(i);
        }
    }
}

class Loop
{
public:
    std::vector<GEO::index_t> vertices;
    bool                      closed{false};
    GEO::vec3f                centre{0.0f, 0.0f, 0.0f};
};

enum class Step : unsigned int
{
    advance_a,
    advance_b
};

// A position in the strip: the rung between a[a_index] and b[b_index].
class Rung
{
public:
    std::size_t a_index{0};
    std::size_t b_index{0};
};

class Facet_plan
{
public:
    std::vector<GEO::index_t> vertices;
    std::vector<Vertex_pair>  corner_edges; // per corner: the loop edge whose facet gives the corner
    Vertex_pair               facet_edge{GEO::NO_INDEX, GEO::NO_INDEX};
};

////////////////////////////////////////////////////////////////////////////////

class Bridge : public Edit_mesh_operation
{
public:
    Bridge(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
    }

    void build(const Geometry_component_selection& selection, const Bridge_loops_options& options)
    {
        m_options = options;
        m_bridged = bridge(selection);
        if (!m_bridged) {
            // Every error leaves the source unchanged, also after the facets
            // of a facet selection were deleted.
            m_edit_mesh.load(source);
            m_new_facets.clear();
            m_rungs.clear();
            m_merged_vertices.clear();
            m_merged_edges.clear();
        }
        emit();
    }

    void make_result(Bridge_loops_result* result, Component_remap* remap) const
    {
        std::vector<GEO::index_t> facet_to_dst(m_edit_mesh.get_facet_slot_count(), GEO::NO_INDEX);
        GEO::index_t next = 0;
        for (GEO::index_t facet = 0, end = m_edit_mesh.get_facet_slot_count(); facet < end; ++facet) {
            if (m_edit_mesh.is_facet_alive(facet)) {
                facet_to_dst[facet] = next++;
            }
        }
        if (result != nullptr) {
            result->bridge_facets.clear();
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
                result->bridge_facets.push_back(facet_to_dst[facet]);
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
        if (!has_remap) {
            return;
        }
        for (const GEO::index_t vertex : m_merged_vertices) {
            if (m_edit_mesh.is_vertex_alive(vertex)) {
                remap->destination->vertices.insert(get_emitted_vertex(vertex));
            }
        }
        for (const Vertex_pair& pair : m_merged_edges) {
            if (!m_edit_mesh.is_vertex_alive(pair.first) || !m_edit_mesh.is_vertex_alive(pair.second)) {
                continue;
            }
            const GEO::index_t edge = m_edit_mesh.find_edge(pair.first, pair.second);
            if ((edge != GEO::NO_INDEX) && (m_edit_mesh.get_edge_facet_count(edge) > 0)) {
                remap->destination->edges.insert(make_vertex_pair(get_emitted_vertex(pair.first), get_emitted_vertex(pair.second)));
            }
        }
    }

    // After emit(): the rungs (edges between the two loops of a pair) in
    // destination vertex pairs (first < second).
    void get_rungs(std::set<Vertex_pair>& out_rungs) const
    {
        out_rungs.clear();
        for (const Vertex_pair& rung : m_rungs) {
            if (!m_edit_mesh.is_vertex_alive(rung.first) || !m_edit_mesh.is_vertex_alive(rung.second)) {
                continue;
            }
            const GEO::index_t edge = m_edit_mesh.find_edge(rung.first, rung.second);
            if ((edge == GEO::NO_INDEX) || (m_edit_mesh.get_edge_facet_count(edge) == 0)) {
                continue;
            }
            out_rungs.insert(make_vertex_pair(get_emitted_vertex(rung.first), get_emitted_vertex(rung.second)));
        }
    }

    [[nodiscard]] auto is_bridged() const -> bool
    {
        return m_bridged;
    }

private:
    [[nodiscard]] auto bridge(const Geometry_component_selection& selection) -> bool
    {
        std::vector<Loop> loops;
        if (!selection.facets.empty()) {
            if (!gather_facet_loops(selection, loops)) {
                return false;
            }
        } else if (!gather_edge_loops(selection, loops)) {
            return false;
        }
        if (loops.size() < 2) {
            log_operation->warn("bridge loops: {} loop(s) selected, at least two are needed", loops.size());
            return false;
        }

        // Loop edges must have at most one facet, and each loop edge
        // remembers that facet (corners and winding come from it).
        m_loop_edge_facet.clear();
        for (const Loop& loop : loops) {
            const std::size_t n = loop.vertices.size();
            const std::size_t edge_count = loop.closed ? n : (n - 1);
            for (std::size_t i = 0; i < edge_count; ++i) {
                const GEO::index_t u    = loop.vertices[i];
                const GEO::index_t v    = loop.vertices[(i + 1) % n];
                const GEO::index_t edge = m_edit_mesh.find_edge(u, v);
                if (edge == GEO::NO_INDEX) {
                    log_operation->warn("bridge loops: loop edge {} - {} does not exist", u, v);
                    return false;
                }
                const std::vector<GEO::index_t>& edge_facets = m_edit_mesh.get_edge(edge).facets;
                if (edge_facets.size() >= 2) {
                    log_operation->warn("bridge loops: edge {} - {} already has two faces", u, v);
                    return false;
                }
                if (!edge_facets.empty()) {
                    m_loop_edge_facet[make_vertex_pair(u, v)] = edge_facets.front();
                }
            }
        }
        for (Loop& loop : loops) {
            GEO::vec3f sum{0.0f, 0.0f, 0.0f};
            for (const GEO::index_t vertex : loop.vertices) {
                sum += m_edit_mesh.get_position(vertex);
            }
            loop.centre = sum / static_cast<float>(loop.vertices.size());
        }

        // Order by proximity: from the first loop, the nearest centre next.
        std::vector<std::size_t> order{0};
        std::vector<std::uint8_t> used(loops.size(), 0);
        used[0] = 1;
        while (order.size() < loops.size()) {
            const GEO::vec3f last = loops[order.back()].centre;
            std::size_t best          = loops.size();
            float       best_distance = std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < loops.size(); ++i) {
                if (used[i] != 0) {
                    continue;
                }
                const float d = GEO::length(loops[i].centre - last);
                if (d < best_distance) {
                    best_distance = d;
                    best          = i;
                }
            }
            used[best] = 1;
            order.push_back(best);
        }

        std::vector<std::pair<std::size_t, std::size_t>> pairs;
        switch (m_options.connection) {
            case Bridge_connection::open_loop:
            case Bridge_connection::closed_loop: {
                for (std::size_t i = 0; (i + 1) < order.size(); ++i) {
                    pairs.emplace_back(order[i], order[i + 1]);
                }
                if ((m_options.connection == Bridge_connection::closed_loop) && (order.size() >= 3)) {
                    pairs.emplace_back(order.back(), order.front());
                }
                break;
            }
            case Bridge_connection::loop_pairs: {
                for (std::size_t i = 0; (i + 1) < order.size(); i += 2) {
                    pairs.emplace_back(order[i], order[i + 1]);
                }
                if ((order.size() % 2) != 0) {
                    log_operation->warn("bridge loops: loop pairs with {} loops, the last loop stays unbridged", order.size());
                }
                break;
            }
            default: {
                return false;
            }
        }

        if (m_options.merge) {
            for (const std::pair<std::size_t, std::size_t>& pair : pairs) {
                const Loop& a = loops[pair.first];
                const Loop& b = loops[pair.second];
                if ((a.closed != b.closed) || (a.vertices.size() != b.vertices.size())) {
                    log_operation->warn("bridge loops: merge needs loops of equal length and kind ({} and {} vertices)", a.vertices.size(), b.vertices.size());
                    return false;
                }
            }
        }

        bool bridged_any = false;
        for (const std::pair<std::size_t, std::size_t>& pair : pairs) {
            bridged_any = bridge_pair(loops[pair.first], loops[pair.second]) || bridged_any;
        }
        if (!bridged_any) {
            log_operation->warn("bridge loops: nothing bridged");
        }
        return bridged_any;
    }

    // Facet selection: the boundaries of the selected regions, recorded before
    // the facets are deleted.
    [[nodiscard]] auto gather_facet_loops(const Geometry_component_selection& selection, std::vector<Loop>& out_loops) -> bool
    {
        const GEO::index_t facet_slot_count = m_edit_mesh.get_facet_slot_count();
        std::vector<std::uint8_t> selected(facet_slot_count, 0);
        std::vector<GEO::index_t> facets;
        for (const GEO::index_t facet : selection.facets) {
            if ((facet < facet_slot_count) && m_edit_mesh.is_facet_alive(facet)) {
                selected[facet] = 1;
                facets.push_back(facet);
            }
        }
        if (facets.empty()) {
            log_operation->warn("bridge loops: no selected face exists");
            return false;
        }
        std::set<Vertex_pair> boundary;
        for (const GEO::index_t facet : facets) {
            const std::span<const Edit_corner> corners = m_edit_mesh.get_facet_corners(facet);
            const std::size_t n = corners.size();
            for (std::size_t i = 0; i < n; ++i) {
                const GEO::index_t u    = corners[i].vertex;
                const GEO::index_t v    = corners[(i + 1) % n].vertex;
                const GEO::index_t edge = m_edit_mesh.find_edge(u, v);
                if (edge == GEO::NO_INDEX) {
                    continue;
                }
                for (const GEO::index_t other : m_edit_mesh.get_edge(edge).facets) {
                    if (selected[other] == 0) {
                        boundary.insert(make_vertex_pair(u, v));
                        break;
                    }
                }
            }
        }
        if (!make_loops(boundary, out_loops)) {
            return false;
        }
        if (out_loops.size() < 2) {
            return true; // the caller reports it; nothing deleted yet
        }
        m_edit_mesh.delete_elements(facets, Delete_context::faces);
        return true;
    }

    // Edge or vertex selection: the selected edges plus every edge between
    // two selected vertices.
    [[nodiscard]] auto gather_edge_loops(const Geometry_component_selection& selection, std::vector<Loop>& out_loops) -> bool
    {
        const GEO::index_t vertex_slot_count = m_edit_mesh.get_vertex_slot_count();
        std::set<Vertex_pair> edges;
        for (const GEO::index_t vertex : selection.vertices) {
            if ((vertex >= vertex_slot_count) || !m_edit_mesh.is_vertex_alive(vertex)) {
                continue;
            }
            for (const GEO::index_t edge : m_edit_mesh.get_vertex_edges(vertex)) {
                const GEO::index_t other = m_edit_mesh.get_edge_other_vertex(edge, vertex);
                if (selection.vertices.contains(other)) {
                    edges.insert(make_vertex_pair(vertex, other));
                }
            }
        }
        for (const Vertex_pair& pair : selection.edges) {
            if ((pair.first >= vertex_slot_count) || (pair.second >= vertex_slot_count)) {
                continue;
            }
            if (m_edit_mesh.find_edge(pair.first, pair.second) != GEO::NO_INDEX) {
                edges.insert(make_vertex_pair(pair.first, pair.second));
            }
        }
        return make_loops(edges, out_loops);
    }

    // The edges as loops: open chains from their lower-numbered end first,
    // then closed cycles. False (logged) when a vertex has more than two
    // loop edges.
    [[nodiscard]] auto make_loops(const std::set<Vertex_pair>& edges, std::vector<Loop>& out_loops) const -> bool
    {
        out_loops.clear();
        std::map<GEO::index_t, std::vector<GEO::index_t>> adjacency;
        for (const Vertex_pair& edge : edges) {
            adjacency[edge.first].push_back(edge.second);
            adjacency[edge.second].push_back(edge.first);
        }
        for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            if (entry.second.size() > 2) {
                log_operation->warn("bridge loops: vertex {} has {} selected loop edges, at most two are allowed", entry.first, entry.second.size());
                return false;
            }
        }
        std::set<GEO::index_t> visited;
        const auto walk = [&](const GEO::index_t start, Loop& loop) {
            GEO::index_t previous = GEO::NO_INDEX;
            GEO::index_t current  = start;
            for (;;) {
                loop.vertices.push_back(current);
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
                Loop loop{};
                walk(entry.first, loop);
                out_loops.push_back(std::move(loop));
            }
        }
        for (const std::pair<const GEO::index_t, std::vector<GEO::index_t>>& entry : adjacency) {
            if (!visited.contains(entry.first)) {
                Loop loop{};
                loop.closed = true;
                walk(entry.first, loop);
                out_loops.push_back(std::move(loop));
            }
        }
        return true;
    }

    // Whether the facets along the loop traverse it in its vertex order
    // (majority over its edges); nullopt on a tie.
    [[nodiscard]] auto get_facet_direction(const std::span<const GEO::index_t> vertices, const bool closed) const -> std::optional<bool>
    {
        const std::size_t n = vertices.size();
        const std::size_t edge_count = closed ? n : (n - 1);
        std::size_t forward  = 0;
        std::size_t backward = 0;
        for (std::size_t i = 0; i < edge_count; ++i) {
            const GEO::index_t u = vertices[i];
            const GEO::index_t v = vertices[(i + 1) % n];
            const std::map<Vertex_pair, GEO::index_t>::const_iterator it = m_loop_edge_facet.find(make_vertex_pair(u, v));
            if (it == m_loop_edge_facet.end()) {
                continue;
            }
            if (traverses(m_edit_mesh, it->second, u, v)) {
                ++forward;
            } else {
                ++backward;
            }
        }
        if (forward == backward) {
            return std::nullopt;
        }
        return forward > backward;
    }

    // Whether loop b must be reversed to run alongside loop a.
    [[nodiscard]] auto should_flip(const std::vector<GEO::index_t>& a, const std::vector<GEO::index_t>& b, const bool closed, const GEO::vec3f& centre_a, const GEO::vec3f& centre_b) const -> bool
    {
        constexpr float decisive = 0.1f;
        const auto facet_rule = [&]() -> std::optional<bool> {
            const std::optional<bool> direction_a = get_facet_direction(a, closed);
            const std::optional<bool> direction_b = get_facet_direction(b, closed);
            if (!direction_a.has_value() || !direction_b.has_value()) {
                return std::nullopt;
            }
            // A consistently wound bridge needs the facets along a and b to
            // run in opposite directions relative to the pairing.
            return direction_a.value() == direction_b.value();
        };
        if (!closed) {
            const GEO::vec3f direction_a = normalized_or_zero(m_edit_mesh.get_position(a.back()) - m_edit_mesh.get_position(a.front()));
            const GEO::vec3f direction_b = normalized_or_zero(m_edit_mesh.get_position(b.back()) - m_edit_mesh.get_position(b.front()));
            const float cosine = GEO::dot(direction_a, direction_b);
            if (std::abs(cosine) > decisive) {
                return cosine < 0.0f;
            }
            return facet_rule().value_or(false);
        }
        const GEO::vec3f normal_a = normalized_or_zero(newell_vector(m_edit_mesh, a));
        const GEO::vec3f normal_b = normalized_or_zero(newell_vector(m_edit_mesh, b));
        const GEO::vec3f between  = normalized_or_zero(centre_b - centre_a);
        const float side_a = GEO::dot(normal_a, between);
        const float side_b = GEO::dot(normal_b, between);
        if ((std::abs(side_a) > decisive) && (std::abs(side_b) > decisive)) {
            return (side_a > 0.0f) != (side_b > 0.0f);
        }
        const std::optional<bool> by_facets = facet_rule();
        if (by_facets.has_value()) {
            return by_facets.value();
        }
        return GEO::dot(normal_a, normal_b) < 0.0f;
    }

    [[nodiscard]] auto rung_length(const std::vector<GEO::index_t>& a, const std::vector<GEO::index_t>& b, const Rung& rung) const -> float
    {
        return GEO::length(m_edit_mesh.get_position(a[rung.a_index]) - m_edit_mesh.get_position(b[rung.b_index]));
    }

    // The loop edge whose facet gives the corner of a loop vertex that does
    // not advance in a step: the edge after it, else the one before it.
    [[nodiscard]] static auto get_apex_edge(const std::vector<GEO::index_t>& loop, const std::size_t index, const bool closed) -> Vertex_pair
    {
        const std::size_t n = loop.size();
        if (closed || ((index + 1) < n)) {
            return make_vertex_pair(loop[index], loop[(index + 1) % n]);
        }
        return make_vertex_pair(loop[index - 1], loop[index]);
    }

    [[nodiscard]] auto bridge_pair(const Loop& loop_a, const Loop& loop_b) -> bool
    {
        if (loop_a.closed != loop_b.closed) {
            log_operation->warn("bridge loops: an open and a closed loop cannot be bridged");
            return false;
        }
        const bool closed = loop_a.closed;
        // A loop bridged by an earlier pair has edges with two facets now.
        for (const Loop* loop : {&loop_a, &loop_b}) {
            const std::size_t n = loop->vertices.size();
            const std::size_t edge_count = closed ? n : (n - 1);
            for (std::size_t i = 0; i < edge_count; ++i) {
                const GEO::index_t edge = m_edit_mesh.find_edge(loop->vertices[i], loop->vertices[(i + 1) % n]);
                if ((edge == GEO::NO_INDEX) || (m_edit_mesh.get_edge_facet_count(edge) >= 2)) {
                    log_operation->warn("bridge loops: a loop already bridged by an earlier pair is skipped (its edges have two faces)");
                    return false;
                }
            }
        }

        const std::vector<GEO::index_t>& a = loop_a.vertices;
        std::vector<GEO::index_t>        b = loop_b.vertices;
        if (should_flip(a, b, closed, loop_a.centre, loop_b.centre)) {
            std::reverse(b.begin(), b.end());
        }
        const std::size_t count_a = a.size();
        const std::size_t count_b = b.size();
        const std::size_t length  = std::max(count_a, count_b);

        // Rung sequences: rung r pairs a[index_a[r]] with b[index_b[r]].
        std::vector<std::size_t> index_a;
        std::vector<std::size_t> base_b;
        if (count_a < length) {
            expand_indices(count_a, length, index_a);
        } else {
            expand_indices(count_a, count_a, index_a);
        }
        if (count_b < length) {
            expand_indices(count_b, length, base_b);
        } else {
            expand_indices(count_b, count_b, base_b);
        }
        std::vector<std::size_t> index_b(length);
        const auto rotate_b = [&](const std::size_t rotation) {
            for (std::size_t r = 0; r < length; ++r) {
                index_b[r] = (base_b[r] + rotation) % count_b;
            }
        };
        std::size_t rotation = 0;
        if (closed) {
            float best_cost = std::numeric_limits<float>::max();
            for (std::size_t candidate = 0; candidate < count_b; ++candidate) {
                rotate_b(candidate);
                float cost = 0.0f;
                for (std::size_t r = 0; r < length; ++r) {
                    cost += rung_length(a, b, Rung{index_a[r], index_b[r]});
                }
                if (cost < (best_cost - 1e-6f)) {
                    best_cost = cost;
                    rotation  = candidate;
                }
            }
            const long long n     = static_cast<long long>(count_b);
            const long long twist = ((static_cast<long long>(m_options.twist_offset) % n) + n) % n;
            rotation = (rotation + static_cast<std::size_t>(twist)) % count_b;
        }
        rotate_b(rotation);

        if (m_options.merge) {
            merge_pair(a, b, index_a, index_b, closed);
            return true;
        }

        // Steps between consecutive rungs; a step advancing both loops is a
        // quad, split into an a-step and a b-step for the beautify.
        std::vector<Step> steps;
        const std::size_t segment_count = closed ? length : (length - 1);
        for (std::size_t s = 0; s < segment_count; ++s) {
            const std::size_t next = (s + 1) % length;
            if (index_a[next] != index_a[s]) {
                steps.push_back(Step::advance_a);
            }
            if (index_b[next] != index_b[s]) {
                steps.push_back(Step::advance_b);
            }
        }
        const auto advance = [&](Rung& rung, const Step step) {
            if (step == Step::advance_a) {
                rung.a_index = (rung.a_index + 1) % count_a;
            } else {
                rung.b_index = (rung.b_index + 1) % count_b;
            }
        };
        const Rung first_rung{index_a[0], index_b[0]};

        // Beautify (unequal counts): two neighbouring steps of different
        // loops swap when the rung between them gets shorter.
        if (count_a != count_b) {
            bool        changed = true;
            std::size_t guard   = 0;
            while (changed && (guard++ < (4 * steps.size()))) {
                changed = false;
                Rung rung = first_rung;
                for (std::size_t k = 0; (k + 1) < steps.size(); ++k) {
                    advance(rung, steps[k]);
                    if (steps[k] == steps[k + 1]) {
                        continue;
                    }
                    Rung swapped = rung;
                    if (steps[k] == Step::advance_a) {
                        swapped.a_index = (rung.a_index + count_a - 1) % count_a;
                        swapped.b_index = (rung.b_index + 1) % count_b;
                    } else {
                        swapped.a_index = (rung.a_index + 1) % count_a;
                        swapped.b_index = (rung.b_index + count_b - 1) % count_b;
                    }
                    if (rung_length(a, b, swapped) < (rung_length(a, b, rung) - 1e-6f)) {
                        std::swap(steps[k], steps[k + 1]);
                        rung    = swapped;
                        changed = true;
                    }
                }
            }
        }

        // Group the steps into facets: two steps of different loops make a
        // quad, a lone step a triangle.
        std::vector<Facet_plan> plans;
        std::size_t reverse_votes = 0;
        std::size_t keep_votes    = 0;
        const auto vote = [&](const GEO::index_t u, const GEO::index_t v) {
            // The bridge facet traverses u -> v; the facet that had the loop
            // edge must traverse v -> u.
            const std::map<Vertex_pair, GEO::index_t>::const_iterator it = m_loop_edge_facet.find(make_vertex_pair(u, v));
            if (it == m_loop_edge_facet.end()) {
                return;
            }
            if (traverses(m_edit_mesh, it->second, u, v)) {
                ++reverse_votes;
            } else {
                ++keep_votes;
            }
        };
        Rung rung = first_rung;
        for (std::size_t k = 0; k < steps.size();) {
            const std::size_t take = (((k + 1) < steps.size()) && (steps[k] != steps[k + 1])) ? 2 : 1;
            const Rung start = rung;
            for (std::size_t i = 0; i < take; ++i) {
                advance(rung, steps[k + i]);
            }
            const Rung end = rung;
            k += take;
            m_rungs.push_back(make_vertex_pair(a[start.a_index], b[start.b_index]));

            const bool advances_a = (start.a_index != end.a_index);
            const bool advances_b = (start.b_index != end.b_index);
            const Vertex_pair edge_a = advances_a ? make_vertex_pair(a[start.a_index], a[end.a_index]) : get_apex_edge(a, start.a_index, closed);
            const Vertex_pair edge_b = advances_b ? make_vertex_pair(b[start.b_index], b[end.b_index]) : get_apex_edge(b, start.b_index, closed);
            // Canonical orientation: a[end] -> a[start] -> b[start] -> b[end].
            Facet_plan plan{};
            plan.facet_edge = edge_a;
            plan.vertices.push_back(a[end.a_index]);
            plan.corner_edges.push_back(edge_a);
            if (advances_a) {
                plan.vertices.push_back(a[start.a_index]);
                plan.corner_edges.push_back(edge_a);
                vote(a[end.a_index], a[start.a_index]);
            }
            plan.vertices.push_back(b[start.b_index]);
            plan.corner_edges.push_back(edge_b);
            if (advances_b) {
                plan.vertices.push_back(b[end.b_index]);
                plan.corner_edges.push_back(edge_b);
                vote(b[start.b_index], b[end.b_index]);
            }
            plans.push_back(std::move(plan));
        }
        if (!closed) {
            m_rungs.push_back(make_vertex_pair(a[rung.a_index], b[rung.b_index]));
        }

        const bool reverse = (reverse_votes > keep_votes);
        bool created_any = false;
        for (Facet_plan& plan : plans) {
            if (reverse) {
                std::reverse(plan.vertices.begin(), plan.vertices.end());
                std::reverse(plan.corner_edges.begin(), plan.corner_edges.end());
            }
            created_any = create_bridge_facet(plan) || created_any;
        }
        return created_any;
    }

    [[nodiscard]] auto create_bridge_facet(const Facet_plan& plan) -> bool
    {
        const std::size_t n = plan.vertices.size();
        if (n < 3) {
            return false;
        }
        for (std::size_t i = 0; i < n; ++i) {
            const GEO::index_t edge = m_edit_mesh.find_edge(plan.vertices[i], plan.vertices[(i + 1) % n]);
            if ((edge != GEO::NO_INDEX) && (m_edit_mesh.get_edge_facet_count(edge) >= 2)) {
                log_operation->warn("bridge loops: a bridge face would give edge {} - {} a third face; skipped", plan.vertices[i], plan.vertices[(i + 1) % n]);
                return false;
            }
        }
        std::vector<Edit_corner> corners(n);
        for (std::size_t i = 0; i < n; ++i) {
            const GEO::index_t vertex = plan.vertices[i];
            corners[i].vertex = vertex;
            GEO::index_t from_facet = GEO::NO_INDEX;
            const std::map<Vertex_pair, GEO::index_t>::const_iterator it = m_loop_edge_facet.find(plan.corner_edges[i]);
            if ((it != m_loop_edge_facet.end()) && m_edit_mesh.is_facet_alive(it->second)) {
                from_facet = it->second;
            } else if (!m_edit_mesh.get_vertex_facets(vertex).empty()) {
                from_facet = m_edit_mesh.get_vertex_facets(vertex).front();
            }
            if (from_facet != GEO::NO_INDEX) {
                const GEO::index_t local_corner = m_edit_mesh.find_facet_corner(from_facet, vertex);
                if (local_corner != GEO::NO_INDEX) {
                    corners[i].sources = m_edit_mesh.get_facet_corners(from_facet)[local_corner].sources;
                }
            }
        }
        GEO::index_t source_facet = GEO::NO_INDEX;
        const std::map<Vertex_pair, GEO::index_t>::const_iterator it = m_loop_edge_facet.find(plan.facet_edge);
        if ((it != m_loop_edge_facet.end()) && m_edit_mesh.is_facet_alive(it->second)) {
            source_facet = m_edit_mesh.get_facet(it->second).source_facet;
        }
        const GEO::index_t facet = m_edit_mesh.create_facet_from_corners(corners, source_facet);
        if (facet == GEO::NO_INDEX) {
            log_operation->warn("bridge loops: a bridge face repeats an existing face; skipped");
            return false;
        }
        m_new_facets.push_back(facet);
        return true;
    }

    // Merge: each pair welds at lerp(a, b, merge_factor) onto the a vertex.
    void merge_pair(
        const std::vector<GEO::index_t>& a,
        const std::vector<GEO::index_t>& b,
        const std::vector<std::size_t>&  index_a,
        const std::vector<std::size_t>&  index_b,
        const bool                       closed
    )
    {
        const float factor = m_options.merge_factor;
        std::vector<std::pair<GEO::index_t, GEO::index_t>> merges;
        for (std::size_t r = 0; r < index_a.size(); ++r) {
            const GEO::index_t survivor = a[index_a[r]];
            const GEO::index_t merged   = b[index_b[r]];
            if (survivor == merged) {
                continue;
            }
            const GEO::vec3f position =
                ((1.0f - factor) * m_edit_mesh.get_position(survivor)) +
                (factor * m_edit_mesh.get_position(merged));
            std::map<GEO::index_t, float> weights;
            for (const Edit_source& source_entry : m_edit_mesh.get_vertex(survivor).sources) {
                weights[source_entry.second] += (1.0f - factor) * source_entry.first;
            }
            for (const Edit_source& source_entry : m_edit_mesh.get_vertex(merged).sources) {
                weights[source_entry.second] += factor * source_entry.first;
            }
            std::vector<Edit_source> sources;
            for (const std::pair<const GEO::index_t, float>& weight : weights) {
                if (weight.second > 0.0f) {
                    sources.emplace_back(weight.second, weight.first);
                }
            }
            m_edit_mesh.set_position(survivor, position);
            m_edit_mesh.set_vertex_sources(survivor, sources);
            merges.emplace_back(merged, survivor);
            m_merged_vertices.push_back(survivor);
        }
        const std::size_t n = a.size();
        const std::size_t edge_count = closed ? n : (n - 1);
        for (std::size_t i = 0; i < edge_count; ++i) {
            m_merged_edges.push_back(make_vertex_pair(a[i], a[(i + 1) % n]));
        }
        m_edit_mesh.weld_vertices(merges);
    }

    Bridge_loops_options                 m_options{};
    std::map<Vertex_pair, GEO::index_t>  m_loop_edge_facet;  // loop edge -> its facet before bridging
    std::vector<GEO::index_t>            m_new_facets;       // scratch facets
    std::vector<Vertex_pair>             m_rungs;            // scratch vertex pairs
    std::vector<GEO::index_t>            m_merged_vertices;  // scratch survivors
    std::vector<Vertex_pair>             m_merged_edges;     // scratch survivor pairs
    bool                                 m_bridged{false};
};

} // anonymous namespace

void bridge_loops(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    const Bridge_loops_options          options,
    Bridge_loops_result*                result,
    Component_remap*                    remap
)
{
    const int cuts = std::clamp(options.cuts, 0, 500);
    if ((cuts == 0) || options.merge) {
        Bridge operation{source, destination};
        operation.build(selection, options);
        operation.make_result(result, remap);
        return;
    }

    // Cuts: bridge into an intermediate geometry, then subdivide its rungs
    // as an edge ring.
    Geometry intermediate{"bridge_loops"};
    std::set<Vertex_pair> rungs;
    {
        Bridge operation{source, intermediate};
        operation.build(selection, options);
        operation.get_rungs(rungs);
    }
    Subdivide_edges_options subdivide_options{};
    subdivide_options.cuts = cuts;
    Subdivide_edges_result subdivide_result{};
    subdivide_edges(intermediate, destination, rungs, subdivide_options, &subdivide_result, nullptr);

    if (result != nullptr) {
        result->bridge_facets.clear();
    }
    const bool has_remap = (remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr);
    if (has_remap) {
        remap->destination->vertices.clear();
        remap->destination->edges.clear();
        remap->destination->facets.clear();
    }
    if (rungs.empty()) {
        return;
    }
    const GEO::Mesh& mesh = destination.get_mesh();
    for (const GEO::index_t facet : subdivide_result.inner_facets) {
        if (result != nullptr) {
            result->bridge_facets.push_back(facet);
        }
        if (!has_remap) {
            continue;
        }
        Geometry_component_selection& dst = *remap->destination;
        dst.facets.insert(facet);
        const GEO::index_t n = mesh.facets.nb_vertices(facet);
        for (GEO::index_t i = 0; i < n; ++i) {
            const GEO::index_t u = mesh.facets.vertex(facet, i);
            const GEO::index_t v = mesh.facets.vertex(facet, (i + 1) % n);
            dst.vertices.insert(u);
            dst.edges.insert(make_vertex_pair(u, v));
        }
    }
}

} // namespace erhe::geometry::operation
