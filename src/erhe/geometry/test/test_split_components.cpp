#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/split_components.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;
using erhe::geometry::operation::Rip_options;

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
// +Z. Vertex (x, y) is y * 5 + x, facet (x, y) is y * 4 + x. Every vertex
// carries vertex_texcoord_0 and every corner corner_texcoord_0, both
// position.xy / 4. 25 vertices, 40 edges, 16 facets.
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

// The facet's vertex at p, GEO::NO_INDEX when none.
auto facet_vertex_at(const Geometry& geometry, const GEO::index_t facet, const GEO::vec3f& p) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (GEO::index_t i = 0; i < mesh.facets.nb_vertices(facet); ++i) {
        const GEO::index_t vertex = mesh.facets.vertex(facet, i);
        if (is_near(position(geometry, vertex), p)) {
            return vertex;
        }
    }
    return GEO::NO_INDEX;
}

// True when no other facet uses a vertex of the facet.
auto is_detached(const Geometry& geometry, const GEO::index_t facet) -> bool
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    std::set<GEO::index_t> vertices;
    for (GEO::index_t i = 0; i < mesh.facets.nb_vertices(facet); ++i) {
        vertices.insert(mesh.facets.vertex(facet, i));
    }
    for (const GEO::index_t other : mesh.facets) {
        if (other == facet) {
            continue;
        }
        for (GEO::index_t i = 0; i < mesh.facets.nb_vertices(other); ++i) {
            if (vertices.contains(mesh.facets.vertex(other, i))) {
                return false;
            }
        }
    }
    return true;
}

void expect_corner_texcoords_follow_positions(const Geometry& geometry, const GEO::index_t facet)
{
    const GEO::Mesh&                       mesh       = geometry.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    for (GEO::index_t corner = mesh.facets.corners_begin(facet); corner < mesh.facets.corners_end(facet); ++corner) {
        const GEO::vec3f                p        = position(geometry, mesh.facet_corners.vertex(corner));
        const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
        ASSERT_TRUE(texcoord.has_value());
        EXPECT_NEAR(texcoord.value().x, p.x / 4.0f, epsilon);
        EXPECT_NEAR(texcoord.value().y, p.y / 4.0f, epsilon);
    }
}

} // anonymous namespace

TEST(SplitComponents, SplitInteriorGridQuad)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection{};
    selection.facets = {grid_facet(1, 1)};
    Geometry_component_selection remapped{};
    Component_remap remap{&selection, &remapped};
    Geometry result{"split"};
    erhe::geometry::operation::split_facets(*grid, result, selection, &remap);
    expect_counts(result, Counts{.vertices = 29, .edges = 44, .facets = 16});
    EXPECT_TRUE(is_detached(result, grid_facet(1, 1)));
    expect_corner_texcoords_follow_positions(result, grid_facet(1, 1));
    EXPECT_EQ(remapped.facets, (std::set<GEO::index_t>{grid_facet(1, 1)}));
}

TEST(SplitComponents, SplitTwoAdjacentGridQuads)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection{};
    selection.facets = {grid_facet(1, 1), grid_facet(2, 1)};
    Geometry_component_selection remapped{};
    Component_remap remap{&selection, &remapped};
    Geometry result{"split"};
    erhe::geometry::operation::split_facets(*grid, result, selection, &remap);
    // 6 boundary vertices and 6 boundary edges duplicate; the shared edge moves.
    expect_counts(result, Counts{.vertices = 31, .edges = 46, .facets = 16});
    EXPECT_EQ(remapped.facets, (std::set<GEO::index_t>{grid_facet(1, 1), grid_facet(2, 1)}));
    // The two region quads still share their middle edge.
    const GEO::vec3f shared{2.0f, 1.0f, 0.0f};
    EXPECT_EQ(facet_vertex_at(result, grid_facet(1, 1), shared), facet_vertex_at(result, grid_facet(2, 1), shared));
    EXPECT_NE(facet_vertex_at(result, grid_facet(1, 1), shared), facet_vertex_at(result, grid_facet(1, 0), shared));
}

TEST(SplitComponents, SplitCubeFaceFromVertexSelection)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::Mesh& mesh = cube->get_mesh();
    Geometry_component_selection selection{};
    for (GEO::index_t i = 0; i < mesh.facets.nb_vertices(cube_y_pos); ++i) {
        selection.vertices.insert(mesh.facets.vertex(cube_y_pos, i));
    }
    Geometry_component_selection remapped{};
    Component_remap remap{&selection, &remapped};
    Geometry result{"split"};
    erhe::geometry::operation::split_facets(*cube, result, selection, &remap);
    expect_counts(result, Counts{.vertices = 12, .edges = 16, .facets = 6});
    EXPECT_TRUE(is_detached(result, cube_y_pos));
    // Vertex mode: the region's vertices, which are the copies.
    ASSERT_EQ(remapped.vertices.size(), 4u);
    for (const GEO::index_t vertex : remapped.vertices) {
        EXPECT_GE(vertex, 8u);
    }
}

TEST(SplitComponents, EdgeSplitInteriorGridEdge)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const std::pair<GEO::index_t, GEO::index_t> edge{grid_vertex(2, 1), grid_vertex(2, 2)};
    Geometry_component_selection selection{};
    selection.edges = {edge};
    Geometry_component_selection remapped{};
    Component_remap remap{&selection, &remapped};
    Geometry result{"split"};
    erhe::geometry::operation::split_edges(*grid, result, selection.edges, &remap);
    // Both endpoints tear (each along the continuation of the edge), so the
    // edge and one continuation edge per end duplicate.
    expect_counts(result, Counts{.vertices = 27, .edges = 43, .facets = 16});
    // The facets left and right of the edge come apart.
    const GEO::vec3f top{2.0f, 2.0f, 0.0f};
    EXPECT_NE(facet_vertex_at(result, grid_facet(1, 1), top), facet_vertex_at(result, grid_facet(2, 1), top));
    ASSERT_EQ(remapped.edges.size(), 1u);
    EXPECT_GE(remapped.edges.begin()->first, 25u);
    EXPECT_GE(remapped.edges.begin()->second, 25u);
}

TEST(SplitComponents, RipGridVertexAlongOppositeEdges)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection{};
    selection.vertices = {grid_vertex(2, 2)};
    selection.edges    = {{grid_vertex(2, 1), grid_vertex(2, 2)}, {grid_vertex(2, 2), grid_vertex(2, 3)}};
    Geometry_component_selection remapped{};
    Component_remap remap{&selection, &remapped};
    Geometry result{"rip"};
    erhe::geometry::operation::rip_vertices(*grid, result, selection, Rip_options{}, &remap);
    expect_counts(result, Counts{.vertices = 26, .edges = 42, .facets = 16});
    // The two halves: left facets share one vertex at (2, 2), right facets the other.
    const GEO::vec3f p{2.0f, 2.0f, 0.0f};
    const GEO::index_t left  = facet_vertex_at(result, grid_facet(1, 1), p);
    const GEO::index_t right = facet_vertex_at(result, grid_facet(2, 1), p);
    EXPECT_NE(left, right);
    EXPECT_EQ(left,  facet_vertex_at(result, grid_facet(1, 2), p));
    EXPECT_EQ(right, facet_vertex_at(result, grid_facet(2, 2), p));
    ASSERT_EQ(remapped.vertices.size(), 1u);
    EXPECT_TRUE((*remapped.vertices.begin() == left) || (*remapped.vertices.begin() == right));
}

TEST(SplitComponents, RipGridVertexTowardDirection)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection{};
    selection.vertices = {grid_vertex(2, 2)};
    Geometry_component_selection remapped{};
    Component_remap remap{&selection, &remapped};
    Geometry result{"rip"};
    erhe::geometry::operation::rip_vertices(*grid, result, selection, Rip_options{.direction = GEO::vec3f{1.0f, 1.0f, 0.0f}}, &remap);
    expect_counts(result, Counts{.vertices = 26, .edges = 42, .facets = 16});
    // The quad toward +x +y is ripped off the other three.
    const GEO::vec3f   p       = GEO::vec3f{2.0f, 2.0f, 0.0f};
    const GEO::index_t ripped  = facet_vertex_at(result, grid_facet(2, 2), p);
    const GEO::index_t kept    = facet_vertex_at(result, grid_facet(1, 1), p);
    EXPECT_NE(ripped, kept);
    EXPECT_EQ(kept, facet_vertex_at(result, grid_facet(2, 1), p));
    EXPECT_EQ(kept, facet_vertex_at(result, grid_facet(1, 2), p));
    EXPECT_EQ(remapped.vertices, (std::set<GEO::index_t>{ripped}));
}

TEST(SplitComponents, RipGridEdgeChain)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection{};
    selection.edges = {{grid_vertex(1, 2), grid_vertex(2, 2)}, {grid_vertex(2, 2), grid_vertex(3, 2)}};
    Geometry_component_selection remapped{};
    Component_remap remap{&selection, &remapped};
    Geometry result{"rip"};
    erhe::geometry::operation::rip_vertices(*grid, result, selection, Rip_options{}, &remap);
    // Three torn vertices; the chain's two edges and one continuation edge per
    // end duplicate.
    expect_counts(result, Counts{.vertices = 28, .edges = 44, .facets = 16});
    // Below and above the chain come apart at every chain vertex.
    for (int x = 1; x <= 3; ++x) {
        const GEO::vec3f p{static_cast<float>(x), 2.0f, 0.0f};
        const int facet_x = (x < 3) ? x : 2;
        EXPECT_NE(facet_vertex_at(result, grid_facet(facet_x, 1), p), facet_vertex_at(result, grid_facet(facet_x, 2), p)) << x;
    }
    // Edge mode: the ripped side's copies of the two chain edges.
    ASSERT_EQ(remapped.edges.size(), 2u);
    for (const std::pair<GEO::index_t, GEO::index_t>& edge : remapped.edges) {
        EXPECT_GE(edge.first,  25u);
        EXPECT_GE(edge.second, 25u);
    }
}

TEST(SplitComponents, ExtractCubeFacet)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry_component_selection selection{};
    selection.facets = {cube_y_pos};
    Geometry_component_selection kept_selection{};
    Geometry_component_selection extracted_selection{};
    Component_remap kept_remap     {&selection, &kept_selection};
    Component_remap extracted_remap{&selection, &extracted_selection};
    Geometry kept     {"kept"};
    Geometry extracted{"extracted"};
    erhe::geometry::operation::extract_facets(*cube, kept, extracted, selection.facets, &kept_remap, &extracted_remap);
    // The facet's four vertices stay in the kept mesh: other facets use them.
    expect_counts(kept,      Counts{.vertices = 8, .edges = 12, .facets = 5});
    expect_counts(extracted, Counts{.vertices = 4, .edges = 4,  .facets = 1});
    EXPECT_TRUE(kept_selection.facets.empty());
    EXPECT_EQ(extracted_selection.facets, (std::set<GEO::index_t>{0}));
}

TEST(SplitComponents, ExtractGridCornerQuad)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection{};
    selection.facets = {grid_facet(0, 0)};
    Geometry_component_selection extracted_selection{};
    Component_remap extracted_remap{&selection, &extracted_selection};
    Geometry kept     {"kept"};
    Geometry extracted{"extracted"};
    erhe::geometry::operation::extract_facets(*grid, kept, extracted, selection.facets, nullptr, &extracted_remap);
    // The corner vertex (0, 0) and the two outer edges go with the quad.
    expect_counts(kept,      Counts{.vertices = 24, .edges = 38, .facets = 15});
    expect_counts(extracted, Counts{.vertices = 4,  .edges = 4,  .facets = 1});
    expect_corner_texcoords_follow_positions(extracted, 0);
    EXPECT_EQ(extracted_selection.facets, (std::set<GEO::index_t>{0}));
}
