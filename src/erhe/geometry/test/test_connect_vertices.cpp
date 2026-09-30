#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/connect_vertices.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

constexpr float epsilon = 1e-5f;

// One polygon per entry of facets, over the given XY positions (z = 0).
auto make_polygons(
    const std::span<const GEO::vec2f>               positions,
    const std::vector<std::vector<GEO::index_t>>&   facets
) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("polygons");
    GEO::Mesh& mesh = geo->get_mesh();
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(static_cast<GEO::index_t>(positions.size()));
    for (GEO::index_t vertex = 0; vertex < positions.size(); ++vertex) {
        erhe::geometry::set_pointf(mesh.vertices, vertex, GEO::vec3f{positions[vertex].x, positions[vertex].y, 0.0f});
    }
    for (const std::vector<GEO::index_t>& facet : facets) {
        const GEO::index_t new_facet = mesh.facets.create_polygon(static_cast<GEO::index_t>(facet.size()));
        for (GEO::index_t i = 0; i < facet.size(); ++i) {
            mesh.facets.set_vertex(new_facet, i, facet[i]);
        }
    }
    geo->process({.flags = edge_flags});
    return geo;
}

// Open grid of 4 x 4 unit quads, vertex (x, y) is y * 5 + x.
auto grid_vertex(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * 5) + x);
}

auto make_grid() -> std::unique_ptr<Geometry>
{
    std::vector<GEO::vec2f> positions;
    for (int y = 0; y <= 4; ++y) {
        for (int x = 0; x <= 4; ++x) {
            positions.push_back(GEO::vec2f{static_cast<float>(x), static_cast<float>(y)});
        }
    }
    std::vector<std::vector<GEO::index_t>> facets;
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            facets.push_back({grid_vertex(x, y), grid_vertex(x + 1, y), grid_vertex(x + 1, y + 1), grid_vertex(x, y + 1)});
        }
    }
    return make_polygons(positions, facets);
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

auto has_edge(const Geometry& geometry, const GEO::index_t a, const GEO::index_t b) -> bool
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (const GEO::index_t facet : mesh.facets) {
        const GEO::index_t n = mesh.facets.nb_vertices(facet);
        for (GEO::index_t i = 0; i < n; ++i) {
            if (make_edge(mesh.facets.vertex(facet, i), mesh.facets.vertex(facet, (i + 1) % n)) == make_edge(a, b)) {
                return true;
            }
        }
    }
    return false;
}

} // anonymous namespace

TEST(Connect_vertices, OppositeCornersOfGridQuad)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const GEO::index_t a = grid_vertex(1, 1);
    const GEO::index_t b = grid_vertex(2, 2);
    Geometry_component_selection selection{};
    selection.vertices = {a, b};
    Geometry destination{"connected"};
    Geometry_component_selection after{};
    Component_remap remap{&selection, &after};
    erhe::geometry::operation::connect_selection(*grid, destination, selection, &remap);

    expect_counts(destination, Counts{25, 41, 17});
    EXPECT_TRUE(has_edge(destination, a, b));
    EXPECT_EQ(after.edges, (std::set<std::pair<GEO::index_t, GEO::index_t>>{make_edge(a, b)}));
    EXPECT_EQ(after.vertices, (std::set<GEO::index_t>{a, b}));
}

TEST(Connect_vertices, ThreeVerticesOfHexagon)
{
    std::vector<GEO::vec2f> positions;
    for (int i = 0; i < 6; ++i) {
        const float angle = static_cast<float>(i) * (3.14159265f / 3.0f);
        positions.push_back(GEO::vec2f{std::cos(angle), std::sin(angle)});
    }
    const std::unique_ptr<Geometry> hexagon = make_polygons(positions, {{0, 1, 2, 3, 4, 5}});
    Geometry destination{"connected"};
    erhe::geometry::operation::connect_vertices(*hexagon, destination, std::set<GEO::index_t>{0, 2, 4});

    expect_counts(destination, Counts{6, 9, 4});
    EXPECT_TRUE(has_edge(destination, 0, 2));
    EXPECT_TRUE(has_edge(destination, 2, 4));
    EXPECT_TRUE(has_edge(destination, 4, 0));
}

TEST(Connect_vertices, ContiguousRunInteriorIsSkipped)
{
    // Hexagon corners 0, 1, 2 selected: 1 is inside the run, only 0 - 2 splits.
    std::vector<GEO::vec2f> positions;
    for (int i = 0; i < 6; ++i) {
        const float angle = static_cast<float>(i) * (3.14159265f / 3.0f);
        positions.push_back(GEO::vec2f{std::cos(angle), std::sin(angle)});
    }
    const std::unique_ptr<Geometry> hexagon = make_polygons(positions, {{0, 1, 2, 3, 4, 5}});
    Geometry destination{"connected"};
    erhe::geometry::operation::connect_vertices(*hexagon, destination, std::set<GEO::index_t>{0, 1, 2});

    expect_counts(destination, Counts{6, 7, 2});
    EXPECT_TRUE(has_edge(destination, 0, 2));
}

TEST(Connect_vertices, PairTwoQuadsApartTakesStraightPath)
{
    // A row of three unit quads; (0, 0) and (3, 1) share no facet. The cut
    // plane is vertical through the line between them: it crosses the edges
    // x = 1 and x = 2 at y = 1/3 and y = 2/3.
    const std::vector<GEO::vec2f> positions{
        {0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.0f}, {3.0f, 0.0f},
        {0.0f, 1.0f}, {1.0f, 1.0f}, {2.0f, 1.0f}, {3.0f, 1.0f}
    };
    const std::unique_ptr<Geometry> row = make_polygons(positions, {{0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}});
    Geometry_component_selection selection{};
    selection.vertices = {0, 7};
    Geometry destination{"connected"};
    Geometry_component_selection after{};
    Component_remap remap{&selection, &after};
    erhe::geometry::operation::connect_selection(*row, destination, selection, &remap);

    expect_counts(destination, Counts{10, 15, 6});
    const GEO::Mesh& mesh = destination.get_mesh();
    std::vector<GEO::vec3f> inserted;
    for (GEO::index_t vertex = 8; vertex < mesh.vertices.nb(); ++vertex) {
        inserted.push_back(erhe::geometry::get_pointf(mesh.vertices, vertex));
    }
    ASSERT_EQ(inserted.size(), 2u);
    for (const GEO::vec3f& p : inserted) {
        EXPECT_NEAR(p.y, p.x / 3.0f, epsilon); // on the straight line
    }
    EXPECT_TRUE(has_edge(destination, 0, 8) || has_edge(destination, 0, 9));
    EXPECT_TRUE(has_edge(destination, 8, 9));
    EXPECT_TRUE(has_edge(destination, 7, 8) || has_edge(destination, 7, 9));
    EXPECT_EQ(after.edges.size(), 3u);
    EXPECT_EQ(after.vertices.size(), 4u);
}

TEST(Connect_vertices, SplitLeavingTheFacetIsDropped)
{
    // An L-shaped hexagon: corners (2, 1) and (1, 2) are not adjacent, but
    // the chord between them runs outside the facet (its midpoint (1.5, 1.5)
    // is outside). Corners (0, 0) and (1, 1) split it.
    const std::vector<GEO::vec2f> positions{
        {0.0f, 0.0f}, {2.0f, 0.0f}, {2.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 2.0f}, {0.0f, 2.0f}
    };
    const std::unique_ptr<Geometry> l_shape = make_polygons(positions, {{0, 1, 2, 3, 4, 5}});

    Geometry dropped{"dropped"};
    erhe::geometry::operation::connect_vertices(*l_shape, dropped, std::set<GEO::index_t>{2, 4});
    expect_counts(dropped, Counts{6, 6, 1});

    Geometry split{"split"};
    erhe::geometry::operation::connect_vertices(*l_shape, split, std::set<GEO::index_t>{0, 3});
    expect_counts(split, Counts{6, 7, 2});
    EXPECT_TRUE(has_edge(split, 0, 3));
}
