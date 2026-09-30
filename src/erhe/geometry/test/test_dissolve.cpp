#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/dissolve.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>

using erhe::geometry::Delete_context;
using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Dissolve_edges_options;
using erhe::geometry::operation::Dissolve_faces_options;
using erhe::geometry::operation::Dissolve_limited_options;
using erhe::geometry::operation::Dissolve_vertices_options;
using erhe::geometry::operation::Geometry_component_selection;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

auto make_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_box(geo->get_mesh(), 2.0f, 2.0f, 2.0f);
    geo->process({.flags = edge_flags});
    return geo;
}

// Open grid of 4 x 4 quads in the XY plane, counter-clockwise seen from +Z.
// Vertex (x, y) is y * 5 + x, facet (x, y) is y * 4 + x.
constexpr int grid_size = 4;

auto grid_vertex(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * (grid_size + 1)) + x);
}

auto grid_facet(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * grid_size) + x);
}

auto grid_edge(const int x0, const int y0, const int x1, const int y1) -> std::pair<GEO::index_t, GEO::index_t>
{
    const GEO::index_t a = grid_vertex(x0, y0);
    const GEO::index_t b = grid_vertex(x1, y1);
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

void make_grid_vertices(GEO::Mesh& mesh, const int size)
{
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(static_cast<GEO::index_t>((size + 1) * (size + 1)));
    for (int y = 0; y <= size; ++y) {
        for (int x = 0; x <= size; ++x) {
            const GEO::index_t vertex = static_cast<GEO::index_t>((y * (size + 1)) + x);
            erhe::geometry::set_pointf(mesh.vertices, vertex, GEO::vec3f{static_cast<float>(x), static_cast<float>(y), 0.0f});
        }
    }
}

auto make_grid() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("grid");
    GEO::Mesh& mesh = geo->get_mesh();
    make_grid_vertices(mesh, grid_size);
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            mesh.facets.create_quad(grid_vertex(x, y), grid_vertex(x + 1, y), grid_vertex(x + 1, y + 1), grid_vertex(x, y + 1));
        }
    }
    geo->process({.flags = edge_flags});
    return geo;
}

// 2 x 2 quads, each split along its (x, y) - (x + 1, y + 1) diagonal.
// Vertex (x, y) is y * 3 + x.
auto tri_vertex(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * 3) + x);
}

auto make_triangulated_grid() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("triangulated grid");
    GEO::Mesh& mesh = geo->get_mesh();
    make_grid_vertices(mesh, 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            mesh.facets.create_triangle(tri_vertex(x, y), tri_vertex(x + 1, y), tri_vertex(x + 1, y + 1));
            mesh.facets.create_triangle(tri_vertex(x, y), tri_vertex(x + 1, y + 1), tri_vertex(x, y + 1));
        }
    }
    geo->process({.flags = edge_flags});
    return geo;
}

class Counts
{
public:
    GEO::index_t vertices;
    GEO::index_t edges;
    GEO::index_t facets;
};

void expect_result(const Geometry& destination, const Counts& counts)
{
    const GEO::Mesh& mesh = destination.get_mesh();
    EXPECT_EQ(mesh.vertices.nb(), counts.vertices);
    EXPECT_EQ(mesh.edges.nb(),    counts.edges);
    EXPECT_EQ(mesh.facets.nb(),   counts.facets);
    EXPECT_EQ(destination.validate(), std::string{});
}

auto count_facets_with_corners(const Geometry& geometry, const GEO::index_t corner_count) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    GEO::index_t count = 0;
    for (const GEO::index_t facet : mesh.facets) {
        if (mesh.facets.nb_corners(facet) == corner_count) {
            ++count;
        }
    }
    return count;
}

auto source_edge(const Geometry& geometry, const GEO::index_t edge) -> std::pair<GEO::index_t, GEO::index_t>
{
    const GEO::Mesh&   mesh = geometry.get_mesh();
    const GEO::index_t a    = mesh.edges.vertex(edge, 0);
    const GEO::index_t b    = mesh.edges.vertex(edge, 1);
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

} // anonymous namespace

TEST(Dissolve, DeleteCube)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry_component_selection selection;
    selection.vertices.insert(0);
    selection.edges.insert(source_edge(*cube, 0));
    selection.facets.insert(0);

    class Case
    {
    public:
        Delete_context context;
        Counts         counts;
    };
    // One vertex takes three facets; one edge takes its two facets (every
    // endpoint keeps an edge); one facet leaves every edge used.
    const std::array<Case, 5> cases{
        Case{Delete_context::vertices,             Counts{7,  9, 3}},
        Case{Delete_context::edges,                Counts{8, 11, 4}},
        Case{Delete_context::faces,                Counts{8, 12, 5}},
        Case{Delete_context::only_edges_and_faces, Counts{8, 11, 4}},
        Case{Delete_context::only_faces,           Counts{8, 12, 5}}
    };
    for (const Case& test_case : cases) {
        SCOPED_TRACE(static_cast<int>(test_case.context));
        Geometry destination{"result"};
        erhe::geometry::operation::delete_components(*cube, destination, selection, test_case.context);
        expect_result(destination, test_case.counts);
    }
}

TEST(Dissolve, DeleteGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection;
    selection.vertices.insert(grid_vertex(2, 2));
    selection.edges.insert(grid_edge(0, 0, 1, 0));
    selection.edges.insert(grid_edge(0, 0, 0, 1));
    selection.facets.insert(grid_facet(0, 0));

    class Case
    {
    public:
        Delete_context context;
        Counts         counts;
    };
    // The corner vertex (0, 0) goes with its facet or its two edges, except
    // in the contexts that keep vertices (it stays, loose).
    const std::array<Case, 5> cases{
        Case{Delete_context::vertices,             Counts{24, 36, 12}},
        Case{Delete_context::edges,                Counts{24, 38, 15}},
        Case{Delete_context::faces,                Counts{24, 38, 15}},
        Case{Delete_context::only_edges_and_faces, Counts{25, 38, 15}},
        Case{Delete_context::only_faces,           Counts{25, 38, 15}}
    };
    for (const Case& test_case : cases) {
        SCOPED_TRACE(static_cast<int>(test_case.context));
        Geometry destination{"result"};
        Geometry_component_selection remapped;
        Component_remap remap{.source = &selection, .destination = &remapped};
        erhe::geometry::operation::delete_components(*grid, destination, selection, test_case.context, &remap);
        expect_result(destination, test_case.counts);
        if (test_case.context == Delete_context::faces) {
            // The deleted facet has no image; the deleted edges neither; the
            // vertex (2, 2) survives as vertex 11 (vertex 0 went).
            EXPECT_TRUE(remapped.facets.empty());
            EXPECT_TRUE(remapped.edges.empty());
            ASSERT_EQ(remapped.vertices.size(), 1u);
            EXPECT_EQ(*remapped.vertices.begin(), grid_vertex(2, 2) - 1);
        }
    }
}

TEST(Dissolve, DissolveFacesCube)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::span<const GEO::index_t> pair = cube->get_edge_facets(0);
    ASSERT_EQ(pair.size(), 2u);
    const std::set<GEO::index_t> facets{pair[0], pair[1]};
    {
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_faces(*cube, destination, facets);
        expect_result(destination, Counts{8, 11, 5});
        EXPECT_EQ(count_facets_with_corners(destination, 6), 1u);
    }
    {
        // The two shared-edge vertices drop to two edges and collapse; the
        // hexagon becomes a quad and their third facets become triangles.
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_faces(*cube, destination, facets, Dissolve_faces_options{.dissolve_vertices = true});
        expect_result(destination, Counts{6, 9, 5});
        EXPECT_EQ(count_facets_with_corners(destination, 3), 2u);
        EXPECT_EQ(count_facets_with_corners(destination, 4), 3u);
    }
}

TEST(Dissolve, DissolveFacesGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    // The four facets around (1, 1): (1, 1) goes with the interior edges;
    // (1, 0) and (0, 1) drop from three edges to two.
    const std::set<GEO::index_t> facets{grid_facet(0, 0), grid_facet(1, 0), grid_facet(0, 1), grid_facet(1, 1)};
    {
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_faces(*grid, destination, facets);
        expect_result(destination, Counts{24, 36, 13});
        EXPECT_EQ(count_facets_with_corners(destination, 8), 1u);
    }
    {
        Geometry destination{"result"};
        Geometry_component_selection source_selection;
        source_selection.facets = facets;
        Geometry_component_selection remapped;
        Component_remap remap{.source = &source_selection, .destination = &remapped};
        erhe::geometry::operation::dissolve_faces(*grid, destination, facets, Dissolve_faces_options{.dissolve_vertices = true}, &remap);
        expect_result(destination, Counts{22, 34, 13});
        EXPECT_EQ(count_facets_with_corners(destination, 6), 1u);
        ASSERT_EQ(remapped.facets.size(), 1u);
        EXPECT_EQ(destination.get_mesh().facets.nb_corners(*remapped.facets.begin()), 6u);
    }
}

TEST(Dissolve, DissolveEdgesGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    {
        // One interior edge: its end vertices keep three edges.
        const std::set<std::pair<GEO::index_t, GEO::index_t>> edges{grid_edge(2, 1, 2, 2)};
        Geometry_component_selection source_selection;
        source_selection.edges = edges;
        source_selection.vertices.insert(grid_vertex(2, 1));
        Geometry_component_selection remapped;
        Component_remap remap{.source = &source_selection, .destination = &remapped};
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_edges(*grid, destination, edges, Dissolve_edges_options{}, &remap);
        expect_result(destination, Counts{25, 39, 15});
        EXPECT_EQ(count_facets_with_corners(destination, 6), 1u);
        EXPECT_TRUE(remapped.edges.empty()); // the edge is gone
        ASSERT_EQ(remapped.vertices.size(), 1u);
        EXPECT_EQ(*remapped.vertices.begin(), grid_vertex(2, 1));
    }

    // The three interior edges of the top row: the row joins into one facet,
    // the three top boundary vertices drop to two edges and collapse.
    const std::set<std::pair<GEO::index_t, GEO::index_t>> row{grid_edge(1, 3, 1, 4), grid_edge(2, 3, 2, 4), grid_edge(3, 3, 3, 4)};
    {
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_edges(*grid, destination, row);
        expect_result(destination, Counts{22, 34, 13});
        EXPECT_EQ(count_facets_with_corners(destination, 7), 1u);
    }
    {
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_edges(*grid, destination, row, Dissolve_edges_options{.angle_threshold_degrees = 0.0f});
        expect_result(destination, Counts{25, 37, 13});
        EXPECT_EQ(count_facets_with_corners(destination, 10), 1u);
    }
    {
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_edges(*grid, destination, row, Dissolve_edges_options{.dissolve_vertices = false});
        expect_result(destination, Counts{25, 37, 13});
    }
}

TEST(Dissolve, DissolveEdgesTrianglePair)
{
    const std::unique_ptr<Geometry> grid = make_triangulated_grid();
    ASSERT_EQ(grid->get_mesh().edges.nb(), 16u);
    // The diagonal of the corner quad; its corner end (0, 0) drops to two edges.
    const std::set<std::pair<GEO::index_t, GEO::index_t>> edges{std::make_pair(tri_vertex(0, 0), tri_vertex(1, 1))};
    {
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_edges(*grid, destination, edges);
        expect_result(destination, Counts{9, 15, 7});
        EXPECT_EQ(count_facets_with_corners(destination, 4), 1u);
    }
    {
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_edges(*grid, destination, edges, Dissolve_edges_options{.preserve_quads = false});
        expect_result(destination, Counts{8, 14, 7});
        EXPECT_EQ(count_facets_with_corners(destination, 3), 7u);
    }
}

TEST(Dissolve, DissolveVerticesGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    {
        // Interior vertex: its four quads join into one 8-gon.
        const std::set<GEO::index_t> vertices{grid_vertex(2, 2)};
        Geometry_component_selection source_selection;
        source_selection.vertices = {grid_vertex(2, 2), grid_vertex(0, 0)};
        Geometry_component_selection remapped;
        Component_remap remap{.source = &source_selection, .destination = &remapped};
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_vertices(*grid, destination, vertices, Dissolve_vertices_options{}, &remap);
        expect_result(destination, Counts{24, 36, 13});
        EXPECT_EQ(count_facets_with_corners(destination, 8), 1u);
        ASSERT_EQ(remapped.vertices.size(), 1u);
        EXPECT_EQ(*remapped.vertices.begin(), grid_vertex(0, 0));
    }
    {
        // Face split: each quad's corner at (2, 2) is split off first, so the
        // four corner triangles join into one diamond and each quad keeps a
        // triangle.
        const std::set<GEO::index_t> vertices{grid_vertex(2, 2)};
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_vertices(*grid, destination, vertices, Dissolve_vertices_options{.face_split = true});
        expect_result(destination, Counts{24, 40, 17});
        EXPECT_EQ(count_facets_with_corners(destination, 3), 4u);
        EXPECT_EQ(count_facets_with_corners(destination, 4), 13u);
    }
    {
        // Boundary vertex: its two facets join, then it collapses (two edges).
        const std::set<GEO::index_t> vertices{grid_vertex(2, 0)};
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_vertices(*grid, destination, vertices);
        expect_result(destination, Counts{24, 38, 15});
        EXPECT_EQ(count_facets_with_corners(destination, 5), 1u);
    }
    {
        // Boundary tear: one copy per facet, each collapsed out of its facet.
        const std::set<GEO::index_t> vertices{grid_vertex(2, 0)};
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_vertices(*grid, destination, vertices, Dissolve_vertices_options{.boundary_tear = true});
        expect_result(destination, Counts{24, 39, 16});
        EXPECT_EQ(count_facets_with_corners(destination, 3), 2u);
    }
}

TEST(Dissolve, DissolveLimited)
{
    {
        // Coplanar grid: every interior edge joins, then the twelve collinear
        // boundary vertices collapse, leaving the four corners.
        const std::unique_ptr<Geometry> grid = make_grid();
        Geometry_component_selection source_selection;
        source_selection.vertices = {grid_vertex(0, 0), grid_vertex(2, 0)};
        Geometry_component_selection remapped;
        Component_remap remap{.source = &source_selection, .destination = &remapped};
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_limited(*grid, destination, nullptr, Dissolve_limited_options{}, &remap);
        expect_result(destination, Counts{4, 4, 1});
        ASSERT_EQ(remapped.vertices.size(), 1u);
        EXPECT_EQ(*remapped.vertices.begin(), 0u);
    }
    {
        // Selection restricted to the edges of facets (0, 0) and (1, 0): their
        // four manifold edges join them with (2, 0), (0, 1) and (1, 1) (the
        // join also takes the edge (1, 1) - (1, 2) those two share, and the
        // vertex (1, 1)); then the collinear (1, 0), (2, 0) and (0, 1)
        // collapse. The rest of the grid stays.
        const std::unique_ptr<Geometry> grid = make_grid();
        Geometry_component_selection selection;
        selection.facets = {grid_facet(0, 0), grid_facet(1, 0)};
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_limited(*grid, destination, &selection);
        expect_result(destination, Counts{21, 32, 12});
    }
    {
        // Cube: every dihedral angle is 90 degrees.
        const std::unique_ptr<Geometry> cube = make_cube();
        Geometry destination{"result"};
        erhe::geometry::operation::dissolve_limited(*cube, destination, nullptr);
        expect_result(destination, Counts{8, 12, 6});
    }
}
