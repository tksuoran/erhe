#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/subdivide_edges.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;
using erhe::geometry::operation::Subdivide_edges_options;
using erhe::geometry::operation::Subdivide_edges_result;

namespace {

using Edge_set = std::set<std::pair<GEO::index_t, GEO::index_t>>;

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
// Vertex (x, y) is y * 5 + x. Every vertex carries vertex_texcoord_0 and
// every corner corner_texcoord_0, both position.xy / 4.
constexpr int grid_size = 4;

auto grid_vertex(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * (grid_size + 1)) + x);
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
    for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, vertex);
        attributes.vertex_texcoord_0.set(vertex, GEO::vec2f{p.x / 4.0f, p.y / 4.0f});
    }
    for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
        attributes.corner_texcoord_0.set(corner, GEO::vec2f{p.x / 4.0f, p.y / 4.0f});
    }
    geo->process({.flags = edge_flags});
    return geo;
}

// One open polygon per facet list entry over the given XY positions.
auto make_polygons(const std::vector<GEO::vec3f>& positions, const std::vector<std::vector<GEO::index_t>>& facets) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("polygons");
    GEO::Mesh& mesh = geo->get_mesh();
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(static_cast<GEO::index_t>(positions.size()));
    for (GEO::index_t vertex = 0; vertex < positions.size(); ++vertex) {
        erhe::geometry::set_pointf(mesh.vertices, vertex, positions[vertex]);
    }
    for (const std::vector<GEO::index_t>& facet_vertices : facets) {
        const GEO::index_t facet = mesh.facets.create_polygon(static_cast<GEO::index_t>(facet_vertices.size()));
        for (GEO::index_t local_corner = 0; local_corner < facet_vertices.size(); ++local_corner) {
            mesh.facets.set_vertex(facet, local_corner, facet_vertices[local_corner]);
        }
    }
    geo->process({.flags = edge_flags});
    return geo;
}

auto make_triangle() -> std::unique_ptr<Geometry>
{
    return make_polygons({{0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}}, {{0, 1, 2}});
}

auto make_hexagon() -> std::unique_ptr<Geometry>
{
    return make_polygons(
        {{2.0f, 0.0f, 0.0f}, {1.0f, 1.7f, 0.0f}, {-1.0f, 1.7f, 0.0f}, {-2.0f, 0.0f, 0.0f}, {-1.0f, -1.7f, 0.0f}, {1.0f, -1.7f, 0.0f}},
        {{0, 1, 2, 3, 4, 5}}
    );
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

auto position(const Geometry& geometry, const GEO::index_t vertex) -> GEO::vec3f
{
    return erhe::geometry::get_pointf(geometry.get_mesh().vertices, vertex);
}

auto is_near(const GEO::vec3f& a, const GEO::vec3f& b) -> bool
{
    return GEO::length(a - b) < 1e-5f;
}

// The destination vertex at the position, or GEO::NO_INDEX.
auto find_vertex_at(const Geometry& geometry, const GEO::vec3f& p) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
        if (is_near(position(geometry, vertex), p)) {
            return vertex;
        }
    }
    return GEO::NO_INDEX;
}

auto has_edge_between(const Geometry& geometry, const GEO::vec3f& a, const GEO::vec3f& b) -> bool
{
    const GEO::index_t vertex_a = find_vertex_at(geometry, a);
    const GEO::index_t vertex_b = find_vertex_at(geometry, b);
    if ((vertex_a == GEO::NO_INDEX) || (vertex_b == GEO::NO_INDEX)) {
        return false;
    }
    return geometry.get_edge(vertex_a, vertex_b) != GEO::NO_EDGE;
}

auto grid_edge(const int x0, const int y0, const int x1, const int y1) -> std::pair<GEO::index_t, GEO::index_t>
{
    return std::make_pair(grid_vertex(x0, y0), grid_vertex(x1, y1));
}

// The edges of grid quad (1, 1): bottom, right, top, left.
const std::pair<GEO::index_t, GEO::index_t> quad_bottom = grid_edge(1, 1, 2, 1);
const std::pair<GEO::index_t, GEO::index_t> quad_right  = grid_edge(2, 1, 2, 2);
const std::pair<GEO::index_t, GEO::index_t> quad_top    = grid_edge(1, 2, 2, 2);
const std::pair<GEO::index_t, GEO::index_t> quad_left   = grid_edge(1, 1, 1, 2);

auto subdivide(const Geometry& source, const Edge_set& edges, const Subdivide_edges_options options = {}, Subdivide_edges_result* result = nullptr) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("result");
    erhe::geometry::operation::subdivide_edges(source, *destination, edges, options, result);
    return destination;
}

} // anonymous namespace

TEST(Subdivide_edges, OneEdgeCutsOneNoFill)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const std::unique_ptr<Geometry> result = subdivide(*grid, {quad_bottom});
    // The two quads of the edge each get the new vertex on their boundary.
    expect_result(*result, Counts{26, 41, 16});
    EXPECT_EQ(count_facets_with_corners(*result, 5), 2u);
    EXPECT_NE(find_vertex_at(*result, GEO::vec3f{1.5f, 1.0f, 0.0f}), GEO::NO_INDEX);

    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::Mesh& cube_mesh = cube->get_mesh();
    const std::unique_ptr<Geometry> cube_result = subdivide(*cube, {{cube_mesh.edges.vertex(0, 0), cube_mesh.edges.vertex(0, 1)}});
    expect_result(*cube_result, Counts{9, 13, 6});
}

TEST(Subdivide_edges, OppositeEdgesCutsOne)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Subdivide_edges_result result;
    Geometry_component_selection selection;
    selection.edges = {quad_bottom, quad_top};
    Geometry_component_selection remapped;
    Component_remap remap{&selection, &remapped};
    Geometry destination{"result"};
    erhe::geometry::operation::subdivide_edges(*grid, destination, selection.edges, Subdivide_edges_options{}, &result, &remap);
    // One connecting edge: the quad becomes two quads; its neighbours below
    // and above keep the new vertex on their boundary.
    expect_result(destination, Counts{27, 43, 17});
    EXPECT_EQ(count_facets_with_corners(destination, 5), 2u);
    const GEO::index_t bottom_cut = find_vertex_at(destination, GEO::vec3f{1.5f, 1.0f, 0.0f});
    const GEO::index_t top_cut    = find_vertex_at(destination, GEO::vec3f{1.5f, 2.0f, 0.0f});
    ASSERT_NE(bottom_cut, GEO::NO_INDEX);
    ASSERT_NE(top_cut,    GEO::NO_INDEX);
    EXPECT_NE(destination.get_edge(bottom_cut, top_cut), GEO::NO_EDGE);

    // Inner elements: the two cuts, the connecting edge and the two quads.
    EXPECT_EQ(result.inner_vertices, (std::vector<GEO::index_t>{std::min(bottom_cut, top_cut), std::max(bottom_cut, top_cut)}));
    EXPECT_EQ(result.inner_edges, (std::vector<std::pair<GEO::index_t, GEO::index_t>>{{std::min(bottom_cut, top_cut), std::max(bottom_cut, top_cut)}}));
    ASSERT_EQ(result.inner_facets.size(), 2u);
    for (const GEO::index_t facet : result.inner_facets) {
        EXPECT_EQ(destination.get_mesh().facets.nb_corners(facet), 4u);
    }

    // Remap: each selected edge maps to its two halves.
    const auto sorted = [](const GEO::index_t a, const GEO::index_t b) { return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a); };
    const std::set<std::pair<GEO::index_t, GEO::index_t>> expected_halves{
        sorted(grid_vertex(1, 1), bottom_cut), sorted(bottom_cut, grid_vertex(2, 1)),
        sorted(grid_vertex(1, 2), top_cut),    sorted(top_cut,    grid_vertex(2, 2))
    };
    EXPECT_EQ(remapped.edges, expected_halves);
}

TEST(Subdivide_edges, OppositeEdgesCutsTwo)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const std::unique_ptr<Geometry> result = subdivide(*grid, {quad_bottom, quad_top}, Subdivide_edges_options{.cuts = 2});
    expect_result(*result, Counts{29, 46, 18});
    constexpr float third = 1.0f / 3.0f;
    EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{1.0f + third, 1.0f, 0.0f}, GEO::vec3f{1.0f + third, 2.0f, 0.0f}));
    EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{1.0f + (2.0f * third), 1.0f, 0.0f}, GEO::vec3f{1.0f + (2.0f * third), 2.0f, 0.0f}));
}

TEST(Subdivide_edges, AdjacentEdgesPath)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const std::unique_ptr<Geometry> result = subdivide(*grid, {quad_bottom, quad_right});
    // Corner triangle, the quad between the cut and the corner-to-corner
    // edge, and the triangle at the opposite corner.
    expect_result(*result, Counts{27, 44, 18});
    EXPECT_EQ(count_facets_with_corners(*result, 3), 2u);
    EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{1.5f, 1.0f, 0.0f}, GEO::vec3f{2.0f, 1.5f, 0.0f}));
    EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{1.0f, 1.0f, 0.0f}, GEO::vec3f{2.0f, 2.0f, 0.0f}));
}

TEST(Subdivide_edges, ThreeEdges)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const std::unique_ptr<Geometry> result = subdivide(*grid, {quad_bottom, quad_right, quad_top});
    // The strip along the left side, then the middle cut connects to the
    // bottom cut: two quads and one triangle.
    expect_result(*result, Counts{28, 45, 18});
    EXPECT_EQ(count_facets_with_corners(*result, 3), 1u);
    EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{1.5f, 1.0f, 0.0f}, GEO::vec3f{1.5f, 2.0f, 0.0f}));
    EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{2.0f, 1.5f, 0.0f}, GEO::vec3f{1.5f, 1.0f, 0.0f}));
}

TEST(Subdivide_edges, FourEdgesGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const Edge_set edges{quad_bottom, quad_right, quad_top, quad_left};
    {
        Subdivide_edges_result result_elements;
        const std::unique_ptr<Geometry> result = subdivide(*grid, edges, Subdivide_edges_options{}, &result_elements);
        // A 3 x 3 vertex grid: four quads around one interior vertex.
        expect_result(*result, Counts{30, 48, 19});
        const GEO::index_t centre = find_vertex_at(*result, GEO::vec3f{1.5f, 1.5f, 0.0f});
        ASSERT_NE(centre, GEO::NO_INDEX);
        const erhe::geometry::Mesh_attributes& attributes = result->get_attributes();
        const std::optional<GEO::vec2f> vertex_texcoord = attributes.vertex_texcoord_0.try_get(centre);
        ASSERT_TRUE(vertex_texcoord.has_value());
        EXPECT_NEAR(vertex_texcoord.value().x, 1.5f / 4.0f, 1e-6f);
        EXPECT_NEAR(vertex_texcoord.value().y, 1.5f / 4.0f, 1e-6f);
        const GEO::Mesh& mesh = result->get_mesh();
        GEO::index_t centre_corners = 0;
        for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
            if (mesh.facet_corners.vertex(corner) != centre) {
                continue;
            }
            ++centre_corners;
            const std::optional<GEO::vec2f> corner_texcoord = attributes.corner_texcoord_0.try_get(corner);
            ASSERT_TRUE(corner_texcoord.has_value());
            EXPECT_NEAR(corner_texcoord.value().x, 1.5f / 4.0f, 1e-5f);
            EXPECT_NEAR(corner_texcoord.value().y, 1.5f / 4.0f, 1e-5f);
        }
        EXPECT_EQ(centre_corners, 4u);
        EXPECT_EQ(result_elements.inner_vertices.size(), 5u);
        EXPECT_EQ(result_elements.inner_edges.size(),    4u);
        EXPECT_EQ(result_elements.inner_facets.size(),   4u);
    }
    {
        const std::unique_ptr<Geometry> result = subdivide(*grid, edges, Subdivide_edges_options{.cuts = 2});
        // A 4 x 4 vertex grid: nine quads around four interior vertices.
        expect_result(*result, Counts{37, 60, 24});
        constexpr float third = 1.0f / 3.0f;
        EXPECT_NE(find_vertex_at(*result, GEO::vec3f{1.0f + third, 1.0f + third, 0.0f}), GEO::NO_INDEX);
        EXPECT_NE(find_vertex_at(*result, GEO::vec3f{1.0f + (2.0f * third), 1.0f + (2.0f * third), 0.0f}), GEO::NO_INDEX);
    }

    // One cube face.
    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::Mesh& cube_mesh = cube->get_mesh();
    Edge_set face_edges;
    for (GEO::index_t local_corner = 0; local_corner < 4; ++local_corner) {
        face_edges.insert(std::make_pair(cube_mesh.facets.vertex(0, local_corner), cube_mesh.facets.vertex(0, (local_corner + 1) % 4)));
    }
    const std::unique_ptr<Geometry> cube_result = subdivide(*cube, face_edges);
    expect_result(*cube_result, Counts{13, 20, 9});
}

TEST(Subdivide_edges, TriangleFanAndLattice)
{
    const std::unique_ptr<Geometry> triangle = make_triangle();
    {
        const std::unique_ptr<Geometry> result = subdivide(*triangle, {{0, 1}});
        expect_result(*result, Counts{4, 5, 2});
        EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{1.5f, 0.0f, 0.0f}, GEO::vec3f{0.0f, 3.0f, 0.0f}));
    }
    {
        const std::unique_ptr<Geometry> result = subdivide(*triangle, {{0, 1}, {1, 2}, {2, 0}});
        expect_result(*result, Counts{6, 9, 4});
        EXPECT_EQ(count_facets_with_corners(*result, 3), 4u);
    }
    {
        const std::unique_ptr<Geometry> result = subdivide(*triangle, {{0, 1}, {1, 2}, {2, 0}}, Subdivide_edges_options{.cuts = 2});
        expect_result(*result, Counts{10, 18, 9});
        EXPECT_EQ(count_facets_with_corners(*result, 3), 9u);
        EXPECT_NE(find_vertex_at(*result, GEO::vec3f{1.0f, 1.0f, 0.0f}), GEO::NO_INDEX);
    }
}

TEST(Subdivide_edges, NgonTwoEdges)
{
    const std::unique_ptr<Geometry> hexagon = make_hexagon();
    {
        const std::unique_ptr<Geometry> result = subdivide(*hexagon, {{0, 1}, {3, 4}});
        expect_result(*result, Counts{8, 9, 2});
        EXPECT_TRUE(has_edge_between(*result, GEO::vec3f{1.5f, 0.85f, 0.0f}, GEO::vec3f{-1.5f, -0.85f, 0.0f}));
    }
    {
        const std::unique_ptr<Geometry> result = subdivide(*hexagon, {{0, 1}, {3, 4}}, Subdivide_edges_options{.cuts = 2});
        expect_result(*result, Counts{10, 12, 3});
    }
    {
        // Two adjacent edges of the hexagon: the cuts nest around the shared vertex.
        const std::unique_ptr<Geometry> result = subdivide(*hexagon, {{0, 1}, {1, 2}});
        expect_result(*result, Counts{8, 9, 2});
        EXPECT_EQ(count_facets_with_corners(*result, 3), 1u);
    }
}

TEST(Subdivide_edges, CollinearPairIsNotFilled)
{
    // Pentagon with collinear edges (0, 1) and (1, 2).
    const std::unique_ptr<Geometry> pentagon = make_polygons(
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0, 1, 2, 3, 4}}
    );
    const std::unique_ptr<Geometry> collinear = subdivide(*pentagon, {{0, 1}, {1, 2}});
    expect_result(*collinear, Counts{7, 7, 1});
    const std::unique_ptr<Geometry> bent = subdivide(*pentagon, {{1, 2}, {2, 3}});
    expect_result(*bent, Counts{7, 8, 2});
}

TEST(Subdivide_edges, OnlyQuads)
{
    // A quad split along its diagonal into two triangles.
    const std::unique_ptr<Geometry> triangles = make_polygons(
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{0, 1, 2}, {0, 2, 3}}
    );
    const std::unique_ptr<Geometry> filled = subdivide(*triangles, {{0, 1}});
    expect_result(*filled, Counts{5, 7, 3});
    const std::unique_ptr<Geometry> only_quads = subdivide(*triangles, {{0, 1}}, Subdivide_edges_options{.only_quads = true});
    expect_result(*only_quads, Counts{5, 6, 2});
}

TEST(Subdivide_edges, SmoothnessBulgesOutward)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::Mesh& cube_mesh = cube->get_mesh();
    const GEO::index_t a = cube_mesh.edges.vertex(0, 0);
    const GEO::index_t b = cube_mesh.edges.vertex(0, 1);
    const GEO::vec3f midpoint = 0.5f * (position(*cube, a) + position(*cube, b));

    const std::unique_ptr<Geometry> straight = subdivide(*cube, {{a, b}});
    expect_result(*straight, Counts{9, 13, 6});
    EXPECT_NE(find_vertex_at(*straight, midpoint), GEO::NO_INDEX);

    const std::unique_ptr<Geometry> smooth = subdivide(*cube, {{a, b}}, Subdivide_edges_options{.smoothness = 1.0f});
    expect_result(*smooth, Counts{9, 13, 6});
    EXPECT_EQ(find_vertex_at(*smooth, midpoint), GEO::NO_INDEX);
    // The new vertex is the one not at a cube corner.
    GEO::index_t new_vertex = GEO::NO_INDEX;
    for (GEO::index_t vertex = 0; vertex < smooth->get_mesh().vertices.nb(); ++vertex) {
        if (find_vertex_at(*cube, position(*smooth, vertex)) == GEO::NO_INDEX) {
            new_vertex = vertex;
        }
    }
    ASSERT_NE(new_vertex, GEO::NO_INDEX);
    const GEO::vec3f p = position(*smooth, new_vertex);
    // Outward: farther from the cube centre than the straight midpoint, and
    // still equidistant from the two endpoints.
    EXPECT_GT(GEO::length(p), GEO::length(midpoint) + 0.01f);
    EXPECT_NEAR(GEO::length(p - position(*cube, a)), GEO::length(p - position(*cube, b)), 1e-4f);
}
