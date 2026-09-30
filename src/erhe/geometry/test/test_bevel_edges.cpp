#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/bevel_edges.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Bevel_edges_options;
using erhe::geometry::operation::Bevel_edges_result;
using erhe::geometry::operation::Bevel_offset_type;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;

namespace {

using Edge_set = std::set<std::pair<GEO::index_t, GEO::index_t>>;

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

constexpr float epsilon = 1e-5f;

auto make_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_box(geo->get_mesh(), 2.0f, 2.0f, 2.0f);
    geo->process({.flags = edge_flags});
    return geo;
}

// A frustum: bottom 4 x 2 at y = 0, top 2 x 1 at y = 1, so its x and z sides
// slope differently.
auto make_frustum() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("frustum");
    GEO::Mesh& mesh = geo->get_mesh();
    mesh.vertices.set_single_precision();
    const GEO::vec3f positions[8] = {
        {-2.0f, 0.0f, -1.0f}, { 2.0f, 0.0f, -1.0f}, { 2.0f, 0.0f,  1.0f}, {-2.0f, 0.0f,  1.0f},
        {-1.0f, 1.0f, -0.5f}, { 1.0f, 1.0f, -0.5f}, { 1.0f, 1.0f,  0.5f}, {-1.0f, 1.0f,  0.5f}
    };
    mesh.vertices.create_vertices(8);
    for (GEO::index_t i = 0; i < 8; ++i) {
        erhe::geometry::set_pointf(mesh.vertices, i, positions[i]);
    }
    mesh.facets.create_quad(4, 7, 6, 5); // top
    mesh.facets.create_quad(0, 1, 2, 3); // bottom
    mesh.facets.create_quad(3, 2, 6, 7); // z+
    mesh.facets.create_quad(1, 0, 4, 5); // z-
    mesh.facets.create_quad(2, 1, 5, 6); // x+
    mesh.facets.create_quad(0, 3, 7, 4); // x-
    geo->process({.flags = edge_flags});
    return geo;
}

// Open grid of 4 x 4 unit quads in the XY plane, counter-clockwise seen from
// +Z; every corner carries corner_texcoord_0 = (x / 4, y / 4).
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
    for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
        attributes.corner_texcoord_0.set(corner, GEO::vec2f{p.x / 4.0f, p.y / 4.0f});
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

void expect_counts(const Geometry& destination, const Counts& counts)
{
    const GEO::Mesh& mesh = destination.get_mesh();
    EXPECT_EQ(mesh.vertices.nb(), counts.vertices);
    EXPECT_EQ(mesh.edges.nb(),    counts.edges);
    EXPECT_EQ(mesh.facets.nb(),   counts.facets);
    EXPECT_EQ(destination.validate(), std::string{});
}

auto position(const Geometry& geometry, const GEO::index_t vertex) -> GEO::vec3f
{
    return erhe::geometry::get_pointf(geometry.get_mesh().vertices, vertex);
}

auto is_near(const GEO::vec3f& a, const GEO::vec3f& b) -> bool
{
    return (std::abs(a.x - b.x) < epsilon) && (std::abs(a.y - b.y) < epsilon) && (std::abs(a.z - b.z) < epsilon);
}

auto find_vertex(const Geometry& geometry, const GEO::vec3f& p) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
        if (is_near(position(geometry, vertex), p)) {
            return vertex;
        }
    }
    return GEO::NO_INDEX;
}

auto has_vertex_at(const Geometry& geometry, const GEO::vec3f& p) -> bool
{
    return find_vertex(geometry, p) != GEO::NO_INDEX;
}

auto cube_edge(const Geometry& cube, const GEO::vec3f& a, const GEO::vec3f& b) -> std::pair<GEO::index_t, GEO::index_t>
{
    return {find_vertex(cube, a), find_vertex(cube, b)};
}

auto run(
    const Geometry&            source,
    const Edge_set&            edges,
    const Bevel_edges_options& options,
    Bevel_edges_result&        result
) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("bevel");
    erhe::geometry::operation::bevel_edges(source, *destination, edges, options, &result);
    return destination;
}

// Distance of p from the line through a and b.
auto distance_to_line(const GEO::vec3f& p, const GEO::vec3f& a, const GEO::vec3f& b) -> float
{
    const GEO::vec3f d = GEO::normalize(b - a);
    const GEO::vec3f w = p - a;
    return GEO::length(w - (GEO::dot(w, d) * d));
}

} // anonymous namespace

TEST(BevelEdges, CubeOneEdgeOffset)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    // The edge between the x+ and y+ facets.
    const std::pair<GEO::index_t, GEO::index_t> edge = cube_edge(*cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{1.0f, 1.0f, -1.0f});
    ASSERT_NE(edge.first,  GEO::NO_INDEX);
    ASSERT_NE(edge.second, GEO::NO_INDEX);
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, {edge}, Bevel_edges_options{.amount = 0.25f}, result);
    expect_counts(*bevel, Counts{.vertices = 10, .edges = 15, .facets = 7});
    ASSERT_EQ(result.boundary_vertices.size(),   4u);
    ASSERT_EQ(result.boundary_directions.size(), 4u);
    ASSERT_EQ(result.edge_facets.size(),         1u);
    EXPECT_TRUE(result.vertex_facets.empty());
    EXPECT_TRUE(std::is_sorted(result.boundary_vertices.begin(), result.boundary_vertices.end()));

    // Single beveled edge at a three-valent vertex: the boundary vertices slide
    // onto the neighbouring edges, 0.25 from the edge in each facet.
    for (const GEO::vec3f& p : {
        GEO::vec3f{1.0f, 0.75f, 1.0f}, GEO::vec3f{0.75f, 1.0f, 1.0f},
        GEO::vec3f{1.0f, 0.75f, -1.0f}, GEO::vec3f{0.75f, 1.0f, -1.0f}
    }) {
        EXPECT_TRUE(has_vertex_at(*bevel, p)) << p.x << " " << p.y << " " << p.z;
    }
    const GEO::Mesh&   mesh  = bevel->get_mesh();
    const GEO::index_t quad  = result.edge_facets.front();
    ASSERT_EQ(mesh.facets.nb_corners(quad), 4u);
    for (GEO::index_t local = 0; local < 4; ++local) {
        const GEO::vec3f p = position(*bevel, mesh.facets.vertex(quad, local));
        EXPECT_NEAR(distance_to_line(p, GEO::vec3f{1.0f, 1.0f, 0.0f}, GEO::vec3f{1.0f, 1.0f, 1.0f}), 0.25f, epsilon);
    }
    // The quad faces outward (+x +y).
    const GEO::vec3f normal = GEO::normalize(erhe::geometry::mesh_facet_normalf(mesh, quad));
    EXPECT_GT(normal.x, 0.5f);
    EXPECT_GT(normal.y, 0.5f);

    // The directions reproduce the positions from the original vertices.
    for (std::size_t i = 0; i < result.boundary_vertices.size(); ++i) {
        const GEO::vec3f origin = position(*bevel, result.boundary_vertices[i]) - (0.25f * result.boundary_directions[i]);
        EXPECT_TRUE(is_near(origin, GEO::vec3f{1.0f, 1.0f, 1.0f}) || is_near(origin, GEO::vec3f{1.0f, 1.0f, -1.0f}))
            << origin.x << " " << origin.y << " " << origin.z;
    }
}

TEST(BevelEdges, CubeOneEdgeWidth)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = cube_edge(*cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{1.0f, 1.0f, -1.0f});
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, {edge}, Bevel_edges_options{.offset_type = Bevel_offset_type::width, .amount = 0.25f}, result);
    expect_counts(*bevel, Counts{.vertices = 10, .edges = 15, .facets = 7});
    // The two quad vertices at z = 1 are 0.25 apart (a 90 degree dihedral).
    std::vector<GEO::vec3f> front;
    for (const GEO::index_t vertex : result.boundary_vertices) {
        const GEO::vec3f p = position(*bevel, vertex);
        if (std::abs(p.z - 1.0f) < epsilon) {
            front.push_back(p);
        }
    }
    ASSERT_EQ(front.size(), 2u);
    EXPECT_NEAR(GEO::length(front[0] - front[1]), 0.25f, epsilon);
}

TEST(BevelEdges, CubeAllEdges)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::Mesh& cube_mesh = cube->get_mesh();
    Edge_set edges;
    for (const GEO::index_t edge : cube_mesh.edges) {
        edges.insert({cube_mesh.edges.vertex(edge, 0), cube_mesh.edges.vertex(edge, 1)});
    }
    ASSERT_EQ(edges.size(), 12u);
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, edges, Bevel_edges_options{.amount = 0.25f}, result);
    expect_counts(*bevel, Counts{.vertices = 24, .edges = 48, .facets = 26});
    EXPECT_EQ(result.boundary_vertices.size(), 24u);
    EXPECT_EQ(result.edge_facets.size(),       12u);
    ASSERT_EQ(result.vertex_facets.size(),      8u);
    const GEO::Mesh& mesh = bevel->get_mesh();
    for (const GEO::index_t facet : result.vertex_facets) {
        EXPECT_EQ(mesh.facets.nb_corners(facet), 3u);
    }
    // The corner triangle at (1, 1, 1): the meets 0.25 in from each edge.
    for (const GEO::vec3f& p : {
        GEO::vec3f{1.0f, 0.75f, 0.75f}, GEO::vec3f{0.75f, 1.0f, 0.75f}, GEO::vec3f{0.75f, 0.75f, 1.0f}
    }) {
        EXPECT_TRUE(has_vertex_at(*bevel, p)) << p.x << " " << p.y << " " << p.z;
    }
}

TEST(BevelEdges, BoundaryEdgeSkipped)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*grid, {{grid_vertex(0, 0), grid_vertex(1, 0)}}, Bevel_edges_options{.amount = 0.25f}, result);
    expect_counts(*bevel, Counts{.vertices = 25, .edges = 40, .facets = 16});
    EXPECT_TRUE(result.boundary_vertices.empty());
    EXPECT_TRUE(result.edge_facets.empty());
}

TEST(BevelEdges, LoopSlideOnCube)
{
    // Two top edges meeting at (1, 1, 1); the vertical edge lies between
    // them on the far side. Loop slide on: the boundary vertex is on it at the
    // amount; off: the meet of the offset lines, the same point on a cube.
    const std::unique_ptr<Geometry> cube = make_cube();
    const Edge_set edges{
        cube_edge(*cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{ 1.0f, 1.0f, -1.0f}),
        cube_edge(*cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{-1.0f, 1.0f,  1.0f})
    };
    for (const bool loop_slide : {true, false}) {
        Bevel_edges_result result;
        const std::unique_ptr<Geometry> bevel = run(*cube, edges, Bevel_edges_options{.amount = 0.25f, .loop_slide = loop_slide}, result);
        expect_counts(*bevel, Counts{.vertices = 11, .edges = 17, .facets = 8});
        EXPECT_TRUE(has_vertex_at(*bevel, GEO::vec3f{1.0f,  0.75f, 1.0f})) << loop_slide;
        EXPECT_TRUE(has_vertex_at(*bevel, GEO::vec3f{0.75f, 1.0f,  0.75f})) << loop_slide;
    }
}

TEST(BevelEdges, LoopSlideOnFrustum)
{
    // The top edges meeting at t2 = (1, 1, 0.5); the slanted edge t2 - b2
    // lies between them on the far side, and the two side facets slope
    // differently, so the offset lines reach it at different points.
    const std::unique_ptr<Geometry> frustum = make_frustum();
    const GEO::vec3f t2{1.0f, 1.0f, 0.5f};
    const GEO::vec3f b2{2.0f, 0.0f, 1.0f};
    const Edge_set edges{{6, 5}, {6, 7}};
    Bevel_edges_result on_result;
    Bevel_edges_result off_result;
    const std::unique_ptr<Geometry> on  = run(*frustum, edges, Bevel_edges_options{.amount = 0.1f, .loop_slide = true},  on_result);
    const std::unique_ptr<Geometry> off = run(*frustum, edges, Bevel_edges_options{.amount = 0.1f, .loop_slide = false}, off_result);
    expect_counts(*on,  Counts{.vertices = 11, .edges = 17, .facets = 8});
    expect_counts(*off, Counts{.vertices = 11, .edges = 17, .facets = 8});

    // The boundary vertex of t2 below the top: the one with y < 1.
    const auto find_low = [&t2](const Geometry& geometry, const Bevel_edges_result& result) -> GEO::vec3f {
        GEO::vec3f best{0.0f, 2.0f, 0.0f};
        for (const GEO::index_t vertex : result.boundary_vertices) {
            const GEO::vec3f p = position(geometry, vertex);
            if ((p.y < 1.0f - epsilon) && (GEO::length(p - t2) < 0.5f)) {
                best = p;
            }
        }
        return best;
    };
    const GEO::vec3f on_low  = find_low(*on,  on_result);
    const GEO::vec3f off_low = find_low(*off, off_result);
    ASSERT_LT(on_low.y,  1.0f);
    ASSERT_LT(off_low.y, 1.0f);
    EXPECT_NEAR(distance_to_line(on_low, t2, b2), 0.0f, epsilon);
    EXPECT_GT(distance_to_line(off_low, t2, b2), 1e-3f);
    EXPECT_GT(GEO::length(on_low - off_low), 1e-3f);
}

TEST(BevelEdges, GridInteriorEdgeTexcoords)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*grid, {{grid_vertex(1, 1), grid_vertex(2, 1)}}, Bevel_edges_options{.amount = 0.25f}, result);
    // Each end (four-valent) becomes three boundary vertices and a triangle.
    expect_counts(*bevel, Counts{.vertices = 29, .edges = 47, .facets = 19});
    EXPECT_EQ(result.edge_facets.size(),   1u);
    EXPECT_EQ(result.vertex_facets.size(), 2u);

    // The rebuilt facets' corners lie on the original edges: their texcoords
    // are linear in position.
    const GEO::Mesh&                       mesh       = bevel->get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = bevel->get_attributes();
    for (GEO::index_t facet = 0; facet < mesh.facets.nb(); ++facet) {
        if (
            (std::find(result.edge_facets.begin(),   result.edge_facets.end(),   facet) != result.edge_facets.end()) ||
            (std::find(result.vertex_facets.begin(), result.vertex_facets.end(), facet) != result.vertex_facets.end())
        ) {
            continue;
        }
        for (GEO::index_t corner = mesh.facets.corners_begin(facet); corner < mesh.facets.corners_end(facet); ++corner) {
            const GEO::vec3f p = position(*bevel, mesh.facet_corners.vertex(corner));
            ASSERT_TRUE(attributes.corner_texcoord_0.has(corner));
            const GEO::vec2f uv = attributes.corner_texcoord_0.get(corner);
            EXPECT_NEAR(uv.x, p.x / 4.0f, epsilon) << "facet " << facet;
            EXPECT_NEAR(uv.y, p.y / 4.0f, epsilon) << "facet " << facet;
        }
    }
}

TEST(BevelEdges, RemapSelectsEdgeFacets)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = cube_edge(*cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{1.0f, 1.0f, -1.0f});
    Geometry_component_selection source_selection;
    Geometry_component_selection destination_selection;
    source_selection.edges.insert((edge.first < edge.second) ? edge : std::pair<GEO::index_t, GEO::index_t>{edge.second, edge.first});
    Component_remap    remap{&source_selection, &destination_selection};
    Bevel_edges_result result;
    Geometry           destination{"bevel"};
    erhe::geometry::operation::bevel_edges(*cube, destination, {edge}, Bevel_edges_options{.amount = 0.25f}, &result, &remap);
    ASSERT_EQ(result.edge_facets.size(), 1u);
    EXPECT_EQ(destination_selection.facets, std::set<GEO::index_t>{result.edge_facets.front()});
    EXPECT_EQ(destination_selection.edges.size(),    4u);
    EXPECT_EQ(destination_selection.vertices.size(), 4u);
}
