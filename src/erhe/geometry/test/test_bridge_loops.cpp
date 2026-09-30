#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/bridge_loops.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Bridge_loops_options;
using erhe::geometry::operation::Bridge_loops_result;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

constexpr float epsilon = 1e-5f;

using Vertex_pair = std::pair<GEO::index_t, GEO::index_t>;

auto make_edge(const GEO::index_t a, const GEO::index_t b) -> Vertex_pair
{
    return (a < b) ? Vertex_pair{a, b} : Vertex_pair{b, a};
}

// A geometry from positions and facets; every corner carries
// corner_texcoord_0 = position.xy / 4.
auto make_geometry(
    const char*                                   name,
    const std::vector<GEO::vec3f>&                positions,
    const std::vector<std::vector<GEO::index_t>>& facets
) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>(name);
    GEO::Mesh& mesh = geo->get_mesh();
    mesh.vertices.set_single_precision();
    mesh.vertices.create_vertices(static_cast<GEO::index_t>(positions.size()));
    for (GEO::index_t vertex = 0; vertex < positions.size(); ++vertex) {
        erhe::geometry::set_pointf(mesh.vertices, vertex, positions[vertex]);
    }
    for (const std::vector<GEO::index_t>& facet_vertices : facets) {
        const GEO::index_t facet = mesh.facets.create_polygon(static_cast<GEO::index_t>(facet_vertices.size()));
        for (GEO::index_t i = 0; i < facet_vertices.size(); ++i) {
            mesh.facets.set_vertex(facet, i, facet_vertices[i]);
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

// Two unit squares in the planes z = 0 and z = 1, the caps of a unit cube
// with their normals facing out: the bottom (vertices 0..3) faces -z, the top
// (vertices 4..7, from top_xy, counter-clockwise seen from +z) faces +z.
auto make_caps(const std::vector<GEO::vec2f>& top_xy) -> std::unique_ptr<Geometry>
{
    std::vector<GEO::vec3f> positions{
        GEO::vec3f{0.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 0.0f, 0.0f}, GEO::vec3f{1.0f, 1.0f, 0.0f}, GEO::vec3f{0.0f, 1.0f, 0.0f}
    };
    std::vector<GEO::index_t> top;
    for (const GEO::vec2f& xy : top_xy) {
        top.push_back(static_cast<GEO::index_t>(positions.size()));
        positions.push_back(GEO::vec3f{xy.x, xy.y, 1.0f});
    }
    return make_geometry("caps", positions, {{0, 3, 2, 1}, top});
}

auto make_square_caps() -> std::unique_ptr<Geometry>
{
    return make_caps({GEO::vec2f{0.0f, 0.0f}, GEO::vec2f{1.0f, 0.0f}, GEO::vec2f{1.0f, 1.0f}, GEO::vec2f{0.0f, 1.0f}});
}

// Every edge of every facet.
auto all_edges(const Geometry& geometry) -> std::set<Vertex_pair>
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    std::set<Vertex_pair> edges;
    for (const GEO::index_t facet : mesh.facets) {
        const GEO::index_t n = mesh.facets.nb_vertices(facet);
        for (GEO::index_t i = 0; i < n; ++i) {
            edges.insert(make_edge(mesh.facets.vertex(facet, i), mesh.facets.vertex(facet, (i + 1) % n)));
        }
    }
    return edges;
}

// Two strips of three unit quads in the plane z = 0, facing +z: strip A
// spans y = -1 .. 0 (vertex (x, -1) is x, (x, 0) is 4 + x), strip B spans
// y = 1 .. 2 with its vertices numbered in reverse along x ((x, 1) is
// 8 + 3 - x, (x, 2) is 12 + 3 - x). The loops are A's top row and B's bottom
// row; B's numbering makes its chain run the other way.
auto strip_a(const int x, const int row) -> GEO::index_t
{
    return static_cast<GEO::index_t>((row * 4) + x);
}

auto strip_b(const int x, const int row) -> GEO::index_t
{
    return static_cast<GEO::index_t>(8 + (row * 4) + (3 - x));
}

auto make_strips() -> std::unique_ptr<Geometry>
{
    std::vector<GEO::vec3f> positions(16);
    for (int x = 0; x <= 3; ++x) {
        positions[strip_a(x, 0)] = GEO::vec3f{static_cast<float>(x), -1.0f, 0.0f};
        positions[strip_a(x, 1)] = GEO::vec3f{static_cast<float>(x),  0.0f, 0.0f};
        positions[strip_b(x, 0)] = GEO::vec3f{static_cast<float>(x),  1.0f, 0.0f};
        positions[strip_b(x, 1)] = GEO::vec3f{static_cast<float>(x),  2.0f, 0.0f};
    }
    std::vector<std::vector<GEO::index_t>> facets;
    for (int x = 0; x < 3; ++x) {
        facets.push_back({strip_a(x, 0), strip_a(x + 1, 0), strip_a(x + 1, 1), strip_a(x, 1)});
        facets.push_back({strip_b(x, 0), strip_b(x + 1, 0), strip_b(x + 1, 1), strip_b(x, 1)});
    }
    return make_geometry("strips", positions, facets);
}

auto strip_loop_edges() -> std::set<Vertex_pair>
{
    std::set<Vertex_pair> edges;
    for (int x = 0; x < 3; ++x) {
        edges.insert(make_edge(strip_a(x, 1), strip_a(x + 1, 1)));
        edges.insert(make_edge(strip_b(x, 0), strip_b(x + 1, 0)));
    }
    return edges;
}

auto make_cube(const GEO::vec3i subdivisions) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_box(geo->get_mesh(), GEO::vec3f{2.0f, 2.0f, 2.0f}, subdivisions);
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

auto facet_normal(const Geometry& geometry, const GEO::index_t facet) -> GEO::vec3f
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const GEO::index_t n = mesh.facets.nb_vertices(facet);
    GEO::vec3f normal{0.0f, 0.0f, 0.0f};
    for (GEO::index_t i = 0; i < n; ++i) {
        const GEO::vec3f a = position(geometry, mesh.facets.vertex(facet, i));
        const GEO::vec3f b = position(geometry, mesh.facets.vertex(facet, (i + 1) % n));
        normal.x += (a.y - b.y) * (a.z + b.z);
        normal.y += (a.z - b.z) * (a.x + b.x);
        normal.z += (a.x - b.x) * (a.y + b.y);
    }
    return normal;
}

auto facet_centroid(const Geometry& geometry, const GEO::index_t facet) -> GEO::vec3f
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const GEO::index_t n = mesh.facets.nb_vertices(facet);
    GEO::vec3f sum{0.0f, 0.0f, 0.0f};
    for (GEO::index_t i = 0; i < n; ++i) {
        sum += position(geometry, mesh.facets.vertex(facet, i));
    }
    return sum / static_cast<float>(n);
}

// True when every edge with two facets is traversed in opposite directions.
auto has_consistent_winding(const Geometry& geometry) -> bool
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    std::set<Vertex_pair> directed;
    for (const GEO::index_t facet : mesh.facets) {
        const GEO::index_t n = mesh.facets.nb_vertices(facet);
        for (GEO::index_t i = 0; i < n; ++i) {
            const Vertex_pair edge{mesh.facets.vertex(facet, i), mesh.facets.vertex(facet, (i + 1) % n)};
            if (!directed.insert(edge).second) {
                return false;
            }
        }
    }
    return true;
}

// The edges between a vertex at z = 0 and one at z = 1 (the rungs of the
// caps' bridge).
auto get_cap_rungs(const Geometry& geometry) -> std::vector<Vertex_pair>
{
    std::vector<Vertex_pair> rungs;
    for (const Vertex_pair& edge : all_edges(geometry)) {
        const float za = position(geometry, edge.first).z;
        const float zb = position(geometry, edge.second).z;
        if (std::abs(za - zb) > 0.5f) {
            rungs.push_back(edge);
        }
    }
    return rungs;
}

auto is_vertical(const Geometry& geometry, const Vertex_pair& edge) -> bool
{
    const GEO::vec3f a = position(geometry, edge.first);
    const GEO::vec3f b = position(geometry, edge.second);
    return (std::abs(a.x - b.x) < epsilon) && (std::abs(a.y - b.y) < epsilon);
}

auto select_all_edges(const Geometry& geometry) -> Geometry_component_selection
{
    Geometry_component_selection selection{};
    selection.edges = all_edges(geometry);
    return selection;
}

} // anonymous namespace

TEST(Bridge_loops, FacingSquaresCloseIntoPrism)
{
    // The squares face out (bottom -z, top +z), so the bridge closes a unit
    // cube: 8 vertices, 12 edges, 6 facets, every bridge facet facing out.
    const std::unique_ptr<Geometry> caps = make_square_caps();
    expect_counts(*caps, Counts{8, 8, 2});
    const Geometry_component_selection selection = select_all_edges(*caps);
    Geometry destination{"bridged"};
    Bridge_loops_result result{};
    Geometry_component_selection after{};
    Component_remap remap{&selection, &after};
    erhe::geometry::operation::bridge_loops(*caps, destination, selection, Bridge_loops_options{}, &result, &remap);

    expect_counts(destination, Counts{8, 12, 6});
    ASSERT_EQ(result.bridge_facets.size(), 4u);
    EXPECT_TRUE(has_consistent_winding(destination));
    const GEO::vec3f centre{0.5f, 0.5f, 0.5f};
    for (const GEO::index_t facet : destination.get_mesh().facets) {
        EXPECT_GT(GEO::dot(facet_normal(destination, facet), facet_centroid(destination, facet) - centre), 0.0f) << "facet " << facet;
    }
    for (const GEO::index_t facet : result.bridge_facets) {
        EXPECT_EQ(destination.get_mesh().facets.nb_vertices(facet), 4u);
    }
    const std::vector<Vertex_pair> rungs = get_cap_rungs(destination);
    ASSERT_EQ(rungs.size(), 4u);
    for (const Vertex_pair& rung : rungs) {
        EXPECT_TRUE(is_vertical(destination, rung));
    }

    // The remap selects the bridge facets with their vertices and edges.
    EXPECT_EQ(after.facets, (std::set<GEO::index_t>{result.bridge_facets.begin(), result.bridge_facets.end()}));
    EXPECT_EQ(after.vertices.size(), 8u);
    EXPECT_EQ(after.edges.size(), 12u);
}

TEST(Bridge_loops, SquareToTriangleRepeatsOneEntry)
{
    // 4 against 3: the triangle's loop repeats one vertex, which yields one
    // triangle among three quads; 7 vertices, 4 + 3 loop edges + 4 rungs.
    const std::unique_ptr<Geometry> caps = make_caps({GEO::vec2f{0.0f, 0.0f}, GEO::vec2f{1.0f, 0.0f}, GEO::vec2f{0.5f, 1.0f}});
    expect_counts(*caps, Counts{7, 7, 2});
    Geometry destination{"bridged"};
    Bridge_loops_result result{};
    erhe::geometry::operation::bridge_loops(*caps, destination, select_all_edges(*caps), Bridge_loops_options{}, &result, nullptr);

    expect_counts(destination, Counts{7, 11, 6});
    ASSERT_EQ(result.bridge_facets.size(), 4u);
    std::size_t quads     = 0;
    std::size_t triangles = 0;
    for (const GEO::index_t facet : result.bridge_facets) {
        const GEO::index_t n = destination.get_mesh().facets.nb_vertices(facet);
        quads     += (n == 4) ? 1 : 0;
        triangles += (n == 3) ? 1 : 0;
    }
    EXPECT_EQ(quads, 3u);
    EXPECT_EQ(triangles, 1u);
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Bridge_loops, ClosedLoopsFindTheNearestRotation)
{
    // The top square's numbering starts at the opposite corner: the rotation
    // minimizing the paired distances still makes every rung vertical.
    const std::unique_ptr<Geometry> caps = make_caps({GEO::vec2f{1.0f, 1.0f}, GEO::vec2f{0.0f, 1.0f}, GEO::vec2f{0.0f, 0.0f}, GEO::vec2f{1.0f, 0.0f}});
    Geometry destination{"bridged"};
    erhe::geometry::operation::bridge_loops(*caps, destination, select_all_edges(*caps), Bridge_loops_options{}, nullptr, nullptr);

    expect_counts(destination, Counts{8, 12, 6});
    const std::vector<Vertex_pair> rungs = get_cap_rungs(destination);
    ASSERT_EQ(rungs.size(), 4u);
    for (const Vertex_pair& rung : rungs) {
        EXPECT_TRUE(is_vertical(destination, rung));
    }
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Bridge_loops, TwistOffsetRotatesByOne)
{
    const std::unique_ptr<Geometry> caps = make_square_caps();
    Bridge_loops_options options{};
    options.twist_offset = 1;
    Geometry destination{"bridged"};
    erhe::geometry::operation::bridge_loops(*caps, destination, select_all_edges(*caps), options, nullptr, nullptr);

    expect_counts(destination, Counts{8, 12, 6});
    const std::vector<Vertex_pair> rungs = get_cap_rungs(destination);
    ASSERT_EQ(rungs.size(), 4u);
    for (const Vertex_pair& rung : rungs) {
        EXPECT_FALSE(is_vertical(destination, rung));
        // One step around the square: the rung spans one unit side.
        const GEO::vec3f d = position(destination, rung.first) - position(destination, rung.second);
        EXPECT_NEAR((d.x * d.x) + (d.y * d.y), 1.0f, epsilon);
    }
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Bridge_loops, OpenLoopsFlipTheReversedSecond)
{
    // Strip B's chain runs against strip A's: B is flipped, so every rung
    // joins equal x. 16 vertices, 20 + 4 rungs = 24 edges, 6 + 3 facets, all
    // facing +z like the strips.
    const std::unique_ptr<Geometry> strips = make_strips();
    expect_counts(*strips, Counts{16, 20, 6});
    Geometry_component_selection selection{};
    selection.edges = strip_loop_edges();
    Geometry destination{"bridged"};
    Bridge_loops_result result{};
    erhe::geometry::operation::bridge_loops(*strips, destination, selection, Bridge_loops_options{}, &result, nullptr);

    expect_counts(destination, Counts{16, 24, 9});
    ASSERT_EQ(result.bridge_facets.size(), 3u);
    EXPECT_TRUE(has_consistent_winding(destination));
    for (const GEO::index_t facet : destination.get_mesh().facets) {
        EXPECT_GT(facet_normal(destination, facet).z, 0.0f) << "facet " << facet;
    }
    for (int x = 0; x <= 3; ++x) {
        EXPECT_TRUE(all_edges(destination).contains(make_edge(strip_a(x, 1), strip_b(x, 0)))) << "x " << x;
    }

    // Corners copied from the strips' corners on the same loop edge.
    const GEO::Mesh& mesh = destination.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = destination.get_attributes();
    for (const GEO::index_t facet : result.bridge_facets) {
        for (GEO::index_t corner = mesh.facets.corners_begin(facet); corner < mesh.facets.corners_end(facet); ++corner) {
            const GEO::vec3f p = position(destination, mesh.facet_corners.vertex(corner));
            const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
            ASSERT_TRUE(texcoord.has_value());
            EXPECT_NEAR(texcoord.value().x, p.x / 4.0f, epsilon);
            EXPECT_NEAR(texcoord.value().y, p.y / 4.0f, epsilon);
        }
    }
}

TEST(Bridge_loops, MergeWeldsAtTheFactor)
{
    // The loops (4 vertices each) weld into one at y = 0.5: 16 - 4 = 12
    // vertices, B's three loop edges fold onto A's (20 - 3 = 17), no facet
    // added.
    const std::unique_ptr<Geometry> strips = make_strips();
    Geometry_component_selection selection{};
    selection.edges = strip_loop_edges();
    Bridge_loops_options options{};
    options.merge        = true;
    options.merge_factor = 0.5f;
    Geometry destination{"merged"};
    Bridge_loops_result result{};
    erhe::geometry::operation::bridge_loops(*strips, destination, selection, options, &result, nullptr);

    expect_counts(destination, Counts{12, 17, 6});
    EXPECT_TRUE(result.bridge_facets.empty());
    EXPECT_TRUE(has_consistent_winding(destination));
    std::size_t at_middle = 0;
    for (GEO::index_t vertex = 0; vertex < destination.get_mesh().vertices.nb(); ++vertex) {
        at_middle += (std::abs(position(destination, vertex).y - 0.5f) < epsilon) ? 1 : 0;
    }
    EXPECT_EQ(at_middle, 4u);
    for (const GEO::index_t facet : destination.get_mesh().facets) {
        EXPECT_GT(facet_normal(destination, facet).z, 0.0f) << "facet " << facet;
    }
}

TEST(Bridge_loops, CutsSplitEachBridgeQuad)
{
    // One cut: each rung splits (4 new vertices, 4 more halves), each bridge
    // quad becomes two (4 connecting edges): 12 vertices, 20 edges, 2 + 8
    // facets.
    const std::unique_ptr<Geometry> caps = make_square_caps();
    Bridge_loops_options options{};
    options.cuts = 1;
    Geometry destination{"bridged"};
    Bridge_loops_result result{};
    const Geometry_component_selection selection = select_all_edges(*caps);
    Geometry_component_selection after{};
    Component_remap remap{&selection, &after};
    erhe::geometry::operation::bridge_loops(*caps, destination, selection, options, &result, &remap);

    expect_counts(destination, Counts{12, 20, 10});
    EXPECT_EQ(result.bridge_facets.size(), 8u);
    EXPECT_EQ(after.facets.size(), 8u);
    EXPECT_TRUE(has_consistent_winding(destination));
    std::size_t at_half = 0;
    for (GEO::index_t vertex = 0; vertex < destination.get_mesh().vertices.nb(); ++vertex) {
        at_half += (std::abs(position(destination, vertex).z - 0.5f) < epsilon) ? 1 : 0;
    }
    EXPECT_EQ(at_half, 4u);
}

TEST(Bridge_loops, FaceSelectionBridgesTheRegionBoundaries)
{
    // A box with one interior plane along y (12 vertices, 20 edges, 10
    // facets): the top and bottom faces are deleted and their rims bridged
    // through the inside, a torus-like shell: 12 vertices, 20 + 4 rungs
    // edges, 8 sides + 4 bridge facets.
    const std::unique_ptr<Geometry> cube = make_cube(GEO::vec3i{0, 1, 0});
    expect_counts(*cube, Counts{12, 20, 10});
    Geometry_component_selection selection{};
    for (const GEO::index_t facet : cube->get_mesh().facets) {
        if (std::abs(std::abs(facet_centroid(*cube, facet).y) - 1.0f) < epsilon) {
            selection.facets.insert(facet);
        }
    }
    ASSERT_EQ(selection.facets.size(), 2u);
    Geometry destination{"bridged"};
    Bridge_loops_result result{};
    erhe::geometry::operation::bridge_loops(*cube, destination, selection, Bridge_loops_options{}, &result, nullptr);

    expect_counts(destination, Counts{12, 24, 12});
    EXPECT_EQ(result.bridge_facets.size(), 4u);
    EXPECT_TRUE(has_consistent_winding(destination));
}

TEST(Bridge_loops, PlainCubeOppositeFacesBridgeNothing)
{
    // On a plain cube each bridge quad would repeat a side face (and give the
    // vertical edges a third face): nothing is bridged, the cube is unchanged.
    const std::unique_ptr<Geometry> cube = make_cube(GEO::vec3i{0, 0, 0});
    Geometry_component_selection selection{};
    for (const GEO::index_t facet : cube->get_mesh().facets) {
        if (std::abs(std::abs(facet_centroid(*cube, facet).y) - 1.0f) < epsilon) {
            selection.facets.insert(facet);
        }
    }
    ASSERT_EQ(selection.facets.size(), 2u);
    Geometry destination{"bridged"};
    Bridge_loops_result result{};
    erhe::geometry::operation::bridge_loops(*cube, destination, selection, Bridge_loops_options{}, &result, nullptr);

    expect_counts(destination, Counts{8, 12, 6});
    EXPECT_TRUE(result.bridge_facets.empty());
}

TEST(Bridge_loops, VertexWithThreeSelectedEdgesIsAnError)
{
    const std::unique_ptr<Geometry> cube = make_cube(GEO::vec3i{0, 0, 0});
    Geometry_component_selection selection{};
    for (const Vertex_pair& edge : all_edges(*cube)) {
        if ((edge.first == 0) || (edge.second == 0)) {
            selection.edges.insert(edge);
        }
    }
    ASSERT_EQ(selection.edges.size(), 3u);
    Geometry destination{"bridged"};
    Bridge_loops_result result{};
    Geometry_component_selection after{};
    Component_remap remap{&selection, &after};
    erhe::geometry::operation::bridge_loops(*cube, destination, selection, Bridge_loops_options{}, &result, &remap);

    expect_counts(destination, Counts{8, 12, 6});
    EXPECT_TRUE(result.bridge_facets.empty());
    EXPECT_TRUE(after.is_empty());
}
