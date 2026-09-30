#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/flip_facets.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/reverse.hpp"
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
using erhe::geometry::operation::Normal_side;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

// Every corner carries corner_texcoord_0 = (facet, vertex), so a corner that
// keeps its provenance reads back its own facet and vertex.
void set_corner_ids(Geometry& geometry)
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    for (const GEO::index_t facet : mesh.facets) {
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            attributes.corner_texcoord_0.set(corner, GEO::vec2f{static_cast<float>(facet), static_cast<float>(vertex)});
        }
    }
}

// make_box facet order: 0 x+, 1 y+, 2 z+, 3 x-, 4 y-, 5 z-.
auto make_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_box(geo->get_mesh(), 2.0f, 2.0f, 2.0f);
    geo->process({.flags = edge_flags});
    set_corner_ids(*geo);
    return geo;
}

// Open grid of 4 x 4 unit quads in the XY plane. Vertex (x, y) is y * 5 + x,
// facet (x, y) is y * 4 + x. With alternate, the facets with odd x + y are
// wound clockwise seen from +Z, the others counter-clockwise.
constexpr int grid_size = 4;

auto grid_vertex(const int x, const int y) -> GEO::index_t
{
    return static_cast<GEO::index_t>((y * (grid_size + 1)) + x);
}

enum class Grid_winding : unsigned int
{
    consistent,
    alternating
};

auto make_grid(const Grid_winding winding) -> std::unique_ptr<Geometry>
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
            if ((winding == Grid_winding::alternating) && (((x + y) % 2) == 1)) {
                mesh.facets.create_quad(grid_vertex(x, y), grid_vertex(x, y + 1), grid_vertex(x + 1, y + 1), grid_vertex(x + 1, y));
            } else {
                mesh.facets.create_quad(grid_vertex(x, y), grid_vertex(x + 1, y), grid_vertex(x + 1, y + 1), grid_vertex(x, y + 1));
            }
        }
    }
    geo->process({.flags = edge_flags});
    set_corner_ids(*geo);
    return geo;
}

auto facet_normal(const Geometry& geometry, const GEO::index_t facet) -> GEO::vec3f
{
    return GEO::normalize(erhe::geometry::mesh_facet_normalf(geometry.get_mesh(), facet));
}

auto facet_centre(const Geometry& geometry, const GEO::index_t facet) -> GEO::vec3f
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    GEO::vec3f sum{0.0f, 0.0f, 0.0f};
    for (GEO::index_t i = 0; i < mesh.facets.nb_vertices(facet); ++i) {
        sum += erhe::geometry::get_pointf(mesh.vertices, mesh.facets.vertex(facet, i));
    }
    return sum / static_cast<float>(mesh.facets.nb_vertices(facet));
}

auto mesh_centre(const Geometry& geometry) -> GEO::vec3f
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    GEO::vec3f sum{0.0f, 0.0f, 0.0f};
    for (const GEO::index_t vertex : mesh.vertices) {
        sum += erhe::geometry::get_pointf(mesh.vertices, vertex);
    }
    return sum / static_cast<float>(mesh.vertices.nb());
}

// The facet's vertices as a cycle rotated to start at its smallest vertex.
auto canonical_cycle(const Geometry& geometry, const GEO::index_t facet) -> std::vector<GEO::index_t>
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    std::vector<GEO::index_t> cycle;
    for (GEO::index_t i = 0; i < mesh.facets.nb_vertices(facet); ++i) {
        cycle.push_back(mesh.facets.vertex(facet, i));
    }
    std::rotate(cycle.begin(), std::min_element(cycle.begin(), cycle.end()), cycle.end());
    return cycle;
}

auto reversed_cycle(std::vector<GEO::index_t> cycle) -> std::vector<GEO::index_t>
{
    std::reverse(cycle.begin(), cycle.end());
    std::rotate(cycle.begin(), std::min_element(cycle.begin(), cycle.end()), cycle.end());
    return cycle;
}

// Every corner's texcoord is the (source facet, vertex) it was set to.
void expect_corner_ids(const Geometry& geometry)
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    for (const GEO::index_t facet : mesh.facets) {
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            const GEO::index_t vertex = mesh.facet_corners.vertex(corner);
            ASSERT_TRUE(attributes.corner_texcoord_0.has(corner));
            const GEO::vec2f texcoord = attributes.corner_texcoord_0.get(corner);
            EXPECT_EQ(texcoord.x, static_cast<float>(facet));
            EXPECT_EQ(texcoord.y, static_cast<float>(vertex));
        }
    }
}

void expect_same_counts(const Geometry& a, const Geometry& b)
{
    EXPECT_EQ(a.get_mesh().vertices.nb(), b.get_mesh().vertices.nb());
    EXPECT_EQ(a.get_mesh().edges.nb(),    b.get_mesh().edges.nb());
    EXPECT_EQ(a.get_mesh().facets.nb(),   b.get_mesh().facets.nb());
    EXPECT_EQ(b.validate(), std::string{});
}

// The sign of dot(normal, centre - mesh centre) for every facet.
void expect_all_facing(const Geometry& geometry, const float sign)
{
    const GEO::vec3f centre = mesh_centre(geometry);
    for (const GEO::index_t facet : geometry.get_mesh().facets) {
        const float d = GEO::dot(facet_normal(geometry, facet), facet_centre(geometry, facet) - centre);
        EXPECT_GT(sign * d, 0.0f) << "facet " << facet;
    }
}

} // anonymous namespace

TEST(FlipFacets, FlipOneCubeFacet)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry result{"result"};
    const GEO::index_t facet = 1;
    Geometry_component_selection source_selection{};
    source_selection.facets = {facet};
    Geometry_component_selection result_selection{};
    Component_remap remap{&source_selection, &result_selection};
    erhe::geometry::operation::flip_facets(*cube, result, {facet}, &remap);

    expect_same_counts(*cube, result);
    for (const GEO::index_t f : cube->get_mesh().facets) {
        const float d = GEO::dot(facet_normal(*cube, f), facet_normal(result, f));
        if (f == facet) {
            EXPECT_NEAR(d, -1.0f, 1e-5f);
            EXPECT_EQ(canonical_cycle(result, f), reversed_cycle(canonical_cycle(*cube, f)));
        } else {
            EXPECT_NEAR(d, 1.0f, 1e-5f);
            EXPECT_EQ(canonical_cycle(result, f), canonical_cycle(*cube, f));
        }
    }
    expect_corner_ids(result);
    EXPECT_EQ(result_selection.facets, std::set<GEO::index_t>{facet});
}

TEST(FlipFacets, FlipAllEqualsReverse)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    std::set<GEO::index_t> all;
    for (const GEO::index_t facet : cube->get_mesh().facets) {
        all.insert(facet);
    }
    Geometry flipped{"flipped"};
    erhe::geometry::operation::flip_facets(*cube, flipped, all);
    Geometry reversed{"reversed"};
    erhe::geometry::operation::reverse(*cube, reversed);

    expect_same_counts(*cube, flipped);
    for (const GEO::index_t facet : cube->get_mesh().facets) {
        EXPECT_EQ(canonical_cycle(flipped, facet), canonical_cycle(reversed, facet));
        EXPECT_NEAR(GEO::dot(facet_normal(flipped, facet), facet_normal(reversed, facet)), 1.0f, 1e-5f);
    }
    expect_corner_ids(flipped);
    expect_all_facing(flipped, -1.0f);
}

TEST(FlipFacets, FlipEmptySelectionKeepsMesh)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry result{"result"};
    erhe::geometry::operation::flip_facets(*cube, result, {});
    expect_same_counts(*cube, result);
    for (const GEO::index_t facet : cube->get_mesh().facets) {
        EXPECT_EQ(canonical_cycle(result, facet), canonical_cycle(*cube, facet));
    }
}

TEST(FlipFacets, FlipNegatesCornerNormals)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    erhe::geometry::Mesh_attributes& attributes = cube->get_attributes();
    const GEO::Mesh& mesh = cube->get_mesh();
    for (const GEO::index_t facet : mesh.facets) {
        for (const GEO::index_t corner : mesh.facets.corners(facet)) {
            attributes.corner_normal.set(corner, facet_normal(*cube, facet));
        }
    }
    Geometry result{"result"};
    erhe::geometry::operation::flip_facets(*cube, result, {2});
    const GEO::Mesh& result_mesh = result.get_mesh();
    for (const GEO::index_t facet : result_mesh.facets) {
        for (const GEO::index_t corner : result_mesh.facets.corners(facet)) {
            ASSERT_TRUE(result.get_attributes().corner_normal.has(corner));
            EXPECT_NEAR(GEO::dot(result.get_attributes().corner_normal.get(corner), facet_normal(result, facet)), 1.0f, 1e-5f);
        }
    }
}

TEST(RecalculateNormals, CubeWithThreeFlippedFacetsOutside)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry scrambled{"scrambled"};
    erhe::geometry::operation::flip_facets(*cube, scrambled, {0, 2, 4});

    Geometry result{"result"};
    Geometry_component_selection source_selection{};
    source_selection.facets = {0, 1, 2, 3, 4, 5};
    Geometry_component_selection result_selection{};
    Component_remap remap{&source_selection, &result_selection};
    erhe::geometry::operation::recalculate_facet_normals(scrambled, result, {}, Normal_side::outside, &remap);
    expect_same_counts(scrambled, result);
    expect_all_facing(result, 1.0f);
    EXPECT_EQ(result_selection.facets, source_selection.facets);

    // Selecting every facet is the same as the empty (whole mesh) set.
    Geometry selected_result{"selected_result"};
    erhe::geometry::operation::recalculate_facet_normals(scrambled, selected_result, source_selection.facets, Normal_side::outside);
    expect_all_facing(selected_result, 1.0f);
}

TEST(RecalculateNormals, CubeWithThreeFlippedFacetsInside)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry scrambled{"scrambled"};
    erhe::geometry::operation::flip_facets(*cube, scrambled, {1, 3, 5});

    Geometry result{"result"};
    erhe::geometry::operation::recalculate_facet_normals(scrambled, result, {}, Normal_side::inside);
    expect_same_counts(scrambled, result);
    expect_all_facing(result, -1.0f);
}

TEST(RecalculateNormals, InwardCubeTurnsOutside)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry inward{"inward"};
    erhe::geometry::operation::reverse(*cube, inward);
    Geometry result{"result"};
    erhe::geometry::operation::recalculate_facet_normals(inward, result, {}, Normal_side::outside);
    expect_all_facing(result, 1.0f);
}

TEST(RecalculateNormals, OnlySelectedFacetsChange)
{
    // Facet 0 flipped; recalculating only facets 2 and 3 leaves facet 0 inward.
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry scrambled{"scrambled"};
    erhe::geometry::operation::flip_facets(*cube, scrambled, {0, 2});
    Geometry result{"result"};
    erhe::geometry::operation::recalculate_facet_normals(scrambled, result, {2, 3}, Normal_side::outside);
    const GEO::vec3f centre = mesh_centre(result);
    for (const GEO::index_t facet : result.get_mesh().facets) {
        const float d = GEO::dot(facet_normal(result, facet), facet_centre(result, facet) - centre);
        if (facet == 0) {
            EXPECT_LT(d, 0.0f);
        } else {
            EXPECT_GT(d, 0.0f) << "facet " << facet;
        }
    }
}

TEST(RecalculateNormals, OpenGridWithAlternatingWindings)
{
    const std::unique_ptr<Geometry> grid = make_grid(Grid_winding::alternating);
    // The alternating grid has every interior edge traversed twice in one direction.
    for (Normal_side side : {Normal_side::outside, Normal_side::inside}) {
        Geometry result{"result"};
        erhe::geometry::operation::recalculate_facet_normals(*grid, result, {}, side);
        expect_same_counts(*grid, result);
        const GEO::vec3f reference = facet_normal(result, 0);
        EXPECT_NEAR(std::abs(reference.z), 1.0f, 1e-5f);
        for (const GEO::index_t facet : result.get_mesh().facets) {
            EXPECT_NEAR(GEO::dot(facet_normal(result, facet), reference), 1.0f, 1e-5f) << "facet " << facet;
        }
        expect_corner_ids(result);
    }
}
