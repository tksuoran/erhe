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

namespace {

// The edge between the x+ and y+ facets of the cube, from z = 1 to z = -1.
auto cube_x_y_edge(const Geometry& cube) -> std::pair<GEO::index_t, GEO::index_t>
{
    return cube_edge(cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{1.0f, 1.0f, -1.0f});
}

auto all_edges(const Geometry& geometry) -> Edge_set
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    Edge_set edges;
    for (const GEO::index_t edge : mesh.edges) {
        edges.insert({mesh.edges.vertex(edge, 0), mesh.edges.vertex(edge, 1)});
    }
    return edges;
}

// The new vertices of the one cube edge bevel at the z = 1 end, in the XY
// plane.
auto front_samples(const Geometry& bevel, const Bevel_edges_result& result) -> std::vector<GEO::vec2f>
{
    std::vector<GEO::vec2f> samples;
    for (const GEO::index_t vertex : result.boundary_vertices) {
        const GEO::vec3f p = position(bevel, vertex);
        if (std::abs(p.z - 1.0f) < epsilon) {
            samples.push_back(GEO::vec2f{p.x, p.y});
        }
    }
    return samples;
}

} // anonymous namespace

TEST(BevelEdges, CubeOneEdgeSegmentsCircle)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = cube_x_y_edge(*cube);
    for (const int segments : {2, 3, 4}) {
        Bevel_edges_result result;
        const std::unique_ptr<Geometry> bevel = run(*cube, {edge}, Bevel_edges_options{.amount = 0.25f, .segments = segments}, result);
        // Both ends are three-valent with one beveled edge: no vertex patch,
        // each end gets segments - 1 samples, the strip is `segments` quads
        // and the two end facets gain the samples.
        const GEO::index_t n = static_cast<GEO::index_t>(segments);
        expect_counts(*bevel, Counts{.vertices = 10 + (2 * (n - 1)), .edges = 12 + (3 * n), .facets = 6 + n});
        EXPECT_EQ(result.beveled_edges,            1u);
        EXPECT_EQ(result.edge_facets.size(),       static_cast<std::size_t>(n));
        EXPECT_TRUE(result.vertex_facets.empty());
        EXPECT_EQ(result.boundary_vertices.size(), static_cast<std::size_t>(4 + (2 * (n - 1))));
        EXPECT_TRUE(std::is_sorted(result.boundary_vertices.begin(), result.boundary_vertices.end()));
        const GEO::Mesh& mesh = bevel->get_mesh();
        for (const GEO::index_t facet : result.edge_facets) {
            EXPECT_EQ(mesh.facets.nb_corners(facet), 4u);
            const GEO::vec3f normal = GEO::normalize(erhe::geometry::mesh_facet_normalf(mesh, facet));
            EXPECT_GT(normal.x + normal.y, 0.5f) << "segments " << segments;
        }
        // Profile 0.5: a quarter circle about (0.75, 0.75), the meet of the
        // two offset lines, radius 0.25.
        const std::vector<GEO::vec2f> samples = front_samples(*bevel, result);
        EXPECT_EQ(samples.size(), static_cast<std::size_t>(n + 1));
        for (const GEO::vec2f& sample : samples) {
            EXPECT_NEAR(GEO::length(sample - GEO::vec2f{0.75f, 0.75f}), 0.25f, 1e-4f) << "segments " << segments;
            EXPECT_GE(sample.x, 0.75f - epsilon);
            EXPECT_GE(sample.y, 0.75f - epsilon);
        }
        // The directions are exact: every new vertex minus amount times its
        // direction is its original corner.
        for (std::size_t i = 0; i < result.boundary_vertices.size(); ++i) {
            const GEO::vec3f origin = position(*bevel, result.boundary_vertices[i]) - (0.25f * result.boundary_directions[i]);
            EXPECT_TRUE(is_near(origin, GEO::vec3f{1.0f, 1.0f, 1.0f}) || is_near(origin, GEO::vec3f{1.0f, 1.0f, -1.0f}))
                << origin.x << " " << origin.y << " " << origin.z;
        }
    }
}

TEST(BevelEdges, CubeOneEdgeSegmentsOneIgnoresProfile)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = cube_x_y_edge(*cube);
    for (const float profile : {0.0f, 0.5f, 1.0f}) {
        Bevel_edges_result result;
        const std::unique_ptr<Geometry> bevel = run(*cube, {edge}, Bevel_edges_options{.amount = 0.25f, .segments = 1, .profile = profile}, result);
        expect_counts(*bevel, Counts{.vertices = 10, .edges = 15, .facets = 7});
        EXPECT_EQ(result.edge_facets.size(), 1u);
        EXPECT_EQ(result.beveled_edges,      1u);
    }
}

TEST(BevelEdges, CubeOneEdgeProfileChamfer)
{
    // Profile 0: the samples lie on the one segment line, evenly spaced.
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = cube_x_y_edge(*cube);
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, {edge}, Bevel_edges_options{.amount = 0.25f, .segments = 4, .profile = 0.0f}, result);
    expect_counts(*bevel, Counts{.vertices = 16, .edges = 24, .facets = 10});
    std::vector<GEO::vec2f> samples = front_samples(*bevel, result);
    ASSERT_EQ(samples.size(), 5u);
    for (const GEO::vec2f& sample : samples) {
        EXPECT_NEAR(sample.x + sample.y, 1.75f, epsilon);
    }
    std::sort(samples.begin(), samples.end(), [](const GEO::vec2f& a, const GEO::vec2f& b) { return a.x < b.x; });
    for (std::size_t i = 0; i < samples.size(); ++i) {
        EXPECT_NEAR(samples[i].x, 0.75f + (0.0625f * static_cast<float>(i)), 1e-4f);
    }
}

TEST(BevelEdges, CubeOneEdgeProfileSquare)
{
    // Profile 1: the samples lie on the two legs (the original facets); an
    // even count puts the middle sample at the original corner.
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = cube_x_y_edge(*cube);
    for (const int segments : {2, 3, 4}) {
        Bevel_edges_result result;
        const std::unique_ptr<Geometry> bevel = run(*cube, {edge}, Bevel_edges_options{.amount = 0.25f, .segments = segments, .profile = 1.0f}, result);
        EXPECT_EQ(bevel->validate(), std::string{});
        const std::vector<GEO::vec2f> samples = front_samples(*bevel, result);
        ASSERT_EQ(samples.size(), static_cast<std::size_t>(segments + 1));
        std::size_t at_corner = 0;
        for (const GEO::vec2f& sample : samples) {
            const bool on_x_leg = (std::abs(sample.x - 1.0f) < epsilon) && (sample.y >= (0.75f - epsilon));
            const bool on_y_leg = (std::abs(sample.y - 1.0f) < epsilon) && (sample.x >= (0.75f - epsilon));
            EXPECT_TRUE(on_x_leg || on_y_leg) << sample.x << " " << sample.y;
            if (on_x_leg && on_y_leg) {
                ++at_corner;
            }
        }
        EXPECT_EQ(at_corner, ((segments % 2) == 0) ? 1u : 0u) << "segments " << segments;
    }
}

TEST(BevelEdges, CubeAllEdgesSegments2Cutoff)
{
    // Every vertex has three beveled edges: the cutoff patch, per vertex the
    // centre triangle at the middle samples and one triangle per boundary
    // vertex. 24 boundary vertices + 12 edges * 2 ends * 1 sample = 48
    // vertices; 6 + 12 * 2 strip quads + 8 * 4 patch triangles = 62 facets;
    // (6 * 4 + 24 * 4 + 32 * 3) / 2 = 108 edges.
    const std::unique_ptr<Geometry> cube = make_cube();
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, all_edges(*cube), Bevel_edges_options{.amount = 0.25f, .segments = 2}, result);
    expect_counts(*bevel, Counts{.vertices = 48, .edges = 108, .facets = 62});
    EXPECT_EQ(result.beveled_edges,            12u);
    EXPECT_EQ(result.boundary_vertices.size(), 48u);
    EXPECT_EQ(result.edge_facets.size(),       24u);
    ASSERT_EQ(result.vertex_facets.size(),     32u);
    const GEO::Mesh& mesh = bevel->get_mesh();
    for (const GEO::index_t facet : result.vertex_facets) {
        EXPECT_EQ(mesh.facets.nb_corners(facet), 3u);
    }
    // Every facet faces away from the cube's centre, and every vertex stays
    // inside the original cube.
    for (GEO::index_t facet = 0; facet < mesh.facets.nb(); ++facet) {
        GEO::vec3f centroid{0.0f, 0.0f, 0.0f};
        for (GEO::index_t local = 0; local < mesh.facets.nb_corners(facet); ++local) {
            centroid += position(*bevel, mesh.facets.vertex(facet, local));
        }
        const GEO::vec3f normal = erhe::geometry::mesh_facet_normalf(mesh, facet);
        EXPECT_GT(GEO::dot(normal, centroid), 0.0f) << "facet " << facet;
    }
    for (GEO::index_t vertex = 0; vertex < mesh.vertices.nb(); ++vertex) {
        const GEO::vec3f p = position(*bevel, vertex);
        EXPECT_LE(std::max(std::abs(p.x), std::max(std::abs(p.y), std::abs(p.z))), 1.0f + epsilon);
    }
}

TEST(BevelEdges, CubeAllEdgesSegments3Cutoff)
{
    // Odd count: the centre facet takes both middle samples of each side (a
    // hexagon), one triangle per boundary vertex. 24 + 12 * 2 * 2 = 72
    // vertices; 6 + 12 * 3 + 8 * (3 + 1) = 74 facets; (6 * 4 + 36 * 4 + 8 *
    // (3 * 3 + 6)) / 2 = 144 edges.
    const std::unique_ptr<Geometry> cube = make_cube();
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, all_edges(*cube), Bevel_edges_options{.amount = 0.25f, .segments = 3}, result);
    expect_counts(*bevel, Counts{.vertices = 72, .edges = 144, .facets = 74});
    EXPECT_EQ(result.edge_facets.size(),   36u);
    ASSERT_EQ(result.vertex_facets.size(), 32u);
    const GEO::Mesh& mesh = bevel->get_mesh();
    std::size_t hexagons  = 0;
    std::size_t triangles = 0;
    for (const GEO::index_t facet : result.vertex_facets) {
        const GEO::index_t corners = mesh.facets.nb_corners(facet);
        hexagons  += (corners == 6) ? 1 : 0;
        triangles += (corners == 3) ? 1 : 0;
    }
    EXPECT_EQ(hexagons,  8u);
    EXPECT_EQ(triangles, 24u);
}

TEST(BevelEdges, CubeAllEdgesSegments4Cutoff)
{
    // Even count 4: per boundary vertex a triangle and one quad, the centre a
    // triangle. 24 + 12 * 2 * 3 = 96 vertices; 6 + 48 + 8 * (3 + 3 + 1) = 110
    // facets; Euler: 96 + 110 - 2 = 204 edges.
    const std::unique_ptr<Geometry> cube = make_cube();
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, all_edges(*cube), Bevel_edges_options{.amount = 0.25f, .segments = 4}, result);
    expect_counts(*bevel, Counts{.vertices = 96, .edges = 204, .facets = 110});
    EXPECT_EQ(result.vertex_facets.size(), 56u);
}

TEST(BevelEdges, TwoBeveledEdgesShareProfile)
{
    // Two top edges meeting at (1, 1, 1): the two strips share the profile
    // there (no vertex facet). One sample per profile, three profiles (the
    // shared one and the two far ends): 11 + 3 vertices; 6 + 2 * 2 facets;
    // Euler: 14 + 10 - 2 = 22 edges.
    const std::unique_ptr<Geometry> cube = make_cube();
    const Edge_set edges{
        cube_edge(*cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{ 1.0f, 1.0f, -1.0f}),
        cube_edge(*cube, GEO::vec3f{1.0f, 1.0f, 1.0f}, GEO::vec3f{-1.0f, 1.0f,  1.0f})
    };
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*cube, edges, Bevel_edges_options{.amount = 0.25f, .segments = 2}, result);
    expect_counts(*bevel, Counts{.vertices = 14, .edges = 22, .facets = 10});
    EXPECT_EQ(result.edge_facets.size(), 4u);
    EXPECT_TRUE(result.vertex_facets.empty());
}

TEST(BevelEdges, GridInteriorEdgeSegments)
{
    // A flat interior edge: each four-valent end's triangle gains the
    // profile's samples (a pentagon), which stay in the plane. 29 + 2 * 2
    // vertices; 19 + 2 facets; Euler (a disk): 33 + 21 - 1 = 53 edges.
    const std::unique_ptr<Geometry> grid = make_grid();
    Bevel_edges_result result;
    const std::unique_ptr<Geometry> bevel = run(*grid, {{grid_vertex(1, 1), grid_vertex(2, 1)}}, Bevel_edges_options{.amount = 0.25f, .segments = 3}, result);
    expect_counts(*bevel, Counts{.vertices = 33, .edges = 53, .facets = 21});
    EXPECT_EQ(result.edge_facets.size(),   3u);
    ASSERT_EQ(result.vertex_facets.size(), 2u);
    for (const GEO::index_t facet : result.vertex_facets) {
        EXPECT_EQ(bevel->get_mesh().facets.nb_corners(facet), 5u);
    }
    for (const GEO::index_t vertex : result.boundary_vertices) {
        EXPECT_NEAR(position(*bevel, vertex).z, 0.0f, epsilon);
    }
}

TEST(BevelEdges, CreasePropagation)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> beveled = cube_x_y_edge(*cube);
    // A crease away from the bevel, one on an edge at the beveled edge's end
    // (shortened by the bevel) and one on the beveled edge itself (dropped).
    const std::pair<GEO::index_t, GEO::index_t> away     = cube_edge(*cube, GEO::vec3f{-1.0f, -1.0f, 1.0f}, GEO::vec3f{-1.0f, -1.0f, -1.0f});
    const std::pair<GEO::index_t, GEO::index_t> adjacent = cube_edge(*cube, GEO::vec3f{ 1.0f,  1.0f, 1.0f}, GEO::vec3f{ 1.0f, -1.0f,  1.0f});
    cube->set_edge_sharpness(away.first,     away.second,     2.0f);
    cube->set_edge_sharpness(adjacent.first, adjacent.second, 3.0f);
    cube->set_edge_sharpness(beveled.first,  beveled.second,  4.0f);
    for (const int segments : {1, 2}) {
        Bevel_edges_result result;
        const std::unique_ptr<Geometry> bevel = run(*cube, {beveled}, Bevel_edges_options{.amount = 0.25f, .segments = segments}, result);
        EXPECT_EQ(bevel->validate(), std::string{});
        const GEO::index_t away_a     = find_vertex(*bevel, GEO::vec3f{-1.0f, -1.0f,  1.0f});
        const GEO::index_t away_b     = find_vertex(*bevel, GEO::vec3f{-1.0f, -1.0f, -1.0f});
        const GEO::index_t adjacent_a = find_vertex(*bevel, GEO::vec3f{ 1.0f,  0.75f, 1.0f});
        const GEO::index_t adjacent_b = find_vertex(*bevel, GEO::vec3f{ 1.0f, -1.0f,  1.0f});
        ASSERT_NE(away_a,     GEO::NO_INDEX);
        ASSERT_NE(away_b,     GEO::NO_INDEX);
        ASSERT_NE(adjacent_a, GEO::NO_INDEX);
        ASSERT_NE(adjacent_b, GEO::NO_INDEX);
        EXPECT_EQ(bevel->get_edge_sharpness(away_a,     away_b),     2.0f) << "segments " << segments;
        EXPECT_EQ(bevel->get_edge_sharpness(adjacent_a, adjacent_b), 3.0f) << "segments " << segments;
        // No other edge is sharp: the beveled edge's crease is gone and the
        // strip's edges carry none.
        const GEO::Mesh&                       mesh       = bevel->get_mesh();
        const erhe::geometry::Mesh_attributes& attributes = bevel->get_attributes();
        std::size_t sharp = 0;
        for (const GEO::index_t edge : mesh.edges) {
            if (attributes.edge_sharpness.has(edge) && (attributes.edge_sharpness.get(edge) > 0.0f)) {
                ++sharp;
            }
        }
        EXPECT_EQ(sharp, 2u) << "segments " << segments;
    }
}
