#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/inset_faces.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;
using erhe::geometry::operation::Inset_faces_options;
using erhe::geometry::operation::Inset_faces_result;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

constexpr float epsilon = 1e-5f;

// make_box facet order: 0 x+, 1 y+, 2 z+, 3 x-, 4 y-, 5 z-.
constexpr GEO::index_t cube_y_pos = 1;
constexpr GEO::index_t cube_z_pos = 2;

// A 2 x 2 x 2 cube; every corner carries corner_texcoord_0 = ((x + 1) / 2, (z + 1) / 2).
auto make_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    GEO::Mesh& mesh = geo->get_mesh();
    erhe::geometry::shapes::make_box(mesh, 2.0f, 2.0f, 2.0f);
    erhe::geometry::Mesh_attributes& attributes = geo->get_attributes();
    for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
        const GEO::vec3f p = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
        attributes.corner_texcoord_0.set(corner, GEO::vec2f{(p.x + 1.0f) / 2.0f, (p.z + 1.0f) / 2.0f});
    }
    geo->process({.flags = edge_flags});
    return geo;
}

// Open grid of 4 x 4 unit quads in the XY plane, counter-clockwise seen from
// +Z. Vertex (x, y) is y * 5 + x, facet (x, y) is y * 4 + x.
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

// True when one of the result's inset vertices sits at p.
auto has_inset_vertex_at(const Geometry& geometry, const Inset_faces_result& result, const GEO::vec3f& p) -> bool
{
    for (const GEO::index_t vertex : result.inset_vertices) {
        if (is_near(position(geometry, vertex), p)) {
            return true;
        }
    }
    return false;
}

auto run(
    const Geometry&               source,
    const std::set<GEO::index_t>& facets,
    const Inset_faces_options&    options,
    Inset_faces_result&           result
) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("inset");
    erhe::geometry::operation::inset_faces(source, *destination, facets, options, &result);
    return destination;
}

} // anonymous namespace

TEST(InsetFaces, CubeOneFacet)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*cube, {cube_y_pos}, Inset_faces_options{.thickness = 0.25f}, result);
    expect_counts(*inset, Counts{.vertices = 12, .edges = 20, .facets = 10});

    // Even offset on a square: each corner moves 0.25 / cos(45 deg) along the
    // diagonal, so the inset square is inset by 0.25 on both axes.
    ASSERT_EQ(result.inset_vertices.size(), 4u);
    for (const GEO::vec3f& p : {
        GEO::vec3f{ 0.75f, 1.0f,  0.75f}, GEO::vec3f{-0.75f, 1.0f,  0.75f},
        GEO::vec3f{ 0.75f, 1.0f, -0.75f}, GEO::vec3f{-0.75f, 1.0f, -0.75f}
    }) {
        EXPECT_TRUE(has_inset_vertex_at(*inset, result, p)) << p.x << " " << p.y << " " << p.z;
    }

    // Interpolate: the inset facet's corner texcoords are re-sampled from the
    // original facet at the moved positions.
    ASSERT_EQ(result.inset_facets.size(), 1u);
    const GEO::Mesh&                       mesh       = inset->get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = inset->get_attributes();
    const GEO::index_t                     facet      = result.inset_facets.front();
    for (GEO::index_t corner = mesh.facets.corners_begin(facet); corner < mesh.facets.corners_end(facet); ++corner) {
        const GEO::vec3f p = position(*inset, mesh.facet_corners.vertex(corner));
        ASSERT_TRUE(attributes.corner_texcoord_0.has(corner));
        const GEO::vec2f uv = attributes.corner_texcoord_0.get(corner);
        EXPECT_NEAR(uv.x, (p.x + 1.0f) / 2.0f, epsilon);
        EXPECT_NEAR(uv.y, (p.z + 1.0f) / 2.0f, epsilon);
    }
}

TEST(InsetFaces, ResultContents)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*cube, {cube_y_pos}, Inset_faces_options{.thickness = 0.25f}, result);
    ASSERT_EQ(result.inset_vertices.size(),   4u);
    ASSERT_EQ(result.inset_directions.size(), 4u);
    ASSERT_EQ(result.depth_directions.size(), 4u);
    EXPECT_EQ(result.inset_facets.size(),     1u);
    ASSERT_EQ(result.rim_facets.size(),       4u);
    EXPECT_TRUE(std::is_sorted(result.inset_vertices.begin(), result.inset_vertices.end()));
    EXPECT_TRUE(std::is_sorted(result.rim_facets.begin(), result.rim_facets.end()));

    // origin + thickness * direction is the placed position; the origin is the
    // original corner (the directions are the scaled diagonals, the depth
    // direction the facet normal).
    for (std::size_t i = 0; i < result.inset_vertices.size(); ++i) {
        const GEO::vec3f p         = position(*inset, result.inset_vertices[i]);
        const GEO::vec3f direction = result.inset_directions[i];
        EXPECT_NEAR(GEO::length(direction), std::sqrt(2.0f), epsilon);
        const GEO::vec3f origin = p - (0.25f * direction);
        EXPECT_NEAR(std::abs(origin.x), 1.0f, epsilon);
        EXPECT_NEAR(origin.y,           1.0f, epsilon);
        EXPECT_NEAR(std::abs(origin.z), 1.0f, epsilon);
        EXPECT_TRUE(is_near(result.depth_directions[i], GEO::vec3f{0.0f, 1.0f, 0.0f}));
    }
    const GEO::Mesh& mesh = inset->get_mesh();
    for (const GEO::index_t rim : result.rim_facets) {
        EXPECT_EQ(mesh.facets.nb_corners(rim), 4u);
    }
}

TEST(InsetFaces, TwoAdjacentFacetsAreOneRegion)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*cube, {cube_y_pos, cube_z_pos}, Inset_faces_options{.thickness = 0.25f}, result);
    // The shared edge is interior: 6 boundary edges, 6 split vertices, 6 rims.
    expect_counts(*inset, Counts{.vertices = 14, .edges = 24, .facets = 12});
    EXPECT_EQ(result.rim_facets.size(),   6u);
    EXPECT_EQ(result.inset_facets.size(), 2u);
    // An end of the shared edge slides along it (both tangents point along -x
    // there): (1, 1, 1) -> (0.75, 1, 1).
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{0.75f, 1.0f, 1.0f}));
}

TEST(InsetFaces, ClosedRegionIsUnchanged)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*cube, {0, 1, 2, 3, 4, 5}, Inset_faces_options{.thickness = 0.25f}, result);
    expect_counts(*inset, Counts{.vertices = 8, .edges = 12, .facets = 6});
    EXPECT_TRUE(result.inset_vertices.empty());
    EXPECT_TRUE(result.rim_facets.empty());
    EXPECT_TRUE(result.inset_facets.empty());
}

TEST(InsetFaces, IndividualTwoFacets)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(
        *cube, {cube_y_pos, cube_z_pos}, Inset_faces_options{.thickness = 0.25f, .individual = true}, result
    );
    expect_counts(*inset, Counts{.vertices = 16, .edges = 28, .facets = 14});
    EXPECT_EQ(result.inset_vertices.size(), 8u);
    EXPECT_EQ(result.rim_facets.size(),     8u);
    // Each facet insets on its own: the top facet's corner (1, 1, 1) goes to
    // (0.75, 1, 0.75), the front facet's to (0.75, 0.75, 1).
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{0.75f, 1.0f, 0.75f}));
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{0.75f, 0.75f, 1.0f}));
}

TEST(InsetFaces, Outset)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*cube, {cube_y_pos}, Inset_faces_options{.thickness = 0.25f, .outset = true}, result);
    // The region is the other five facets; its boundary is the top facet's.
    expect_counts(*inset, Counts{.vertices = 12, .edges = 20, .facets = 10});
    EXPECT_EQ(result.inset_facets.size(), 5u);
    EXPECT_EQ(result.rim_facets.size(),   4u);
    // The side walls' top corners move down: (1, 1, 1) -> (1, 0.75, 1).
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{1.0f, 0.75f, 1.0f}));
}

TEST(InsetFaces, Depth)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*cube, {cube_y_pos}, Inset_faces_options{.thickness = 0.25f, .depth = 0.1f}, result);
    expect_counts(*inset, Counts{.vertices = 12, .edges = 20, .facets = 10});
    // Displaced along the facet normal (+Y) by the depth.
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{0.75f, 1.1f, 0.75f}));
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{-0.75f, 1.1f, -0.75f}));
}

TEST(InsetFaces, RelativeOffset)
{
    // The cube's edges are 2 long: relative offset doubles the inset.
    const std::unique_ptr<Geometry> cube = make_cube();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*cube, {cube_y_pos}, Inset_faces_options{.relative_offset = true, .thickness = 0.25f}, result);
    expect_counts(*inset, Counts{.vertices = 12, .edges = 20, .facets = 10});
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{0.5f, 1.0f, 0.5f}));
}

TEST(InsetFaces, BoundaryOnAndOff)
{
    // A facet on the grid's lower mesh boundary.
    const std::unique_ptr<Geometry> grid  = make_grid();
    const GEO::index_t              facet = grid_facet(1, 0);
    {
        Inset_faces_result result;
        const std::unique_ptr<Geometry> inset = run(*grid, {facet}, Inset_faces_options{.boundary = true, .thickness = 0.25f}, result);
        // The mesh boundary edge is a boundary edge and gets a rim facet.
        expect_counts(*inset, Counts{.vertices = 29, .edges = 48, .facets = 20});
        EXPECT_EQ(result.rim_facets.size(), 4u);
        EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{1.25f, 0.25f, 0.0f}));
    }
    {
        Inset_faces_result result;
        const std::unique_ptr<Geometry> inset = run(*grid, {facet}, Inset_faces_options{.boundary = false, .thickness = 0.25f}, result);
        // No rim on the mesh boundary edge: its ends slide along it, flush.
        expect_counts(*inset, Counts{.vertices = 29, .edges = 47, .facets = 19});
        EXPECT_EQ(result.rim_facets.size(), 3u);
        EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{1.25f, 0.0f, 0.0f}));
        EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{1.75f, 0.0f, 0.0f}));
        EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{1.25f, 0.75f, 0.0f}));
    }
}

TEST(InsetFaces, PinchGetsOneInsetVertexPerFan)
{
    // Two grid facets touching at vertex (2, 2) only.
    const std::unique_ptr<Geometry> grid = make_grid();
    Inset_faces_result result;
    const std::unique_ptr<Geometry> inset = run(*grid, {grid_facet(1, 1), grid_facet(2, 2)}, Inset_faces_options{.thickness = 0.25f}, result);
    expect_counts(*inset, Counts{.vertices = 33, .edges = 56, .facets = 24});
    EXPECT_EQ(result.inset_vertices.size(), 8u);
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{1.75f, 1.75f, 0.0f}));
    EXPECT_TRUE(has_inset_vertex_at(*inset, result, GEO::vec3f{2.25f, 2.25f, 0.0f}));
}

TEST(InsetFaces, SelectionRemapsToInsetFacets)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry_component_selection source_selection;
    source_selection.facets.insert(cube_y_pos);
    Geometry_component_selection destination_selection;
    Component_remap              remap{&source_selection, &destination_selection};
    Inset_faces_result           result;
    Geometry                     inset{"inset"};
    erhe::geometry::operation::inset_faces(*cube, inset, {cube_y_pos}, Inset_faces_options{.thickness = 0.25f}, &result, &remap);
    ASSERT_EQ(result.inset_facets.size(), 1u);
    EXPECT_EQ(destination_selection.facets, std::set<GEO::index_t>{result.inset_facets.front()});
    for (const GEO::index_t rim : result.rim_facets) {
        EXPECT_FALSE(destination_selection.facets.contains(rim));
    }
}
