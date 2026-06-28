#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/operation/conway/subdivide.hpp"

#include <geogram/basic/geometry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;

namespace {

// A subdivided box whose connectivity / edge caches exist (process() run), so the
// selection-aware operations and the edge remap have edges to work with.
auto make_box_geometry(const int div) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("box");
    erhe::geometry::shapes::make_box(
        geo->get_mesh(),
        GEO::vec3f{2.0f, 2.0f, 2.0f},
        GEO::vec3i{div, div, div},
        1.0f
    );
    const uint64_t flags =
        Geometry::process_flag_connect |
        Geometry::process_flag_build_edges |
        Geometry::process_flag_compute_facet_centroids |
        Geometry::process_flag_compute_smooth_vertex_normals |
        Geometry::process_flag_generate_facet_texture_coordinates;
    geo->process({.flags = flags});
    return geo;
}

// Canonical (min, max) undirected vertex pairs that are facet-adjacent in the mesh:
// the set of real mesh edges expressed as vertex pairs, matching how an edge
// selection is addressed.
auto facet_edge_set(const GEO::Mesh& mesh) -> std::set<std::pair<GEO::index_t, GEO::index_t>>
{
    std::set<std::pair<GEO::index_t, GEO::index_t>> edges;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
            const GEO::index_t c  = mesh.facets.corner(facet, lc);
            const GEO::index_t cn = mesh.facets.corner(facet, (lc + 1) % corner_count);
            const GEO::index_t v0 = mesh.facet_corners.vertex(c);
            const GEO::index_t v1 = mesh.facet_corners.vertex(cn);
            edges.insert((v0 < v1) ? std::make_pair(v0, v1) : std::make_pair(v1, v0));
        }
    }
    return edges;
}

// The result facets whose every vertex lies inside source facet `src_facet`'s
// axis-aligned bounding box. For an axis-aligned box face this is an independent
// ground truth for "the subdivision descendants of src_facet": the sub-facets all
// lie on the original facet (one constant axis, the other two within the facet's
// extent), while every other result facet has at least one vertex off it.
auto facets_inside_source_facet_bbox(
    const Geometry&    source,
    const GEO::index_t src_facet,
    const Geometry&    result
) -> std::set<GEO::index_t>
{
    const GEO::Mesh& src_mesh = source.get_mesh();
    GEO::vec3f lo{ 1e30f,  1e30f,  1e30f};
    GEO::vec3f hi{-1e30f, -1e30f, -1e30f};
    for (GEO::index_t corner : src_mesh.facets.corners(src_facet)) {
        const GEO::index_t v = src_mesh.facet_corners.vertex(corner);
        const GEO::vec3f   p = erhe::geometry::get_pointf(src_mesh.vertices, v);
        lo.x = std::min(lo.x, p.x); hi.x = std::max(hi.x, p.x);
        lo.y = std::min(lo.y, p.y); hi.y = std::max(hi.y, p.y);
        lo.z = std::min(lo.z, p.z); hi.z = std::max(hi.z, p.z);
    }
    const float tol = 1e-3f;

    const GEO::Mesh& res_mesh = result.get_mesh();
    std::set<GEO::index_t> inside;
    for (GEO::index_t facet : res_mesh.facets) {
        bool all_inside = true;
        for (GEO::index_t corner : res_mesh.facets.corners(facet)) {
            const GEO::index_t v = res_mesh.facet_corners.vertex(corner);
            const GEO::vec3f   p = erhe::geometry::get_pointf(res_mesh.vertices, v);
            if ((p.x < lo.x - tol) || (p.x > hi.x + tol) ||
                (p.y < lo.y - tol) || (p.y > hi.y + tol) ||
                (p.z < lo.z - tol) || (p.z > hi.z + tol)) {
                all_inside = false;
                break;
            }
        }
        if (all_inside) {
            inside.insert(facet);
        }
    }
    return inside;
}

} // anonymous namespace

// A selected face maps to exactly the sub-facets the operation made from it.
// Catmull-Clark splits the selected quad facet into four quads, all lying on the
// original facet; the remap must report exactly those four, and nothing from the
// untouched remainder.
TEST(ComponentSelectionRemap, Face_CatmullClark_Maps_Selected_Facet_To_Its_Subfaces)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);

    std::set<GEO::index_t>       selected_facets{0};
    Geometry_component_selection remap_source;
    remap_source.facets = {0};
    Geometry_component_selection remap_destination;
    Component_remap              remap{&remap_source, &remap_destination};

    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("cc_face");
    erhe::geometry::operation::catmull_clark_subdivision(*box, *result, &selected_facets, &remap);

    const std::set<GEO::index_t> expected = facets_inside_source_facet_bbox(*box, 0, *result);
    EXPECT_EQ(expected.size(), 4u) << "Catmull-Clark of a quad facet should yield four sub-quads";
    EXPECT_EQ(remap_destination.facets, expected);
    EXPECT_TRUE(remap_destination.vertices.empty());
    EXPECT_TRUE(remap_destination.edges.empty());
}

// Same face invariant through a second operation (Conway ortho subdivide), to
// exercise the generic facet-provenance inversion on a different subdivision pattern.
TEST(ComponentSelectionRemap, Face_Subdivide_Maps_Selected_Facet_To_Its_Subfaces)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);

    std::set<GEO::index_t>       selected_facets{0};
    Geometry_component_selection remap_source;
    remap_source.facets = {0};
    Geometry_component_selection remap_destination;
    Component_remap              remap{&remap_source, &remap_destination};

    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("subdivide_face");
    erhe::geometry::operation::subdivide(*box, *result, &selected_facets, &remap);

    const std::set<GEO::index_t> expected = facets_inside_source_facet_bbox(*box, 0, *result);
    EXPECT_EQ(expected.size(), 4u) << "ortho subdivide of a quad facet should yield four sub-quads";
    EXPECT_EQ(remap_destination.facets, expected);
}

// Selected vertices map to their destination images. Whole-mesh Catmull-Clark carries
// every source vertex over (smoothed in place) as result vertex of the same index, so
// the images are the same indices - and nothing leaks into the facet / edge sets.
TEST(ComponentSelectionRemap, Vertex_CatmullClark_Maps_Selected_Vertices_To_Images)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::index_t source_vertex_count = box->get_mesh().vertices.nb();
    ASSERT_GT(source_vertex_count, 8u);

    const std::set<GEO::index_t> selected_vertices{0, 3, 7};
    Geometry_component_selection  remap_source;
    remap_source.vertices = selected_vertices;
    Geometry_component_selection  remap_destination;
    Component_remap               remap{&remap_source, &remap_destination};

    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("cc_vertex");
    erhe::geometry::operation::catmull_clark_subdivision(*box, *result, nullptr, &remap);

    EXPECT_EQ(remap_destination.vertices, selected_vertices);
    EXPECT_TRUE(remap_destination.facets.empty());
    EXPECT_TRUE(remap_destination.edges.empty());
}

// A selected edge maps to the chain of result sub-edges along it. Whole-mesh
// Catmull-Clark splits every edge once, so the selected edge (a, b) becomes two real
// result edges (a, m) and (m, b): they share exactly the new midpoint m, their outer
// endpoints are exactly a and b, and both are genuine facet edges of the result.
TEST(ComponentSelectionRemap, Edge_CatmullClark_Splits_Selected_Edge_Into_Two_Subedges)
{
    std::unique_ptr<Geometry> box = make_box_geometry(2);
    const GEO::Mesh&   src_mesh = box->get_mesh();
    const GEO::index_t source_vertex_count = src_mesh.vertices.nb();
    ASSERT_GT(src_mesh.edges.nb(), 0u);

    GEO::index_t a = src_mesh.edges.vertex(0, 0);
    GEO::index_t b = src_mesh.edges.vertex(0, 1);
    if (a > b) {
        std::swap(a, b);
    }

    Geometry_component_selection remap_source;
    remap_source.edges = { std::make_pair(a, b) };
    Geometry_component_selection remap_destination;
    Component_remap              remap{&remap_source, &remap_destination};

    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("cc_edge");
    erhe::geometry::operation::catmull_clark_subdivision(*box, *result, nullptr, &remap);

    ASSERT_EQ(remap_destination.edges.size(), 2u) << "a once-split edge should yield two sub-edges";
    EXPECT_TRUE(remap_destination.facets.empty());
    EXPECT_TRUE(remap_destination.vertices.empty());

    // Both sub-edges must be real edges of the result mesh.
    const std::set<std::pair<GEO::index_t, GEO::index_t>> real_edges = facet_edge_set(result->get_mesh());
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : remap_destination.edges) {
        EXPECT_TRUE(real_edges.count(edge) != 0)
            << "sub-edge (" << edge.first << ", " << edge.second << ") is not a real result edge";
    }

    // The vertex shared by the two sub-edges is the inserted midpoint; the two
    // unshared endpoints are the original edge's endpoints (carried over by index).
    std::map<GEO::index_t, int> vertex_use;
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : remap_destination.edges) {
        ++vertex_use[edge.first];
        ++vertex_use[edge.second];
    }
    std::set<GEO::index_t> shared;
    std::set<GEO::index_t> outer;
    for (const std::pair<const GEO::index_t, int>& use : vertex_use) {
        if (use.second == 2) {
            shared.insert(use.first);
        } else {
            outer.insert(use.first);
        }
    }
    ASSERT_EQ(shared.size(), 1u) << "the two sub-edges must share exactly the midpoint";
    EXPECT_GE(*shared.begin(), source_vertex_count) << "the shared midpoint must be a newly created vertex";
    EXPECT_EQ(outer, (std::set<GEO::index_t>{a, b})) << "the outer endpoints must be the original edge endpoints";
}
