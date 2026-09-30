#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/topology.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"
#include "erhe_geometry/shapes/torus.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <set>
#include <span>
#include <vector>

using erhe::geometry::Edge_loop_delimit;
using erhe::geometry::Geometry;
using erhe::geometry::Region_delimit;
using erhe::geometry::Walk_shape;

namespace {

constexpr uint64_t walk_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

auto make_solid(const char* name, void (*make_fn)(GEO::Mesh&, float)) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>(name);
    make_fn(geo->get_mesh(), 1.0f);
    geo->process({.flags = walk_flags});
    return geo;
}

constexpr int torus_major_steps = 8;
constexpr int torus_minor_steps = 6;

auto make_torus_geometry() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("torus");
    erhe::geometry::shapes::make_torus(geo->get_mesh(), 1.0f, 0.25f, torus_major_steps, torus_minor_steps);
    geo->process({.flags = walk_flags});
    return geo;
}

auto torus_vertex(const int major, const int minor) -> GEO::index_t
{
    return static_cast<GEO::index_t>(((major % torus_major_steps) * torus_minor_steps) + (minor % torus_minor_steps));
}

// Open grid of grid_size x grid_size quads in the XY plane, counter-clockwise
// seen from +Z. Vertex (x, y) is y * (grid_size + 1) + x. With
// Grid_cell::split_center the quad of cell (2, 2) is split into two
// triangles along its (2, 2) - (3, 3) diagonal, making vertices (2, 2) and
// (3, 3) interior poles of valence 5.
constexpr int grid_size = 4;

enum class Grid_cell : unsigned int
{
    all_quads,
    split_center
};

auto grid_vertex(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * (grid_size + 1)) + x);
}

auto make_grid(const Grid_cell grid_cell) -> std::unique_ptr<Geometry>
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
            const GEO::index_t a = grid_vertex(x,     y    );
            const GEO::index_t b = grid_vertex(x + 1, y    );
            const GEO::index_t c = grid_vertex(x + 1, y + 1);
            const GEO::index_t d = grid_vertex(x,     y + 1);
            if ((grid_cell == Grid_cell::split_center) && (x == 2) && (y == 2)) {
                mesh.facets.create_triangle(a, b, c);
                mesh.facets.create_triangle(a, c, d);
            } else {
                mesh.facets.create_quad(a, b, c, d);
            }
        }
    }
    geo->process({.flags = walk_flags});
    return geo;
}

auto grid_edge(const Geometry& geo, const int x0, const int y0, const int x1, const int y1) -> GEO::index_t
{
    const GEO::index_t edge = geo.get_edge(grid_vertex(x0, y0), grid_vertex(x1, y1));
    EXPECT_NE(edge, GEO::NO_EDGE);
    return edge;
}

// Consecutive edges share a vertex, and for a closed walk the last edge
// shares a vertex with the first.
void expect_chained(const Geometry& geo, const std::vector<GEO::index_t>& edges, const Walk_shape shape)
{
    const GEO::Mesh& mesh = geo.get_mesh();
    const auto share_vertex = [&mesh](const GEO::index_t a, const GEO::index_t b) -> bool {
        return
            (mesh.edges.vertex(a, 0) == mesh.edges.vertex(b, 0)) ||
            (mesh.edges.vertex(a, 0) == mesh.edges.vertex(b, 1)) ||
            (mesh.edges.vertex(a, 1) == mesh.edges.vertex(b, 0)) ||
            (mesh.edges.vertex(a, 1) == mesh.edges.vertex(b, 1));
    };
    for (std::size_t i = 1; i < edges.size(); ++i) {
        EXPECT_TRUE(share_vertex(edges[i - 1], edges[i])) << "edges " << edges[i - 1] << " and " << edges[i];
    }
    if ((shape == Walk_shape::closed) && (edges.size() > 2)) {
        EXPECT_TRUE(share_vertex(edges.back(), edges.front()));
    }
}

// Consecutive facets share an edge.
void expect_facets_chained(const Geometry& geo, const std::vector<GEO::index_t>& facets)
{
    const GEO::Mesh& mesh = geo.get_mesh();
    for (std::size_t i = 1; i < facets.size(); ++i) {
        bool shared = false;
        for (const GEO::index_t ca : mesh.facets.corners(facets[i - 1])) {
            for (const GEO::index_t cb : mesh.facets.corners(facets[i])) {
                if (geo.get_corner_edge(ca) == geo.get_corner_edge(cb)) {
                    shared = true;
                }
            }
        }
        EXPECT_TRUE(shared) << "facets " << facets[i - 1] << " and " << facets[i];
    }
}

// Expects `actual` to be `expected` in order or reversed.
void expect_sequence(const std::vector<GEO::index_t>& actual, const std::vector<GEO::index_t>& expected)
{
    std::vector<GEO::index_t> reversed{expected.rbegin(), expected.rend()};
    EXPECT_TRUE((actual == expected) || (actual == reversed));
}

void expect_unique(const std::vector<GEO::index_t>& elements)
{
    const std::set<GEO::index_t> unique{elements.begin(), elements.end()};
    EXPECT_EQ(unique.size(), elements.size());
}

} // anonymous namespace

TEST(TopologyWalkers, Cube)
{
    std::unique_ptr<Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    const GEO::Mesh& mesh = cube->get_mesh();
    ASSERT_EQ(mesh.edges.nb(), 12u);

    std::vector<GEO::index_t> out;
    for (GEO::index_t seed = 0; seed < mesh.edges.nb(); ++seed) {
        // Every cube vertex has valence 3 and there is no hub (quads only):
        // the loop is the seed edge alone.
        EXPECT_EQ(erhe::geometry::walk_edge_loop(*cube, seed, Edge_loop_delimit::outer_corners, out), Walk_shape::open);
        ASSERT_EQ(out.size(), 1u);
        EXPECT_EQ(out.front(), seed);

        EXPECT_EQ(erhe::geometry::walk_edge_ring(*cube, seed, out), Walk_shape::closed);
        EXPECT_EQ(out.size(), 4u);
        EXPECT_EQ(out.front(), seed);
        expect_unique(out);
        for (const GEO::index_t edge : out) { // a ring of a cube is four parallel, disjoint edges
            for (const GEO::index_t other : out) {
                if (edge != other) {
                    EXPECT_NE(mesh.edges.vertex(edge, 0), mesh.edges.vertex(other, 0));
                    EXPECT_NE(mesh.edges.vertex(edge, 0), mesh.edges.vertex(other, 1));
                }
            }
        }

        EXPECT_EQ(erhe::geometry::walk_face_loop(*cube, seed, out), Walk_shape::closed);
        EXPECT_EQ(out.size(), 4u);
        expect_unique(out);
        expect_facets_chained(*cube, out);

        erhe::geometry::walk_boundary_loop(*cube, seed, out);
        EXPECT_TRUE(out.empty());
    }
}

// Pentagonal prism: bottom pentagon 0..4, top pentagon 5..9, five side quads.
// Every vertex has valence 3 and three facets.
TEST(TopologyWalkers, PentagonalPrismHub)
{
    Geometry prism{"prism"};
    GEO::Mesh& mesh = prism.get_mesh();
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(10);
    for (GEO::index_t i = 0; i < 5; ++i) {
        const float angle = 6.2831853f * static_cast<float>(i) / 5.0f;
        erhe::geometry::set_pointf(mesh.vertices, i,     GEO::vec3f{std::cos(angle), std::sin(angle), 0.0f});
        erhe::geometry::set_pointf(mesh.vertices, i + 5, GEO::vec3f{std::cos(angle), std::sin(angle), 1.0f});
    }
    for (const GEO::index_t first : {GEO::index_t{0}, GEO::index_t{5}}) {
        const GEO::index_t facet = mesh.facets.create_polygon(5);
        for (GEO::index_t i = 0; i < 5; ++i) {
            // Bottom cap reversed so it faces -Z.
            const GEO::index_t vertex = (first == 0) ? (4 - i) : (first + i);
            mesh.facets.set_vertex(facet, i, vertex);
        }
    }
    for (GEO::index_t i = 0; i < 5; ++i) {
        const GEO::index_t j = (i + 1) % 5;
        mesh.facets.create_quad(i, j, j + 5, i + 5);
    }
    prism.process({.flags = walk_flags});

    std::vector<GEO::index_t> out;

    // A cap boundary edge: the hub is the top pentagon; the loop is its five edges.
    const GEO::index_t cap_edge = prism.get_edge(5, 6);
    ASSERT_NE(cap_edge, GEO::NO_EDGE);
    EXPECT_EQ(erhe::geometry::walk_edge_loop(prism, cap_edge, Edge_loop_delimit::outer_corners, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), 5u);
    EXPECT_EQ(out.front(), cap_edge);
    expect_unique(out);
    expect_chained(prism, out, Walk_shape::closed);
    for (GEO::index_t i = 0; i < 5; ++i) {
        const GEO::index_t edge = prism.get_edge(5 + i, 5 + ((i + 1) % 5));
        EXPECT_NE(std::find(out.begin(), out.end(), edge), out.end());
    }

    // A side edge between two quads: no hub, valence 3 ends stop the loop.
    const GEO::index_t side_edge = prism.get_edge(0, 5);
    ASSERT_NE(side_edge, GEO::NO_EDGE);
    EXPECT_EQ(erhe::geometry::walk_edge_loop(prism, side_edge, Edge_loop_delimit::outer_corners, out), Walk_shape::open);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out.front(), side_edge);
}

TEST(TopologyWalkers, Torus)
{
    std::unique_ptr<Geometry> torus = make_torus_geometry();
    std::vector<GEO::index_t> out;

    // Edge along the major direction: its loop goes around the major circle,
    // its ring and face loop around the minor circle.
    const GEO::index_t major_edge = torus->get_edge(torus_vertex(2, 3), torus_vertex(3, 3));
    ASSERT_NE(major_edge, GEO::NO_EDGE);
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*torus, major_edge, Edge_loop_delimit::outer_corners, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(torus_major_steps));
    expect_unique(out);
    expect_chained(*torus, out, Walk_shape::closed);
    for (int major = 0; major < torus_major_steps; ++major) {
        const GEO::index_t edge = torus->get_edge(torus_vertex(major, 3), torus_vertex(major + 1, 3));
        EXPECT_NE(std::find(out.begin(), out.end(), edge), out.end());
    }
    EXPECT_EQ(erhe::geometry::walk_edge_ring(*torus, major_edge, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(torus_minor_steps));
    expect_unique(out);
    EXPECT_EQ(erhe::geometry::walk_face_loop(*torus, major_edge, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(torus_minor_steps));
    expect_unique(out);
    expect_facets_chained(*torus, out);

    // Edge along the minor direction.
    const GEO::index_t minor_edge = torus->get_edge(torus_vertex(5, 1), torus_vertex(5, 2));
    ASSERT_NE(minor_edge, GEO::NO_EDGE);
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*torus, minor_edge, Edge_loop_delimit::outer_corners, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(torus_minor_steps));
    expect_unique(out);
    expect_chained(*torus, out, Walk_shape::closed);
    EXPECT_EQ(erhe::geometry::walk_edge_ring(*torus, minor_edge, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(torus_major_steps));
    expect_unique(out);
    EXPECT_EQ(erhe::geometry::walk_face_loop(*torus, minor_edge, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(torus_major_steps));
    expect_unique(out);

    erhe::geometry::walk_boundary_loop(*torus, minor_edge, out);
    EXPECT_TRUE(out.empty());
}

TEST(TopologyWalkers, GridFanOrder)
{
    for (const Grid_cell grid_cell : {Grid_cell::all_quads, Grid_cell::split_center}) {
        std::unique_ptr<Geometry> grid = make_grid(grid_cell);
        const GEO::Mesh& mesh = grid->get_mesh();
        for (GEO::index_t vertex : mesh.vertices) {
            const std::span<const GEO::index_t> corners = grid->get_vertex_corners(vertex);
            for (std::size_t i = 1; i < corners.size(); ++i) {
                // The edges at `vertex` of the facets of two consecutive corners.
                const auto vertex_edges = [&](const GEO::index_t corner) -> std::set<GEO::index_t> {
                    const GEO::index_t facet        = grid->get_corner_facet(corner);
                    const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
                    std::set<GEO::index_t> edges;
                    for (GEO::index_t lc = 0; lc < corner_count; ++lc) {
                        if (mesh.facets.corner(facet, lc) == corner) {
                            edges.insert(grid->get_corner_edge(corner));
                            edges.insert(grid->get_corner_edge(mesh.facets.corner(facet, (lc + corner_count - 1) % corner_count)));
                        }
                    }
                    return edges;
                };
                const std::set<GEO::index_t> a = vertex_edges(corners[i - 1]);
                const std::set<GEO::index_t> b = vertex_edges(corners[i]);
                std::vector<GEO::index_t> shared;
                std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(shared));
                EXPECT_EQ(shared.size(), 1u) << "vertex " << vertex << " corners " << corners[i - 1] << " and " << corners[i];
            }
        }
        // An open fan runs from boundary edge to boundary edge: the first
        // corner's outgoing facet edge and the last corner's incoming facet
        // edge are boundary edges.
        for (int i = 0; i <= grid_size; ++i) {
            for (const GEO::index_t vertex : {grid_vertex(i, 0), grid_vertex(i, grid_size), grid_vertex(0, i), grid_vertex(grid_size, i)}) {
                const std::span<const GEO::index_t> corners = grid->get_vertex_corners(vertex);
                ASSERT_FALSE(corners.empty());
                const GEO::index_t last_facet  = grid->get_corner_facet(corners.back());
                const GEO::index_t last_prev   = mesh.facets.prev_corner_around_facet(last_facet, corners.back());
                EXPECT_EQ(grid->get_edge_facets(grid->get_corner_edge(corners.front())).size(), 1u) << "vertex " << vertex;
                EXPECT_EQ(grid->get_edge_facets(grid->get_corner_edge(last_prev)).size(), 1u) << "vertex " << vertex;
            }
        }
        // A corner vertex has one corner, an edge vertex two, an interior vertex four (pole five).
        EXPECT_EQ(grid->get_vertex_corners(grid_vertex(0, 0)).size(), 1u);
        EXPECT_EQ(grid->get_vertex_corners(grid_vertex(2, 0)).size(), 2u);
        EXPECT_EQ(grid->get_vertex_corners(grid_vertex(1, 1)).size(), 4u);
    }
}

TEST(TopologyWalkers, GridLoops)
{
    std::unique_ptr<Geometry> grid = make_grid(Grid_cell::all_quads);
    std::vector<GEO::index_t> out;

    // Interior loop along row y = 2 stops at the boundary vertices (valence 3).
    const std::vector<GEO::index_t> row = {
        grid_edge(*grid, 0, 2, 1, 2),
        grid_edge(*grid, 1, 2, 2, 2),
        grid_edge(*grid, 2, 2, 3, 2),
        grid_edge(*grid, 3, 2, 4, 2)
    };
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, row[1], Edge_loop_delimit::outer_corners, out), Walk_shape::open);
    expect_sequence(out, row);

    // Boundary seed with outer corners delimit: the bottom row, stopping at
    // the convex corners.
    const std::vector<GEO::index_t> bottom = {
        grid_edge(*grid, 0, 0, 1, 0),
        grid_edge(*grid, 1, 0, 2, 0),
        grid_edge(*grid, 2, 0, 3, 0),
        grid_edge(*grid, 3, 0, 4, 0)
    };
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, bottom[1], Edge_loop_delimit::outer_corners, out), Walk_shape::open);
    expect_sequence(out, bottom);

    // Boundary seed without the delimit: the whole boundary.
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, bottom[1], Edge_loop_delimit::none, out), Walk_shape::closed);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(4 * grid_size));
    expect_unique(out);
    expect_chained(*grid, out, Walk_shape::closed);

    // Boundary loop: every boundary edge, end to end.
    erhe::geometry::walk_boundary_loop(*grid, bottom[2], out);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(4 * grid_size));
    EXPECT_EQ(out.front(), bottom[2]);
    expect_unique(out);
    expect_chained(*grid, out, Walk_shape::closed);
    for (const GEO::index_t edge : out) {
        EXPECT_EQ(grid->get_edge_facets(edge).size(), 1u);
    }
    erhe::geometry::walk_boundary_loop(*grid, row[1], out);
    EXPECT_TRUE(out.empty());

    // Ring across row edges of column x = 1: one edge per row, both boundaries.
    std::vector<GEO::index_t> column_ring;
    for (int y = 0; y <= grid_size; ++y) {
        column_ring.push_back(grid_edge(*grid, 1, y, 2, y));
    }
    EXPECT_EQ(erhe::geometry::walk_edge_ring(*grid, row[1], out), Walk_shape::open);
    expect_sequence(out, column_ring);

    // Face loop across that edge: the four quads of column x = 1.
    std::vector<GEO::index_t> column_facets;
    for (int y = 0; y < grid_size; ++y) {
        column_facets.push_back(static_cast<GEO::index_t>((y * grid_size) + 1));
    }
    EXPECT_EQ(erhe::geometry::walk_face_loop(*grid, row[1], out), Walk_shape::open);
    expect_sequence(out, column_facets);
}

TEST(TopologyWalkers, GridPole)
{
    std::unique_ptr<Geometry> grid = make_grid(Grid_cell::split_center);
    std::vector<GEO::index_t> out;
    ASSERT_EQ(grid->get_vertex_edges(grid_vertex(2, 2)).size(), 5u);

    // The loop along row y = 2 stops at the valence 5 pole (2, 2).
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, grid_edge(*grid, 0, 2, 1, 2), Edge_loop_delimit::outer_corners, out), Walk_shape::open);
    expect_sequence(out, {grid_edge(*grid, 0, 2, 1, 2), grid_edge(*grid, 1, 2, 2, 2)});

    // The ring up column x = 2 stops at the triangles of cell (2, 2).
    EXPECT_EQ(erhe::geometry::walk_edge_ring(*grid, grid_edge(*grid, 2, 0, 3, 0), out), Walk_shape::open);
    expect_sequence(out, {grid_edge(*grid, 2, 0, 3, 0), grid_edge(*grid, 2, 1, 3, 1), grid_edge(*grid, 2, 2, 3, 2)});
    EXPECT_EQ(erhe::geometry::walk_edge_ring(*grid, grid_edge(*grid, 2, 4, 3, 4), out), Walk_shape::open);
    expect_sequence(out, {grid_edge(*grid, 2, 4, 3, 4), grid_edge(*grid, 2, 3, 3, 3)});

    // The face loop up column x = 2 stops before the triangle.
    EXPECT_EQ(erhe::geometry::walk_face_loop(*grid, grid_edge(*grid, 2, 1, 3, 1), out), Walk_shape::open);
    expect_sequence(out, {static_cast<GEO::index_t>(2), static_cast<GEO::index_t>(grid_size + 2)});
}

TEST(TopologyWalkers, GridCrease)
{
    std::unique_ptr<Geometry> grid = make_grid(Grid_cell::all_quads);
    std::vector<GEO::index_t> out;

    // Crease along row y = 2 from x = 0 to x = 3.
    for (int x = 0; x < 3; ++x) {
        grid->set_edge_sharpness(grid_vertex(x, 2), grid_vertex(x + 1, 2), 1.0f);
    }

    // A crease loop follows the crease and stops where it ends.
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, grid_edge(*grid, 1, 2, 2, 2), Edge_loop_delimit::crease | Edge_loop_delimit::outer_corners, out), Walk_shape::open);
    expect_sequence(out, {grid_edge(*grid, 0, 2, 1, 2), grid_edge(*grid, 1, 2, 2, 2), grid_edge(*grid, 2, 2, 3, 2)});

    // Without the crease delimit the loop runs the full row.
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, grid_edge(*grid, 1, 2, 2, 2), Edge_loop_delimit::outer_corners, out), Walk_shape::open);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(grid_size));

    // A plain loop up column x = 1 stops at the crease.
    const Edge_loop_delimit delimit = Edge_loop_delimit::crease | Edge_loop_delimit::outer_corners;
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, grid_edge(*grid, 1, 0, 1, 1), delimit, out), Walk_shape::open);
    expect_sequence(out, {grid_edge(*grid, 1, 0, 1, 1), grid_edge(*grid, 1, 1, 1, 2)});
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, grid_edge(*grid, 1, 4, 1, 3), delimit, out), Walk_shape::open);
    expect_sequence(out, {grid_edge(*grid, 1, 2, 1, 3), grid_edge(*grid, 1, 3, 1, 4)});
    // Without the crease delimit the column loop crosses the crease.
    EXPECT_EQ(erhe::geometry::walk_edge_loop(*grid, grid_edge(*grid, 1, 0, 1, 1), Edge_loop_delimit::outer_corners, out), Walk_shape::open);
    EXPECT_EQ(out.size(), static_cast<std::size_t>(grid_size));

    // A connected region with the crease delimit still reaches around the
    // crease end; every vertex is reached.
    const GEO::index_t seed = grid_vertex(0, 0);
    erhe::geometry::walk_connected_region(*grid, std::span<const GEO::index_t>{&seed, 1}, Region_delimit::crease, out);
    EXPECT_EQ(out.size(), static_cast<std::size_t>((grid_size + 1) * (grid_size + 1)));
}

TEST(TopologyWalkers, ConnectedRegionTwoCubes)
{
    std::unique_ptr<Geometry> cube = make_solid("cube", erhe::geometry::shapes::make_cube);
    Geometry two_cubes{"two_cubes"};
    GEO::mat4f identity;
    identity.load_identity();
    two_cubes.merge_with_transform(*cube, identity);
    two_cubes.merge_with_transform(*cube, identity);
    two_cubes.process({.flags = walk_flags});
    ASSERT_EQ(two_cubes.get_mesh().vertices.nb(), 16u);

    std::vector<GEO::index_t> out;
    const GEO::index_t first_seed = 1;
    erhe::geometry::walk_connected_region(two_cubes, std::span<const GEO::index_t>{&first_seed, 1}, Region_delimit::none, out);
    EXPECT_EQ(out.size(), 8u);
    EXPECT_EQ(out.front(), first_seed);
    for (const GEO::index_t vertex : out) {
        EXPECT_LT(vertex, 8u);
    }

    const GEO::index_t second_seed = 12;
    erhe::geometry::walk_connected_region(two_cubes, std::span<const GEO::index_t>{&second_seed, 1}, Region_delimit::crease | Region_delimit::winding, out);
    EXPECT_EQ(out.size(), 8u);
    for (const GEO::index_t vertex : out) {
        EXPECT_GE(vertex, 8u);
    }

    const std::vector<GEO::index_t> both_seeds = {3, 9, 3};
    erhe::geometry::walk_connected_region(two_cubes, both_seeds, Region_delimit::none, out);
    EXPECT_EQ(out.size(), 16u);
    expect_unique(out);
}
