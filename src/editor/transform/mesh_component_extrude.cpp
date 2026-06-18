#include "transform/mesh_component_extrude.hpp"

#include "erhe_geometry/geometry.hpp"

#include <geogram/mesh/mesh.h>

#include <cmath>
#include <unordered_map>
#include <utility>

namespace editor {

using erhe::geometry::Geometry;
using erhe::geometry::get_pointf;
using erhe::geometry::set_pointf;
using erhe::geometry::mesh_facet_normalf;

namespace {

// True when the directed edge (va, vb) is on the boundary of the selected facet set:
// the edge is shared by at most one selected facet (its other side, if any, is an
// unselected facet or the open mesh boundary). Edges shared by two selected facets are
// interior and stay welded.
auto is_boundary_edge(
    const Geometry&               source,
    const GEO::index_t            va,
    const GEO::index_t            vb,
    const std::set<GEO::index_t>& selected_facets
) -> bool
{
    const GEO::index_t edge = source.get_edge(va, vb);
    if (edge == GEO::NO_EDGE) {
        return true;
    }
    const std::vector<GEO::index_t>& facets = source.get_edge_facets(edge);
    int selected_count = 0;
    for (const GEO::index_t facet : facets) {
        if (selected_facets.count(facet) != 0) {
            ++selected_count;
        }
    }
    return selected_count < 2;
}

// Orient (a, b) to match the traversal direction of an adjacent source facet, so the
// bridge quad (a, b, dup[b], dup[a]) winds consistently with the surrounding surface.
// Leaves (a, b) unchanged for a wholly free edge with no adjacent facet.
void orient_edge_with_facet(const Geometry& source, GEO::index_t& a, GEO::index_t& b)
{
    const GEO::index_t edge = source.get_edge(a, b);
    if (edge == GEO::NO_EDGE) {
        return;
    }
    const std::vector<GEO::index_t>& facets = source.get_edge_facets(edge);
    if (facets.empty()) {
        return;
    }
    const GEO::Mesh&   mesh         = source.get_mesh();
    const GEO::index_t facet        = facets.front();
    const GEO::index_t corner_begin = mesh.facets.corners_begin(facet);
    const GEO::index_t corner_end   = mesh.facets.corners_end(facet);
    const GEO::index_t corner_count = corner_end - corner_begin;
    for (GEO::index_t i = 0; i < corner_count; ++i) {
        const GEO::index_t v0 = mesh.facet_corners.vertex(corner_begin + i);
        const GEO::index_t v1 = mesh.facet_corners.vertex(corner_begin + ((i + 1) % corner_count));
        if (((v0 == a) && (v1 == b)) || ((v0 == b) && (v1 == a))) {
            a = v0;
            b = v1;
            return;
        }
    }
}

} // anonymous namespace

auto extrude_mesh_components(
    const Geometry&                source,
    const Mesh_component_mode      mode,
    const std::set<GEO::index_t>&  selected_vertices,
    const std::set<Mesh_edge_key>& selected_edges,
    const std::set<GEO::index_t>&  selected_facets
) -> Extrude_result
{
    Extrude_result result;
    result.geometry = std::make_shared<Geometry>(source.get_name() + " (extrude)");
    result.geometry->copy_with_transform(source, GEO::create_scaling_matrix(1.0f));
    GEO::Mesh& mesh = result.geometry->get_mesh();

    // Original vertex -> its single duplicate (shared across all uses).
    std::unordered_map<GEO::index_t, GEO::index_t> duplicate_of;
    const auto duplicate = [&](const GEO::index_t vertex) -> GEO::index_t {
        const auto i = duplicate_of.find(vertex);
        if (i != duplicate_of.end()) {
            return i->second;
        }
        const GEO::index_t new_vertex = mesh.vertices.create_vertices(1);
        set_pointf(mesh.vertices, new_vertex, get_pointf(mesh.vertices, vertex));
        duplicate_of.emplace(vertex, new_vertex);
        result.moved_vertices.push_back(new_vertex);
        return new_vertex;
    };

    switch (mode) {
        case Mesh_component_mode::vertex: {
            for (const GEO::index_t vertex : selected_vertices) {
                const GEO::index_t vertex_dup = duplicate(vertex);
                const GEO::index_t facet      = mesh.facets.create_polygon(2);
                mesh.facets.set_vertex(facet, 0, vertex);
                mesh.facets.set_vertex(facet, 1, vertex_dup);
                result.selection_vertices.insert(vertex_dup);
            }
            break;
        }
        case Mesh_component_mode::edge: {
            for (const Mesh_edge_key& edge : selected_edges) {
                GEO::index_t a = edge.first;
                GEO::index_t b = edge.second;
                orient_edge_with_facet(source, a, b);
                const GEO::index_t a_dup = duplicate(a);
                const GEO::index_t b_dup = duplicate(b);
                const GEO::index_t facet = mesh.facets.create_polygon(4);
                mesh.facets.set_vertex(facet, 0, a);
                mesh.facets.set_vertex(facet, 1, b);
                mesh.facets.set_vertex(facet, 2, b_dup);
                mesh.facets.set_vertex(facet, 3, a_dup);
                result.selection_edges.insert(make_edge_key(a_dup, b_dup));
            }
            break;
        }
        case Mesh_component_mode::face: {
            // 1. Collect directed boundary edges (a -> b in a selected facet's corner
            //    order) and the boundary vertex set, from the source connectivity.
            const GEO::Mesh&                                  src_mesh = source.get_mesh();
            std::vector<std::pair<GEO::index_t, GEO::index_t>> boundary_edges;
            std::set<GEO::index_t>                             boundary_vertices;
            for (const GEO::index_t facet : selected_facets) {
                const GEO::index_t corner_begin = src_mesh.facets.corners_begin(facet);
                const GEO::index_t corner_end   = src_mesh.facets.corners_end(facet);
                const GEO::index_t corner_count = corner_end - corner_begin;
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    const GEO::index_t va = src_mesh.facet_corners.vertex(corner_begin + i);
                    const GEO::index_t vb = src_mesh.facet_corners.vertex(corner_begin + ((i + 1) % corner_count));
                    if (is_boundary_edge(source, va, vb, selected_facets)) {
                        boundary_edges.emplace_back(va, vb);
                        boundary_vertices.insert(va);
                        boundary_vertices.insert(vb);
                    }
                }
            }

            // 2. Duplicate the boundary vertices.
            for (const GEO::index_t vertex : boundary_vertices) {
                duplicate(vertex);
            }

            // 3. Re-point the selected facets: boundary-vertex corners take the
            //    duplicate; interior-vertex corners are left and move directly.
            std::set<GEO::index_t> interior_vertices;
            for (const GEO::index_t facet : selected_facets) {
                const GEO::index_t corner_begin = mesh.facets.corners_begin(facet);
                const GEO::index_t corner_end   = mesh.facets.corners_end(facet);
                const GEO::index_t corner_count = corner_end - corner_begin;
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    const GEO::index_t corner = corner_begin + i;
                    const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
                    const auto         it     = duplicate_of.find(vertex);
                    if (it != duplicate_of.end()) {
                        mesh.facets.set_vertex(facet, i, it->second);
                    } else {
                        interior_vertices.insert(vertex);
                    }
                }
            }
            for (const GEO::index_t vertex : interior_vertices) {
                result.moved_vertices.push_back(vertex);
            }

            // 4. Bridge quads (a, b, dup[b], dup[a]) along each boundary edge.
            for (const std::pair<GEO::index_t, GEO::index_t>& boundary_edge : boundary_edges) {
                const GEO::index_t a     = boundary_edge.first;
                const GEO::index_t b     = boundary_edge.second;
                const GEO::index_t a_dup = duplicate_of.at(a);
                const GEO::index_t b_dup = duplicate_of.at(b);
                const GEO::index_t facet = mesh.facets.create_polygon(4);
                mesh.facets.set_vertex(facet, 0, a);
                mesh.facets.set_vertex(facet, 1, b);
                mesh.facets.set_vertex(facet, 2, b_dup);
                mesh.facets.set_vertex(facet, 3, a_dup);
            }

            // 5. The selected facets keep their indices (now re-pointed) and stay selected.
            result.selection_facets = selected_facets;
            break;
        }
        case Mesh_component_mode::object:
        default: {
            break;
        }
    }

    if (result.moved_vertices.empty()) {
        result.geometry.reset();
        return result;
    }

    // Rebuild editor-side connectivity (for get_vertex_corners() during the live drag)
    // and edges / centroids for rendering. No normals here: positions are still
    // coincident, so the new faces are degenerate - normals come later via
    // finalize_extrude_normals() once the gizmo has moved.
    result.geometry->process(
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids
    );

    return result;
}

void finalize_extrude_normals(Geometry& geometry)
{
    GEO::Mesh&                       mesh       = geometry.get_mesh();
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();

    // Smooth vertex normals accumulated from non-degenerate facet normals.
    std::vector<GEO::vec3f> smooth(mesh.vertices.nb(), GEO::vec3f{0.0f, 0.0f, 0.0f});
    for (GEO::index_t facet = 0, facet_end = mesh.facets.nb(); facet < facet_end; ++facet) {
        const GEO::vec3f n     = mesh_facet_normalf(mesh, facet);
        const float      len2  = (n.x * n.x) + (n.y * n.y) + (n.z * n.z);
        if (len2 < 1e-20f) {
            attributes.facet_normal.set(facet, GEO::vec3f{0.0f, 0.0f, 0.0f});
            continue; // degenerate (e.g. a vertex-extrude 2-gon): skip in smoothing
        }
        const float      inv = 1.0f / std::sqrt(len2);
        const GEO::vec3f normalized{n.x * inv, n.y * inv, n.z * inv};
        attributes.facet_normal.set(facet, normalized);
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            smooth[vertex] = smooth[vertex] + normalized;
        }
    }
    for (GEO::index_t vertex = 0, vertex_end = mesh.vertices.nb(); vertex < vertex_end; ++vertex) {
        GEO::vec3f  s    = smooth[vertex];
        const float len2 = (s.x * s.x) + (s.y * s.y) + (s.z * s.z);
        if (len2 < 1e-20f) {
            s = GEO::vec3f{0.0f, 1.0f, 0.0f}; // dangling / degenerate: sane default
        } else {
            const float inv = 1.0f / std::sqrt(len2);
            s = GEO::vec3f{s.x * inv, s.y * inv, s.z * inv};
        }
        attributes.vertex_normal_smooth.set(vertex, s);
        if (attributes.vertex_normal.try_get(vertex).has_value()) {
            attributes.vertex_normal.set(vertex, s);
        }
    }
    // Collapse stored corner normals to the smooth vertex normal (hard-edge shading on
    // the new faces is a known limitation, matching Move_mesh_vertices_operation).
    for (GEO::index_t corner = 0, corner_end = mesh.facet_corners.nb(); corner < corner_end; ++corner) {
        if (attributes.corner_normal.try_get(corner).has_value()) {
            const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            attributes.corner_normal.set(corner, attributes.vertex_normal_smooth.get(vertex));
        }
    }
}

} // namespace editor
