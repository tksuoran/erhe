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

auto length2(const GEO::vec3f& v) -> float
{
    return (v.x * v.x) + (v.y * v.y) + (v.z * v.z);
}

// Normalize, or return `fallback` when the input is (near) zero length. Used for both
// per-element normals and the accumulated subset normal.
auto normalized_or(const GEO::vec3f& v, const GEO::vec3f& fallback) -> GEO::vec3f
{
    const float len2 = length2(v);
    if (len2 < 1e-20f) {
        return fallback;
    }
    const float inv = 1.0f / std::sqrt(len2);
    return GEO::vec3f{v.x * inv, v.y * inv, v.z * inv};
}

// Unit facet normal, or zero for a degenerate facet (so it contributes nothing to a
// subset sum). mesh_facet_normalf returns an area-weighted normal; we normalize per
// facet so the subset average is a mean of unit normals (matching the smooth-normal
// convention used elsewhere in the editor).
auto facet_unit_normal_or_zero(const GEO::Mesh& mesh, const GEO::index_t facet) -> GEO::vec3f
{
    return normalized_or(mesh_facet_normalf(mesh, facet), GEO::vec3f{0.0f, 0.0f, 0.0f});
}

// Unit normal of an edge: the mean of the unit normals of the (one or two) facets that
// share it. Zero when the edge has no adjacent facet.
auto edge_unit_normal_or_zero(const Geometry& source, const GEO::index_t v0, const GEO::index_t v1) -> GEO::vec3f
{
    const GEO::index_t edge = source.get_edge(v0, v1);
    if (edge == GEO::NO_EDGE) {
        return GEO::vec3f{0.0f, 0.0f, 0.0f};
    }
    const GEO::Mesh& mesh = source.get_mesh();
    GEO::vec3f       sum{0.0f, 0.0f, 0.0f};
    for (const GEO::index_t facet : source.get_edge_facets(edge)) {
        sum = sum + facet_unit_normal_or_zero(mesh, facet);
    }
    return normalized_or(sum, GEO::vec3f{0.0f, 0.0f, 0.0f});
}

// Unit normal of a vertex: the stored vertex normal if present, else the mean of the
// unit normals of the incident facets.
auto vertex_unit_normal_or_zero(
    const Geometry&                        source,
    const erhe::geometry::Mesh_attributes& attributes,
    const GEO::index_t                     vertex
) -> GEO::vec3f
{
    if (attributes.vertex_normal.has(vertex)) {
        return normalized_or(attributes.vertex_normal.get(vertex), GEO::vec3f{0.0f, 0.0f, 0.0f});
    }
    if (attributes.vertex_normal_smooth.has(vertex)) {
        return normalized_or(attributes.vertex_normal_smooth.get(vertex), GEO::vec3f{0.0f, 0.0f, 0.0f});
    }
    const GEO::Mesh& mesh = source.get_mesh();
    GEO::vec3f       sum{0.0f, 0.0f, 0.0f};
    for (const GEO::index_t corner : source.get_vertex_corners(vertex)) {
        sum = sum + facet_unit_normal_or_zero(mesh, source.get_corner_facet(corner));
    }
    return normalized_or(sum, GEO::vec3f{0.0f, 0.0f, 0.0f});
}

// Partition the selection into disjoint connected subsets, compute each subset's unit
// average normal, and map every ORIGINAL vertex that the extrude will move to the unit
// normal of the subset(s) it belongs to (geometry-local space). The fallback for a
// vertex with no usable normal (or that maps to no subset) is +Y.
auto compute_subset_directions(
    const Geometry&                source,
    const Mesh_component_mode      mode,
    const std::set<GEO::index_t>&  selected_vertices,
    const std::set<Mesh_edge_key>& selected_edges,
    const std::set<GEO::index_t>&  selected_facets
) -> std::unordered_map<GEO::index_t, GEO::vec3f>
{
    std::unordered_map<GEO::index_t, GEO::vec3f> directions;
    const GEO::vec3f                             up{0.0f, 1.0f, 0.0f};
    const GEO::Mesh&                             mesh       = source.get_mesh();
    const erhe::geometry::Mesh_attributes&       attributes = source.get_attributes();

    switch (mode) {
        case Mesh_component_mode::face: {
            // Connected components of selected facets, joined across edges shared by two
            // selected facets. comp_normal[c] accumulates that component's facet normals.
            std::unordered_map<GEO::index_t, int> comp_of_facet;
            std::vector<GEO::vec3f>               comp_normal;
            std::vector<GEO::index_t>             stack;
            for (const GEO::index_t seed : selected_facets) {
                if (comp_of_facet.count(seed) != 0) {
                    continue;
                }
                const int comp = static_cast<int>(comp_normal.size());
                comp_normal.push_back(GEO::vec3f{0.0f, 0.0f, 0.0f});
                comp_of_facet.emplace(seed, comp);
                stack.clear();
                stack.push_back(seed);
                while (!stack.empty()) {
                    const GEO::index_t facet = stack.back();
                    stack.pop_back();
                    comp_normal[comp] = comp_normal[comp] + facet_unit_normal_or_zero(mesh, facet);
                    const GEO::index_t corner_begin = mesh.facets.corners_begin(facet);
                    const GEO::index_t corner_end   = mesh.facets.corners_end(facet);
                    const GEO::index_t corner_count = corner_end - corner_begin;
                    for (GEO::index_t i = 0; i < corner_count; ++i) {
                        const GEO::index_t va   = mesh.facet_corners.vertex(corner_begin + i);
                        const GEO::index_t vb   = mesh.facet_corners.vertex(corner_begin + ((i + 1) % corner_count));
                        const GEO::index_t edge = source.get_edge(va, vb);
                        if (edge == GEO::NO_EDGE) {
                            continue;
                        }
                        for (const GEO::index_t neighbor : source.get_edge_facets(edge)) {
                            if ((selected_facets.count(neighbor) == 0) || (comp_of_facet.count(neighbor) != 0)) {
                                continue;
                            }
                            comp_of_facet.emplace(neighbor, comp);
                            stack.push_back(neighbor);
                        }
                    }
                }
            }
            for (GEO::vec3f& normal : comp_normal) {
                normal = normalized_or(normal, up);
            }
            // Each moved vertex (boundary duplicate or interior) is a corner vertex of a
            // selected facet; collect the distinct subsets touching it and average their
            // normals (a vertex shared by two edge-disjoint subsets blends both).
            std::unordered_map<GEO::index_t, std::set<int>> vertex_comps;
            for (const GEO::index_t facet : selected_facets) {
                const int          comp         = comp_of_facet.at(facet);
                const GEO::index_t corner_begin = mesh.facets.corners_begin(facet);
                const GEO::index_t corner_end   = mesh.facets.corners_end(facet);
                for (GEO::index_t corner = corner_begin; corner < corner_end; ++corner) {
                    vertex_comps[mesh.facet_corners.vertex(corner)].insert(comp);
                }
            }
            for (const auto& [vertex, comps] : vertex_comps) {
                GEO::vec3f sum{0.0f, 0.0f, 0.0f};
                for (const int comp : comps) {
                    sum = sum + comp_normal[comp];
                }
                directions.emplace(vertex, normalized_or(sum, up));
            }
            break;
        }
        case Mesh_component_mode::edge: {
            // Connected components of selected edges, joined at shared vertices.
            const std::vector<Mesh_edge_key>                  edges(selected_edges.begin(), selected_edges.end());
            std::unordered_map<GEO::index_t, std::vector<int>> vertex_to_edges;
            for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
                vertex_to_edges[edges[i].first ].push_back(i);
                vertex_to_edges[edges[i].second].push_back(i);
            }
            std::vector<int>        comp_of_edge(edges.size(), -1);
            std::vector<GEO::vec3f> comp_normal;
            std::vector<int>        stack;
            for (int seed = 0; seed < static_cast<int>(edges.size()); ++seed) {
                if (comp_of_edge[seed] != -1) {
                    continue;
                }
                const int comp = static_cast<int>(comp_normal.size());
                comp_normal.push_back(GEO::vec3f{0.0f, 0.0f, 0.0f});
                comp_of_edge[seed] = comp;
                stack.clear();
                stack.push_back(seed);
                while (!stack.empty()) {
                    const int edge = stack.back();
                    stack.pop_back();
                    comp_normal[comp] = comp_normal[comp] + edge_unit_normal_or_zero(source, edges[edge].first, edges[edge].second);
                    const GEO::index_t endpoints[2] = {edges[edge].first, edges[edge].second};
                    for (const GEO::index_t endpoint : endpoints) {
                        for (const int neighbor : vertex_to_edges[endpoint]) {
                            if (comp_of_edge[neighbor] != -1) {
                                continue;
                            }
                            comp_of_edge[neighbor] = comp;
                            stack.push_back(neighbor);
                        }
                    }
                }
            }
            for (GEO::vec3f& normal : comp_normal) {
                normal = normalized_or(normal, up);
            }
            // Both endpoints of an edge belong to that edge's subset.
            for (int i = 0; i < static_cast<int>(edges.size()); ++i) {
                const GEO::vec3f normal = comp_normal[comp_of_edge[i]];
                directions[edges[i].first ] = normal;
                directions[edges[i].second] = normal;
            }
            break;
        }
        case Mesh_component_mode::vertex: {
            // Connected components of selected vertices, joined by a mesh edge whose other
            // endpoint is also selected.
            std::unordered_map<GEO::index_t, int> comp_of_vertex;
            std::vector<GEO::vec3f>               comp_normal;
            std::vector<GEO::index_t>             stack;
            for (const GEO::index_t seed : selected_vertices) {
                if (comp_of_vertex.count(seed) != 0) {
                    continue;
                }
                const int comp = static_cast<int>(comp_normal.size());
                comp_normal.push_back(GEO::vec3f{0.0f, 0.0f, 0.0f});
                comp_of_vertex.emplace(seed, comp);
                stack.clear();
                stack.push_back(seed);
                while (!stack.empty()) {
                    const GEO::index_t vertex = stack.back();
                    stack.pop_back();
                    comp_normal[comp] = comp_normal[comp] + vertex_unit_normal_or_zero(source, attributes, vertex);
                    for (const GEO::index_t edge : source.get_vertex_edges(vertex)) {
                        for (GEO::index_t which = 0; which < 2; ++which) {
                            const GEO::index_t other = mesh.edges.vertex(edge, which);
                            if ((other == vertex) || (selected_vertices.count(other) == 0) || (comp_of_vertex.count(other) != 0)) {
                                continue;
                            }
                            comp_of_vertex.emplace(other, comp);
                            stack.push_back(other);
                        }
                    }
                }
            }
            for (GEO::vec3f& normal : comp_normal) {
                normal = normalized_or(normal, up);
            }
            for (const auto& [vertex, comp] : comp_of_vertex) {
                directions.emplace(vertex, comp_normal[comp]);
            }
            break;
        }
        case Mesh_component_mode::object:
        default: {
            break;
        }
    }

    return directions;
}

} // anonymous namespace

auto extrude_mesh_components(
    const Geometry&                source,
    const Mesh_component_mode      mode,
    const std::set<GEO::index_t>&  selected_vertices,
    const std::set<Mesh_edge_key>& selected_edges,
    const std::set<GEO::index_t>&  selected_facets,
    const Extrude_normal_mode      normal_mode
) -> Extrude_result
{
    Extrude_result result;
    result.geometry = std::make_shared<Geometry>(source.get_name() + " (extrude)");
    result.geometry->copy_with_transform(source, GEO::create_scaling_matrix(1.0f));
    GEO::Mesh& mesh = result.geometry->get_mesh();

    // The ORIGINAL source vertex each moved vertex derives from, parallel to
    // result.moved_vertices. Used (in a normal mode) to look up the moved vertex's slide
    // normal, which is keyed by original vertex (its subset's average, or its own).
    std::vector<GEO::index_t> moved_origin;

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
        moved_origin.push_back(vertex);
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
                moved_origin.push_back(vertex);
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

    // Extrude-along-normal: resolve each moved vertex's slide direction from its original
    // vertex. Computed on the source connectivity (the result geometry's new faces are
    // degenerate here, so its normals are not yet meaningful). The +Y fallback covers a
    // vertex with no usable normal (or, in group mode, one that maps to no subset).
    const GEO::vec3f up{0.0f, 1.0f, 0.0f};
    switch (normal_mode) {
        case Extrude_normal_mode::group: {
            const std::unordered_map<GEO::index_t, GEO::vec3f> vertex_direction =
                compute_subset_directions(source, mode, selected_vertices, selected_edges, selected_facets);
            result.move_directions.reserve(moved_origin.size());
            for (const GEO::index_t origin : moved_origin) {
                const auto i = vertex_direction.find(origin);
                result.move_directions.push_back((i != vertex_direction.end()) ? i->second : up);
            }
            break;
        }
        case Extrude_normal_mode::vertex: {
            const erhe::geometry::Mesh_attributes& attributes = source.get_attributes();
            result.move_directions.reserve(moved_origin.size());
            for (const GEO::index_t origin : moved_origin) {
                const GEO::vec3f normal = vertex_unit_normal_or_zero(source, attributes, origin);
                result.move_directions.push_back((length2(normal) > 1e-20f) ? normal : up);
            }
            break;
        }
        case Extrude_normal_mode::none:
        default: {
            break;
        }
    }

    // Rebuild editor-side connectivity (for get_vertex_corners() during the live drag)
    // and edges / centroids for rendering. No normals here: positions are still
    // coincident, so the new faces are degenerate - normals come later via
    // finalize_extrude_normals() once the gizmo has moved.
    result.geometry->process({.flags =
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids
    });

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
    // Corner normals: preserve the unmodified region and keep the new walls hard.
    //
    // The result geometry began as a copy of the source, so every ORIGINAL face
    // corner already carries the source's per-corner normal (present). Those
    // faces are either untouched (the surrounding surface) or a pure translation
    // of the selected facets (the extruded cap); a translation does not change a
    // normal, so their flat shading must be left exactly as copied - overwriting
    // them with a smooth average is what rounded the whole mesh.
    //
    // The newly created side-wall / cap faces were built with set_vertex only, so
    // their corners have no stored normal yet. Give each such corner its facet's
    // hard normal, so a wall reads as a crisp flat face with a hard crease against
    // both the cap and the surrounding surface (the expected look of an extrusion).
    // Degenerate new faces (e.g. the vertex-extrude 2-gons) are left without a
    // corner normal so they fall back to the renderer's facet/derived normal.
    for (GEO::index_t facet = 0, facet_end = mesh.facets.nb(); facet < facet_end; ++facet) {
        const std::optional<GEO::vec3f> facet_normal = attributes.facet_normal.try_get(facet);
        if (!facet_normal.has_value() || (length2(facet_normal.value()) < 1e-20f)) {
            continue;
        }
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            if (!attributes.corner_normal.try_get(corner).has_value()) {
                attributes.corner_normal.set(corner, facet_normal.value());
            }
        }
    }
}

} // namespace editor
