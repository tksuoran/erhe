#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/smooth_vertices.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Smooth_vertices_options;

namespace {

// Open grid of 4 x 4 unit quads in the XY plane. Vertex (x, y) is y * 5 + x.
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
    geo->process({.flags = Geometry::process_flag_connect | Geometry::process_flag_build_edges});
    return geo;
}

void lift(Geometry& geometry, const GEO::index_t vertex, const float z)
{
    GEO::vec3f p = erhe::geometry::get_pointf(geometry.get_mesh().vertices, vertex);
    p.z = z;
    erhe::geometry::set_pointf(geometry.get_mesh().vertices, vertex, p);
}

void expect_near(const GEO::vec3f& actual, const GEO::vec3f& expected)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-5f);
    EXPECT_NEAR(actual.y, expected.y, 1e-5f);
    EXPECT_NEAR(actual.z, expected.z, 1e-5f);
}

} // anonymous namespace

TEST(SmoothVertices, InteriorVertexReturnsTowardPlane)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const GEO::index_t vertex = grid_vertex(2, 2);
    lift(*grid, vertex, 1.0f);

    std::vector<GEO::vec3f> positions;
    erhe::geometry::operation::smooth_vertices(*grid, {vertex}, Smooth_vertices_options{.factor = 0.5f, .repeat = 1}, positions);
    ASSERT_EQ(positions.size(), 1u);
    expect_near(positions[0], GEO::vec3f{2.0f, 2.0f, 0.5f});

    erhe::geometry::operation::smooth_vertices(*grid, {vertex}, Smooth_vertices_options{.factor = 0.5f, .repeat = 2}, positions);
    ASSERT_EQ(positions.size(), 1u);
    expect_near(positions[0], GEO::vec3f{2.0f, 2.0f, 0.25f});

    erhe::geometry::operation::smooth_vertices(*grid, {vertex}, Smooth_vertices_options{.factor = 1.0f, .repeat = 1}, positions);
    expect_near(positions[0], GEO::vec3f{2.0f, 2.0f, 0.0f});

    // The geometry itself is not changed.
    expect_near(erhe::geometry::get_pointf(grid->get_mesh().vertices, vertex), GEO::vec3f{2.0f, 2.0f, 1.0f});
}

TEST(SmoothVertices, BoundaryVertexUsesItsNeighbours)
{
    // (2, 0): neighbours (1, 0), (3, 0) and (2, 1), average (2, 1/3, 0).
    const std::unique_ptr<Geometry> grid = make_grid();
    const GEO::index_t vertex = grid_vertex(2, 0);
    lift(*grid, vertex, 1.0f);
    std::vector<GEO::vec3f> positions;
    erhe::geometry::operation::smooth_vertices(*grid, {vertex}, Smooth_vertices_options{}, positions);
    ASSERT_EQ(positions.size(), 1u);
    expect_near(positions[0], GEO::vec3f{2.0f, 1.0f / 6.0f, 0.5f});
}

TEST(SmoothVertices, AdjacentSelectedVerticesUseStartPositions)
{
    // (1, 2) and (2, 2) both lifted to 1: each has four neighbours, one of
    // them the other lifted vertex, so the average z is 1/4 for both and
    // factor 0.5 lands both at 0.625, whatever the order.
    const std::unique_ptr<Geometry> grid = make_grid();
    const GEO::index_t a = grid_vertex(1, 2);
    const GEO::index_t b = grid_vertex(2, 2);
    lift(*grid, a, 1.0f);
    lift(*grid, b, 1.0f);
    std::vector<GEO::vec3f> positions;
    erhe::geometry::operation::smooth_vertices(*grid, {a, b}, Smooth_vertices_options{}, positions);
    ASSERT_EQ(positions.size(), 2u);
    expect_near(positions[0], GEO::vec3f{1.0f, 2.0f, 0.625f});
    expect_near(positions[1], GEO::vec3f{2.0f, 2.0f, 0.625f});

    // Two iterations: the second reads the first's results for both.
    // z: 0.625 -> 0.625 + 0.5 * (0.625 / 4 - 0.625) = 0.390625.
    erhe::geometry::operation::smooth_vertices(*grid, {a, b}, Smooth_vertices_options{.factor = 0.5f, .repeat = 2}, positions);
    expect_near(positions[0], GEO::vec3f{1.0f, 2.0f, 0.390625f});
    expect_near(positions[1], GEO::vec3f{2.0f, 2.0f, 0.390625f});
}

TEST(SmoothVertices, EmptySelection)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    std::vector<GEO::vec3f> positions{GEO::vec3f{1.0f, 1.0f, 1.0f}};
    erhe::geometry::operation::smooth_vertices(*grid, {}, Smooth_vertices_options{}, positions);
    EXPECT_TRUE(positions.empty());
}
