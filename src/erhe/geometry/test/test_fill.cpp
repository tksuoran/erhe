#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/dissolve.hpp"
#include "erhe_geometry/operation/fill.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

using erhe::geometry::Delete_context;
using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Fill_result;
using erhe::geometry::operation::Geometry_component_selection;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

constexpr float epsilon = 1e-5f;

// make_box facet order: 0 x+, 1 y+, 2 z+, 3 x-, 4 y-, 5 z-.
constexpr GEO::index_t cube_y_pos = 1;

auto make_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_box(geo->get_mesh(), 2.0f, 2.0f, 2.0f);
    geo->process({.flags = edge_flags});
    return geo;
}

// Open grid of 4 x 4 unit quads in the XY plane, counter-clockwise seen from
// +Z. Vertex (x, y) is y * 5 + x, facet (x, y) is y * 4 + x. Every corner
// carries corner_texcoord_0 = position.xy / 4. 25 vertices, 40 edges, 16
// facets.
constexpr int grid_size = 4;

auto grid_vertex(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * (grid_size + 1)) + x);
}

auto grid_facet(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * grid_size) + x);
}

auto make_grid() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("grid");
    GEO::Mesh& mesh = geo->get_mesh();
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices((grid_size + 1) * (grid_size + 1));
    for (int y = 0; y <= grid_size; ++y) {
        for (int x = 0; x <= grid_size; ++x) {
            erhe::geometry::set_pointf(mesh.vertices, grid_vertex(x, y), GEO::vec3f{static_cast<float>(x), static_cast<float>(y), 0.0f});
        }
    }
    for (int y = 0; y < grid_size; ++y) {
        for (int x = 0; x < grid_size; ++x) {
            mesh.facets.create_quad(grid_vertex(x, y), grid_vertex(x + 1, y), grid_vertex(x + 1, y + 1), grid_vertex(x, y + 1));
        }
    }
    erhe::geometry::Mesh_attributes& attributes = geo->get_attributes();
    for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
        attributes.corner_texcoord_0.set(corner, GEO::vec2f{p.x / 4.0f, p.y / 4.0f});
    }
    geo->process({.flags = edge_flags});
    return geo;
}

// The source with the given facets deleted (Delete_context::faces).
auto delete_facets(const Geometry& source, const std::set<GEO::index_t>& facets) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> result = std::make_unique<Geometry>("holed");
    Geometry_component_selection selection{};
    selection.facets = facets;
    erhe::geometry::operation::delete_components(source, *result, selection, Delete_context::faces);
    return result;
}

class Counts
{
public:
    GEO::index_t vertices;
    GEO::index_t edges;
    GEO::index_t facets;
};

void expect_counts(const Geometry& destination, const Counts& counts)
{
    const GEO::Mesh& mesh = destination.get_mesh();
    EXPECT_EQ(mesh.vertices.nb(), counts.vertices);
    EXPECT_EQ(mesh.edges.nb(),    counts.edges);
    EXPECT_EQ(mesh.facets.nb(),   counts.facets);
    EXPECT_EQ(destination.validate(), std::string{});
}

auto make_edge(const GEO::index_t a, const GEO::index_t b) -> std::pair<GEO::index_t, GEO::index_t>
{
    return (a < b) ? std::pair<GEO::index_t, GEO::index_t>{a, b} : std::pair<GEO::index_t, GEO::index_t>{b, a};
}

auto facet_normal(const Geometry& geometry, const GEO::index_t facet) -> GEO::vec3f
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const GEO::index_t n = mesh.facets.nb_vertices(facet);
    GEO::vec3f normal{0.0f, 0.0f, 0.0f};
    for (GEO::index_t i = 0; i < n; ++i) {
        const GEO::vec3f a = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, i));
        const GEO::vec3f b = erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, (i + 1) % n));
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    return normal;
}

// True when every edge with two facets is traversed in opposite directions.
auto has_consistent_winding(const Geometry& geometry) -> bool
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    std::set<std::pair<GEO::index_t, GEO::index_t>> directed;
    for (const GEO::index_t facet : mesh.facets) {
        const GEO::index_t n = mesh.facets.nb_vertices(facet);
        for (GEO::index_t i = 0; i < n; ++i) {
            const std::pair<GEO::index_t, GEO::index_t> edge{mesh.facets.vertex(facet, i), mesh.facets.vertex(facet, (i + 1) % n)};
            if (!directed.insert(edge).second) {
                return false;
            }
        }
    }
    return true;
}

// The hole's four vertices of grid facet (1, 1).
const GEO::index_t hole_00 = grid_vertex(1, 1);
const GEO::index_t hole_10 = grid_vertex(2, 1);
const GEO::index_t hole_11 = grid_vertex(2, 2);
const GEO::index_t hole_01 = grid_vertex(1, 2);

} // anonymous namespace

TEST(Fill, GridHoleEdgeLoopBecomesQuad)
{
    const std::unique_ptr<Geometry> grid  = make_grid();
    const std::unique_ptr<Geometry> holed = delete_facets(*grid, {grid_facet(1, 1)});
    expect_counts(*holed, Counts{25, 40, 15});

    Geometry_component_selection selection{};
    selection.edges = {make_edge(hole_00, hole_10), make_edge(hole_10, hole_11), make_edge(hole_11, hole_01), make_edge(hole_01, hole_00)};
    Geometry destination{"filled"};
    Fill_result result{};
    Geometry_component_selection after{};
    Component_remap remap{&selection, &after};
    erhe::geometry::operation::fill_selection(*holed, destination, selection, &result, &remap);

    expect_counts(destination, Counts{25, 40, 16});
    ASSERT_TRUE(result.filled);
    ASSERT_EQ(result.new_facets.size(), 1u);
    const GEO::index_t facet = result.new_facets.front();
    EXPECT_GT(facet_normal(destination, facet).z, 0.0f); // winding of the neighbours
    EXPECT_TRUE(has_consistent_winding(destination));

    // Corner texcoords copied from the neighbours' corners.
    const GEO::Mesh& mesh = destination.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = destination.get_attributes();
    for (GEO::index_t corner = mesh.facets.corners_begin(facet); corner < mesh.facets.corners_end(facet); ++corner) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
        const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
        ASSERT_TRUE(texcoord.has_value());
        EXPECT_NEAR(texcoord.value().x, p.x / 4.0f, epsilon);
        EXPECT_NEAR(texcoord.value().y, p.y / 4.0f, epsilon);
    }

    // The remap selects the new facet with its vertices and edges.
    EXPECT_EQ(after.facets, (std::set<GEO::index_t>{facet}));
    EXPECT_EQ(after.vertices.size(), 4u);
    EXPECT_EQ(after.edges.size(), 4u);
}

TEST(Fill, CubeHoleBoundaryVerticesBecomeFace)
{
    // Vertex mode: the edges between the four selected vertices are the
    // hole's boundary, which closes into the missing face.
    const std::unique_ptr<Geometry> cube  = make_cube();
    const std::unique_ptr<Geometry> holed = delete_facets(*cube, {cube_y_pos});
    expect_counts(*holed, Counts{8, 12, 5});

    Geometry_component_selection selection{};
    const GEO::Mesh& holed_mesh = holed->get_mesh();
    for (GEO::index_t vertex = 0; vertex < holed_mesh.vertices.nb(); ++vertex) {
        if (erhe::geometry::get_pointf(holed_mesh.vertices, vertex).y > 0.5f) {
            selection.vertices.insert(vertex);
        }
    }
    ASSERT_EQ(selection.vertices.size(), 4u);
    Geometry destination{"filled"};
    Fill_result result{};
    erhe::geometry::operation::fill_selection(*holed, destination, selection, &result);

    expect_counts(destination, Counts{8, 12, 6});
    ASSERT_EQ(result.new_facets.size(), 1u);
    EXPECT_GT(facet_normal(destination, result.new_facets.front()).y, 0.0f);
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Fill, LooseVerticesSortedRadially)
{
    // Four loose vertices of a square in scrambled order plus one far
    // triangle: the radial sort makes one convex quad.
    Geometry geometry{"loose"};
    GEO::Mesh& mesh = geometry.get_mesh();
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(7);
    const GEO::vec3f positions[7] = {
        { 1.0f, 0.0f,  1.0f},
        {-1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f,  1.0f},
        { 1.0f, 0.0f, -1.0f},
        {10.0f, 0.0f,  0.0f},
        {11.0f, 0.0f,  0.0f},
        {10.0f, 1.0f,  0.0f}
    };
    for (GEO::index_t vertex = 0; vertex < 7; ++vertex) {
        erhe::geometry::set_pointf(mesh.vertices, vertex, positions[vertex]);
    }
    mesh.facets.create_triangle(4, 5, 6);
    geometry.process({.flags = edge_flags});

    Geometry_component_selection selection{};
    selection.vertices = {0, 1, 2, 3};
    Geometry destination{"filled"};
    Fill_result result{};
    erhe::geometry::operation::fill_selection(geometry, destination, selection, &result);

    expect_counts(destination, Counts{7, 7, 2});
    ASSERT_EQ(result.new_facets.size(), 1u);
    // Convex: the area vector has the full area of the square (2 * area 4).
    EXPECT_NEAR(GEO::length(facet_normal(destination, result.new_facets.front())), 8.0f, epsilon);
}

TEST(Fill, OpenChainClosesAcrossHole)
{
    const std::unique_ptr<Geometry> grid  = make_grid();
    const std::unique_ptr<Geometry> holed = delete_facets(*grid, {grid_facet(1, 1)});

    Geometry_component_selection selection{};
    selection.edges = {make_edge(hole_00, hole_10), make_edge(hole_10, hole_11), make_edge(hole_11, hole_01)};
    Geometry destination{"filled"};
    Fill_result result{};
    erhe::geometry::operation::fill_selection(*holed, destination, selection, &result);

    expect_counts(destination, Counts{25, 40, 16});
    ASSERT_EQ(result.new_facets.size(), 1u);
    EXPECT_GT(facet_normal(destination, result.new_facets.front()).z, 0.0f);
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Fill, FreeVertexAndChain)
{
    const std::unique_ptr<Geometry> grid  = make_grid();
    const std::unique_ptr<Geometry> holed = delete_facets(*grid, {grid_facet(1, 1)});

    Geometry_component_selection selection{};
    selection.edges    = {make_edge(hole_00, hole_10), make_edge(hole_10, hole_11)};
    selection.vertices = {hole_01};
    Geometry destination{"filled"};
    Fill_result result{};
    erhe::geometry::operation::fill_selection(*holed, destination, selection, &result);

    expect_counts(destination, Counts{25, 40, 16});
    ASSERT_EQ(result.new_facets.size(), 1u);
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Fill, TwoVerticesCloseBoundaryChain)
{
    // Two diagonal corners of the hole: the new edge closes the shorter
    // boundary chain (two edges) into a triangle.
    const std::unique_ptr<Geometry> grid  = make_grid();
    const std::unique_ptr<Geometry> holed = delete_facets(*grid, {grid_facet(1, 1)});

    Geometry_component_selection selection{};
    selection.vertices = {hole_00, hole_11};
    Geometry destination{"filled"};
    Fill_result result{};
    erhe::geometry::operation::fill_selection(*holed, destination, selection, &result);

    expect_counts(destination, Counts{25, 41, 16});
    ASSERT_EQ(result.new_facets.size(), 1u);
    EXPECT_EQ(destination.get_mesh().facets.nb_vertices(result.new_facets.front()), 3u);
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Fill, SelectedFacetsDissolve)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection{};
    selection.facets = {grid_facet(0, 0), grid_facet(1, 0)};
    Geometry destination{"filled"};
    Fill_result result{};
    Geometry_component_selection after{};
    Component_remap remap{&selection, &after};
    erhe::geometry::operation::fill_selection(*grid, destination, selection, &result, &remap);

    expect_counts(destination, Counts{25, 39, 15});
    ASSERT_EQ(result.new_facets.size(), 1u);
    EXPECT_EQ(destination.get_mesh().facets.nb_vertices(result.new_facets.front()), 6u);
    EXPECT_EQ(after.facets, (std::set<GEO::index_t>{result.new_facets.front()}));
}

TEST(Fill, EdgeNetOfTwoHolesSharingAVertex)
{
    // Grid facets (1, 1) and (2, 2) share vertex (2, 2): the eight hole edges
    // form a net where that vertex has four edges; each hole closes again.
    const std::unique_ptr<Geometry> grid  = make_grid();
    const std::unique_ptr<Geometry> holed = delete_facets(*grid, {grid_facet(1, 1), grid_facet(2, 2)});
    expect_counts(*holed, Counts{25, 40, 14});

    Geometry_component_selection selection{};
    for (const auto& [x, y] : {std::pair<int, int>{1, 1}, std::pair<int, int>{2, 2}}) {
        selection.edges.insert(make_edge(grid_vertex(x,     y    ), grid_vertex(x + 1, y    )));
        selection.edges.insert(make_edge(grid_vertex(x + 1, y    ), grid_vertex(x + 1, y + 1)));
        selection.edges.insert(make_edge(grid_vertex(x + 1, y + 1), grid_vertex(x,     y + 1)));
        selection.edges.insert(make_edge(grid_vertex(x,     y + 1), grid_vertex(x,     y    )));
    }
    Geometry destination{"filled"};
    Fill_result result{};
    erhe::geometry::operation::fill_selection(*holed, destination, selection, &result);

    expect_counts(destination, Counts{25, 40, 16});
    ASSERT_EQ(result.new_facets.size(), 2u);
    for (const GEO::index_t facet : result.new_facets) {
        EXPECT_EQ(destination.get_mesh().facets.nb_vertices(facet), 4u);
        EXPECT_GT(facet_normal(destination, facet).z, 0.0f);
    }
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Fill, NothingToFillForTwoUnconnectedVertices)
{
    // Two opposite corners of a closed cube: no boundary chain joins them.
    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::Mesh& mesh = cube->get_mesh();
    Geometry_component_selection selection{};
    for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
        if (((p.x > 0.0f) && (p.y > 0.0f) && (p.z > 0.0f)) || ((p.x < 0.0f) && (p.y < 0.0f) && (p.z < 0.0f))) {
            selection.vertices.insert(vertex);
        }
    }
    ASSERT_EQ(selection.vertices.size(), 2u);
    Geometry destination{"filled"};
    Fill_result result{};
    result.filled = true;
    erhe::geometry::operation::fill_selection(*cube, destination, selection, &result);

    expect_counts(destination, Counts{8, 12, 6});
    EXPECT_FALSE(result.filled);
    EXPECT_TRUE(result.new_facets.empty());
}
