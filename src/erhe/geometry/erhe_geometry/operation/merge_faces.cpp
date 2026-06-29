#include "erhe_geometry/operation/merge_faces.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace erhe::geometry::operation {

class Merge_faces : public Geometry_operation
{
public:
    Merge_faces(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets);
    void build();
};

Merge_faces::Merge_faces(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets)
    : Geometry_operation{source, destination}
{
    m_selected_facets = selected_facets;
}

void Merge_faces::build()
{
    const GEO::index_t facet_count  = source_mesh.facets.nb();
    const GEO::index_t vertex_count = source_mesh.vertices.nb();

    // Union-find over selected facets, connected via an INTERNAL edge: an edge with
    // exactly two incident facets, both selected. Two facets touching only at a vertex
    // (no shared edge) are never unioned, so they stay in different groups.
    std::vector<GEO::index_t> parent(facet_count);
    for (GEO::index_t f = 0; f < facet_count; ++f) {
        parent[f] = f;
    }
    const auto find = [&parent](GEO::index_t x) -> GEO::index_t {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]]; // path halving
            x = parent[x];
        }
        return x;
    };
    for (const GEO::index_t src_edge : source_mesh.edges) {
        const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(src_edge);
        if (edge_facets.size() != 2) {
            continue;
        }
        if (is_facet_selected(edge_facets[0]) && is_facet_selected(edge_facets[1])) {
            const GEO::index_t ra = find(edge_facets[0]);
            const GEO::index_t rb = find(edge_facets[1]);
            if (ra != rb) {
                parent[ra] = rb;
            }
        }
    }

    // Collect the boundary half-edges of the selection: a directed corner edge a -> b
    // of a selected facet whose underlying edge is NOT internal (its neighbor across
    // the edge is absent / unselected / the edge is non-manifold). Each carries the
    // source corner at a, so the merged polygon can inherit that corner's attributes.
    struct Half_edge
    {
        GEO::index_t start;
        GEO::index_t end;
        GEO::index_t src_corner;
        GEO::index_t facet;
    };
    std::vector<Half_edge> half_edges;
    for (GEO::index_t src_facet = 0; src_facet < facet_count; ++src_facet) {
        if (!is_facet_selected(src_facet)) {
            continue;
        }
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(src_facet);
        if (corner_count < 3) {
            continue;
        }
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t corner      = source_mesh.facets.corner(src_facet, lc);
            const GEO::index_t next_corner = source_mesh.facets.corner(src_facet, (lc + 1) % corner_count);
            const GEO::index_t a           = source_mesh.facet_corners.vertex(corner);
            const GEO::index_t b           = source_mesh.facet_corners.vertex(next_corner);

            bool internal = false;
            const GEO::index_t edge = source.get_edge(a, b);
            if (edge != GEO::NO_EDGE) {
                const std::vector<GEO::index_t>& edge_facets = source.get_edge_facets(edge);
                if (edge_facets.size() == 2) {
                    const GEO::index_t other = (edge_facets[0] == src_facet) ? edge_facets[1] : edge_facets[0];
                    if (is_facet_selected(other)) {
                        internal = true;
                    }
                }
            }
            if (!internal) {
                half_edges.push_back(Half_edge{.start = a, .end = b, .src_corner = corner, .facet = src_facet});
            }
        }
    }

    // Group facets and boundary half-edges by their union-find root.
    std::unordered_map<GEO::index_t, std::vector<GEO::index_t>>  group_facets;     // root -> selected facets
    std::unordered_map<GEO::index_t, std::vector<std::size_t>>   group_half_edges; // root -> indices into half_edges
    for (GEO::index_t src_facet = 0; src_facet < facet_count; ++src_facet) {
        if (is_facet_selected(src_facet)) {
            group_facets[find(src_facet)].push_back(src_facet);
        }
    }
    for (std::size_t i = 0; i < half_edges.size(); ++i) {
        group_half_edges[find(half_edges[i].facet)].push_back(i);
    }

    // For each group decide whether it merges into a single polygon. A group is
    // mergeable iff its boundary half-edges form exactly ONE simple closed loop (no
    // hole, no pinch). The walk follows each half-edge a -> b to the unique boundary
    // half-edge starting at b.
    std::unordered_map<GEO::index_t, std::vector<std::size_t>> merge_loops; // root -> ordered half-edge indices
    for (const auto& [root, he_indices] : group_half_edges) {
        std::unordered_map<GEO::index_t, std::size_t> start_to_he;
        bool simple = true;
        for (const std::size_t hi : he_indices) {
            const auto inserted = start_to_he.emplace(half_edges[hi].start, hi);
            if (!inserted.second) {
                simple = false; // two boundary half-edges leave the same vertex -> pinch
                break;
            }
        }
        if (!simple || (he_indices.size() < 3)) {
            continue;
        }

        std::vector<std::size_t> loop;
        std::set<std::size_t>    visited;
        const std::size_t        start_hi = he_indices[0];
        std::size_t              cur       = start_hi;
        bool                     ok        = true;
        do {
            if (visited.count(cur) != 0) {
                ok = false; // re-entered a half-edge before closing -> multiple loops
                break;
            }
            visited.insert(cur);
            loop.push_back(cur);
            const auto next = start_to_he.find(half_edges[cur].end);
            if (next == start_to_he.end()) {
                ok = false; // open chain (should not happen on a closed group)
                break;
            }
            cur = next->second;
        } while (cur != start_hi);

        if (ok && (loop.size() == he_indices.size())) {
            merge_loops.emplace(root, std::move(loop)); // single loop covering the whole boundary
        }
    }

    // Determine which source vertices the output references, so only those are pinned
    // (a vertex interior to a merged group is dropped, leaving no isolated vertex).
    std::vector<uint8_t> referenced(vertex_count, 0);
    const auto mark_facet_vertices = [&](const GEO::index_t facet) {
        const GEO::index_t corner_count = source_mesh.facets.nb_corners(facet);
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            referenced[source_mesh.facet_corners.vertex(source_mesh.facets.corner(facet, lc))] = 1;
        }
    };
    // Unselected facets are re-emitted 1:1.
    for (GEO::index_t src_facet = 0; src_facet < facet_count; ++src_facet) {
        if (!is_facet_selected(src_facet)) {
            mark_facet_vertices(src_facet);
        }
    }
    for (const auto& [root, facets] : group_facets) {
        const auto loop_i = merge_loops.find(root);
        if (loop_i != merge_loops.end()) {
            for (const std::size_t hi : loop_i->second) {
                referenced[half_edges[hi].start] = 1; // merged polygon boundary vertices
            }
        } else {
            for (const GEO::index_t facet : facets) {
                mark_facet_vertices(facet); // unmergeable group: facets re-emitted 1:1
            }
        }
    }

    // Pin the referenced vertices (1:1, weight 1.0). post_processing() interpolates
    // their positions back to the originals, so the surface is unchanged outside the
    // merged interiors.
    for (GEO::index_t src_vertex = 0; src_vertex < vertex_count; ++src_vertex) {
        if (referenced[src_vertex] != 0) {
            make_new_dst_vertex_from_src_vertex(src_vertex);
        }
    }

    // Emit one merged polygon per mergeable group, descending from all of the group's
    // source facets (so a component selection remaps onto the merged polygon).
    for (const auto& [root, loop] : merge_loops) {
        const GEO::index_t new_dst_facet = destination_mesh.facets.create_polygon(static_cast<GEO::index_t>(loop.size()));
        for (GEO::index_t i = 0; i < static_cast<GEO::index_t>(loop.size()); ++i) {
            make_new_dst_corner_from_src_corner(new_dst_facet, i, half_edges[loop[i]].src_corner);
        }
        for (const GEO::index_t facet : group_facets[root]) {
            add_facet_source(new_dst_facet, 1.0f, facet);
        }
    }

    // Re-emit the facets of any unmergeable group unchanged (1:1).
    for (const auto& [root, facets] : group_facets) {
        if (merge_loops.find(root) != merge_loops.end()) {
            continue;
        }
        for (const GEO::index_t facet : facets) {
            const GEO::index_t corner_count  = source_mesh.facets.nb_corners(facet);
            const GEO::index_t new_dst_facet = make_new_dst_facet_from_src_facet(facet, corner_count);
            add_facet_corners(new_dst_facet, facet);
        }
    }

    // Re-emit the unselected facets. Merge inserts no edge midpoints, so this copies
    // each one 1:1 against the pinned vertices -> the seam welds with no cracks.
    emit_unselected_facets_with_boundary_splice();

    post_processing();
}

void merge_faces(const Geometry& source, Geometry& destination, const std::set<GEO::index_t>* selected_facets, Component_remap* remap)
{
    Merge_faces operation{source, destination, selected_facets};
    operation.build();
    if ((remap != nullptr) && (remap->source != nullptr) && (remap->destination != nullptr)) {
        operation.remap_component_selection(*remap->source, *remap->destination);
    }
}

} // namespace erhe::geometry::operation
