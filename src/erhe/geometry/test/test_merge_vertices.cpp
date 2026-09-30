#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/merge_vertices.hpp"
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
using erhe::geometry::operation::Merge_by_distance_options;
using erhe::geometry::operation::Merge_type;
using erhe::geometry::operation::Merge_vertices_options;

namespace {

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

// Two unit quads side by side in the XY plane that do not share vertices:
// quad 0 is vertices 0..3 over x in [0, 1], quad 1 vertices 4..7 over
// x in [1, 2]. Vertices 4 and 7 lie within 1e-5 of vertices 1 and 2.
constexpr float duplicate_offset = 1e-5f;

auto make_split_quads() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("split quads");
    GEO::Mesh& mesh = geo->get_mesh();
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(8);
    const GEO::vec3f positions[8] = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {1.0f + duplicate_offset, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {1.0f, 1.0f + duplicate_offset, 0.0f}
    };
    for (GEO::index_t vertex = 0; vertex < 8; ++vertex) {
        erhe::geometry::set_pointf(mesh.vertices, vertex, positions[vertex]);
    }
    mesh.facets.create_quad(0, 1, 2, 3);
    mesh.facets.create_quad(4, 5, 6, 7);
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

// The endpoints (a < b) of the first cube edge.
auto first_cube_edge(const Geometry& cube) -> std::pair<GEO::index_t, GEO::index_t>
{
    const GEO::Mesh&   mesh = cube.get_mesh();
    const GEO::index_t a    = mesh.edges.vertex(0, 0);
    const GEO::index_t b    = mesh.edges.vertex(0, 1);
    return (a < b) ? std::make_pair(a, b) : std::make_pair(b, a);
}

} // anonymous namespace

TEST(Merge_vertices, AtCenterCubeEdge)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = first_cube_edge(*cube);
    Geometry_component_selection selection;
    selection.vertices = {edge.first, edge.second};
    Geometry destination{"result"};
    Geometry_component_selection remapped;
    Component_remap remap{&selection, &remapped};
    erhe::geometry::operation::merge_vertices(*cube, destination, selection, Merge_vertices_options{}, &remap);
    // The edge collapses; its two quads become triangles.
    expect_result(destination, Counts{7, 11, 6});
    EXPECT_EQ(count_facets_with_corners(destination, 3), 2u);
    const GEO::vec3f midpoint = 0.5f * (position(*cube, edge.first) + position(*cube, edge.second));
    // The lowest selected vertex survives; no lower vertex was removed, so it keeps its index.
    EXPECT_TRUE(is_near(position(destination, edge.first), midpoint));
    // Remap: the survivor is the selection.
    EXPECT_EQ(remapped.vertices, (std::set<GEO::index_t>{edge.first}));
}

TEST(Merge_vertices, AtCenterGridAveragesVertexAttributes)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection;
    selection.vertices = {grid_vertex(1, 1), grid_vertex(2, 1)};
    Geometry destination{"result"};
    erhe::geometry::operation::merge_vertices(*grid, destination, selection, Merge_vertices_options{});
    expect_result(destination, Counts{24, 39, 16});
    EXPECT_EQ(count_facets_with_corners(destination, 3), 2u);
    const GEO::index_t survivor = grid_vertex(1, 1);
    EXPECT_TRUE(is_near(position(destination, survivor), GEO::vec3f{1.5f, 1.0f, 0.0f}));
    const erhe::geometry::Mesh_attributes& attributes = destination.get_attributes();
    const std::optional<GEO::vec2f> vertex_texcoord = attributes.vertex_texcoord_0.try_get(survivor);
    ASSERT_TRUE(vertex_texcoord.has_value());
    EXPECT_NEAR(vertex_texcoord.value().x, 1.5f / 4.0f, 1e-6f);
    EXPECT_NEAR(vertex_texcoord.value().y, 1.0f / 4.0f, 1e-6f);

    // UVs off: each kept corner keeps its own corner texcoord (x of 1/4 or 2/4).
    const GEO::Mesh& mesh = destination.get_mesh();
    int survivor_corner_count = 0;
    for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
        if (mesh.facet_corners.vertex(corner) != survivor) {
            continue;
        }
        ++survivor_corner_count;
        const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
        ASSERT_TRUE(texcoord.has_value());
        EXPECT_TRUE((std::abs(texcoord.value().x - 0.25f) < 1e-6f) || (std::abs(texcoord.value().x - 0.5f) < 1e-6f));
        EXPECT_NEAR(texcoord.value().y, 0.25f, 1e-6f);
    }
    EXPECT_EQ(survivor_corner_count, 6);
}

TEST(Merge_vertices, MergeUvsGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection;
    selection.vertices = {grid_vertex(1, 1), grid_vertex(2, 1)};
    Geometry destination{"result"};
    Merge_vertices_options options{};
    options.merge_uvs = true;
    erhe::geometry::operation::merge_vertices(*grid, destination, selection, options);
    expect_result(destination, Counts{24, 39, 16});
    const GEO::index_t survivor = grid_vertex(1, 1);
    const GEO::Mesh& mesh = destination.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = destination.get_attributes();
    int survivor_corner_count = 0;
    for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
        if (mesh.facet_corners.vertex(corner) != survivor) {
            continue;
        }
        ++survivor_corner_count;
        const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
        ASSERT_TRUE(texcoord.has_value());
        EXPECT_NEAR(texcoord.value().x, 1.5f / 4.0f, 1e-6f);
        EXPECT_NEAR(texcoord.value().y, 1.0f / 4.0f, 1e-6f);
    }
    EXPECT_EQ(survivor_corner_count, 6);
}

TEST(Merge_vertices, AtPositionGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry_component_selection selection;
    selection.vertices = {grid_vertex(1, 1), grid_vertex(2, 1)};
    Geometry destination{"result"};
    Merge_vertices_options options{};
    options.type     = Merge_type::at_position;
    options.position = GEO::vec3f{1.25f, 0.5f, 0.75f};
    erhe::geometry::operation::merge_vertices(*grid, destination, selection, options);
    expect_result(destination, Counts{24, 39, 16});
    EXPECT_TRUE(is_near(position(destination, grid_vertex(1, 1)), options.position));
}

TEST(Merge_vertices, AtFirstAndAtLastKeepPosition)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = first_cube_edge(*cube);
    Geometry_component_selection selection;
    selection.vertices = {edge.first, edge.second};
    {
        Geometry destination{"first"};
        Geometry_component_selection remapped;
        Component_remap remap{&selection, &remapped};
        erhe::geometry::operation::merge_vertices(*cube, destination, selection, Merge_vertices_options{.type = Merge_type::at_first}, &remap);
        expect_result(destination, Counts{7, 11, 6});
        EXPECT_NE(find_vertex_at(destination, position(*cube, edge.first)), GEO::NO_INDEX);
        EXPECT_EQ(find_vertex_at(destination, position(*cube, edge.second)), GEO::NO_INDEX);
        EXPECT_EQ(remapped.vertices, (std::set<GEO::index_t>{edge.first}));
    }
    {
        Geometry destination{"last"};
        Geometry_component_selection remapped;
        Component_remap remap{&selection, &remapped};
        erhe::geometry::operation::merge_vertices(*cube, destination, selection, Merge_vertices_options{.type = Merge_type::at_last}, &remap);
        expect_result(destination, Counts{7, 11, 6});
        const GEO::index_t survivor = find_vertex_at(destination, position(*cube, edge.second));
        EXPECT_NE(survivor, GEO::NO_INDEX);
        EXPECT_EQ(find_vertex_at(destination, position(*cube, edge.first)), GEO::NO_INDEX);
        EXPECT_EQ(remapped.vertices, (std::set<GEO::index_t>{survivor}));
    }
}

TEST(Merge_vertices, CollapseOneEdge)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const std::pair<GEO::index_t, GEO::index_t> edge = first_cube_edge(*cube);
    Geometry_component_selection selection;
    selection.edges = {edge};
    Geometry destination{"result"};
    erhe::geometry::operation::merge_vertices(*cube, destination, selection, Merge_vertices_options{.type = Merge_type::collapse});
    expect_result(destination, Counts{7, 11, 6});
    EXPECT_EQ(count_facets_with_corners(destination, 3), 2u);
    EXPECT_EQ(count_facets_with_corners(destination, 4), 4u);
    const GEO::vec3f midpoint = 0.5f * (position(*cube, edge.first) + position(*cube, edge.second));
    EXPECT_TRUE(is_near(position(destination, edge.first), midpoint));
}

TEST(Merge_vertices, CollapseCubeFaceLoop)
{
    // A cube face's four edges form one island: its four vertices collapse
    // to one at the face center. The face goes (one vertex left), the four
    // side quads become triangles, the four side edges stay (now meeting at
    // the survivor), the opposite face and its four edges are untouched:
    // 8 - 3 = 5 vertices, 12 - 4 = 8 edges, 6 - 1 = 5 facets (4 triangles).
    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::Mesh& mesh = cube->get_mesh();
    Geometry_component_selection edge_selection;
    Geometry_component_selection vertex_selection;
    GEO::vec3f center{0.0f, 0.0f, 0.0f};
    const GEO::index_t corner_count = mesh.facets.nb_corners(0);
    for (GEO::index_t local_corner = 0; local_corner < corner_count; ++local_corner) {
        const GEO::index_t a = mesh.facets.vertex(0, local_corner);
        const GEO::index_t b = mesh.facets.vertex(0, (local_corner + 1) % corner_count);
        edge_selection.edges.insert((a < b) ? std::make_pair(a, b) : std::make_pair(b, a));
        vertex_selection.vertices.insert(a);
        center += position(*cube, a) / static_cast<float>(corner_count);
    }
    for (const Geometry_component_selection* selection : {&edge_selection, &vertex_selection}) {
        Geometry destination{"result"};
        Geometry_component_selection remapped;
        Component_remap remap{selection, &remapped};
        erhe::geometry::operation::merge_vertices(*cube, destination, *selection, Merge_vertices_options{.type = Merge_type::collapse}, &remap);
        expect_result(destination, Counts{5, 8, 5});
        EXPECT_EQ(count_facets_with_corners(destination, 3), 4u);
        const GEO::index_t survivor = find_vertex_at(destination, center);
        EXPECT_NE(survivor, GEO::NO_INDEX);
        if (selection == &vertex_selection) {
            EXPECT_EQ(remapped.vertices, (std::set<GEO::index_t>{survivor}));
        }
    }
}

TEST(Merge_by_distance, MergesDuplicatesPairKeepsLowerIndex)
{
    const std::unique_ptr<Geometry> quads = make_split_quads();
    Geometry destination{"result"};
    erhe::geometry::operation::merge_by_distance(*quads, destination, nullptr, Merge_by_distance_options{});
    // 4 and 7 merge into 1 and 2; the shared edge 1-2 is one edge.
    expect_result(destination, Counts{6, 7, 2});
    // The survivors 1 and 2 keep their indices and move to the pair means.
    EXPECT_TRUE(is_near(position(destination, 1), GEO::vec3f{1.0f + (0.5f * duplicate_offset), 0.0f, 0.0f}));
    EXPECT_TRUE(is_near(position(destination, 2), GEO::vec3f{1.0f, 1.0f + (0.5f * duplicate_offset), 0.0f}));
}

TEST(Merge_by_distance, WithoutCentroidKeepsSurvivorPosition)
{
    const std::unique_ptr<Geometry> quads = make_split_quads();
    Geometry destination{"result"};
    erhe::geometry::operation::merge_by_distance(*quads, destination, nullptr, Merge_by_distance_options{.use_centroid = false});
    expect_result(destination, Counts{6, 7, 2});
    EXPECT_EQ(position(destination, 1).x, 1.0f);
    EXPECT_EQ(position(destination, 2).y, 1.0f);
}

TEST(Merge_by_distance, CubeUnchanged)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry destination{"result"};
    erhe::geometry::operation::merge_by_distance(*cube, destination, nullptr, Merge_by_distance_options{});
    expect_result(destination, Counts{8, 12, 6});
}

TEST(Merge_by_distance, IncludeUnselected)
{
    const std::unique_ptr<Geometry> quads = make_split_quads();
    Geometry_component_selection selection;
    selection.vertices = {1, 2};
    {
        // Only 1 and 2 are candidates, and they are far apart: nothing merges.
        Geometry destination{"selected only"};
        erhe::geometry::operation::merge_by_distance(*quads, destination, &selection, Merge_by_distance_options{});
        expect_result(destination, Counts{8, 8, 2});
    }
    {
        // The unselected duplicates 4 and 7 join and survive at their own
        // positions, although their indices are higher.
        Geometry destination{"include unselected"};
        Geometry_component_selection remapped;
        Component_remap remap{&selection, &remapped};
        erhe::geometry::operation::merge_by_distance(*quads, destination, &selection, Merge_by_distance_options{.include_unselected = true}, &remap);
        expect_result(destination, Counts{6, 7, 2});
        const GEO::index_t survivor_a = find_vertex_at(destination, position(*quads, 4));
        const GEO::index_t survivor_b = find_vertex_at(destination, position(*quads, 7));
        EXPECT_NE(survivor_a, GEO::NO_INDEX);
        EXPECT_NE(survivor_b, GEO::NO_INDEX);
        EXPECT_EQ(position(destination, survivor_a).x, 1.0f + duplicate_offset);
        EXPECT_EQ(remapped.vertices, (std::set<GEO::index_t>{survivor_a, survivor_b}));
    }
}
