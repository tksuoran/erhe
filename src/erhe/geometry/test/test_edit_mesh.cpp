#include "erhe_geometry/edit_mesh.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/edit_mesh_operation.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using erhe::geometry::Collapse_result;
using erhe::geometry::Delete_context;
using erhe::geometry::Edgenet_result;
using erhe::geometry::Edit_mesh;
using erhe::geometry::Edit_source;
using erhe::geometry::Geometry;
using erhe::geometry::Join_result;
using erhe::geometry::operation::Edit_mesh_operation;
using erhe::geometry::operation::Geometry_component_selection;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

// Minimal operation: the test edits the scratch, then emits.
class Test_edit_operation : public Edit_mesh_operation
{
public:
    Test_edit_operation(const Geometry& source, Geometry& destination)
        : Edit_mesh_operation{source, destination}
    {
    }

    auto get_edit_mesh() -> Edit_mesh& { return m_edit_mesh; }
    void run_emit() { emit(); }
};

auto make_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_box(geo->get_mesh(), 2.0f, 2.0f, 2.0f);
    geo->process({.flags = edge_flags});
    return geo;
}

// Open grid of 4 x 4 quads in the XY plane, counter-clockwise seen from +Z.
// Vertex (x, y) is y * 5 + x, facet (x, y) is y * 4 + x. Every corner
// carries corner_texcoord_0 = position.xy / 4.
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

void expect_scratch_counts(const Edit_mesh& edit_mesh, const Counts& counts)
{
    EXPECT_EQ(edit_mesh.get_vertex_count(), counts.vertices);
    EXPECT_EQ(edit_mesh.get_edge_count(),   counts.edges);
    EXPECT_EQ(edit_mesh.get_facet_count(),  counts.facets);
}

// Emits and checks the destination: counts (edges are facet edges only) and validate().
void emit_and_check(Test_edit_operation& operation, const Geometry& destination, const Counts& counts)
{
    operation.run_emit();
    const GEO::Mesh& mesh = destination.get_mesh();
    EXPECT_EQ(mesh.vertices.nb(), counts.vertices);
    EXPECT_EQ(mesh.edges.nb(),    counts.edges);
    EXPECT_EQ(mesh.facets.nb(),   counts.facets);
    EXPECT_EQ(destination.validate(), std::string{});
}

auto is_near(const GEO::vec3f& a, const GEO::vec3f& b) -> bool
{
    return GEO::length(a - b) < 1e-5f;
}

auto edge_of(const Edit_mesh& edit_mesh, const GEO::index_t a, const GEO::index_t b) -> GEO::index_t
{
    const GEO::index_t edge = edit_mesh.find_edge(a, b);
    EXPECT_NE(edge, GEO::NO_INDEX);
    return edge;
}

} // anonymous namespace

TEST(Edit_mesh, LoadCopiesCounts)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry destination{"result"};
    Test_edit_operation operation{*cube, destination};
    expect_scratch_counts(operation.get_edit_mesh(), Counts{8, 12, 6});
    emit_and_check(operation, destination, Counts{8, 12, 6});
    for (GEO::index_t v = 0; v < 8; ++v) {
        EXPECT_TRUE(is_near(erhe::geometry::get_pointf(destination.get_mesh().vertices, v), erhe::geometry::get_pointf(cube->get_mesh().vertices, v)));
    }
}

TEST(Edit_mesh, SplitEdgeCube)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry destination{"result"};
    Test_edit_operation operation{*cube, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const GEO::index_t edge = edge_of(edit_mesh, 0, 1);
    edit_mesh.set_edge_sharpness(edge, 2.0f);
    const GEO::index_t middle = edit_mesh.split_edge(edge, 0.5f);
    expect_scratch_counts(edit_mesh, Counts{9, 13, 6});
    EXPECT_EQ(edit_mesh.get_vertex_edges(middle).size(), 2u);
    EXPECT_EQ(edit_mesh.get_vertex_facets(middle).size(), 2u);
    emit_and_check(operation, destination, Counts{9, 13, 6});
    EXPECT_TRUE(is_near(erhe::geometry::get_pointf(destination.get_mesh().vertices, middle), GEO::vec3f{0.0f, -1.0f, -1.0f}));
    EXPECT_EQ(destination.get_edge_sharpness(0, middle), 2.0f);
    EXPECT_EQ(destination.get_edge_sharpness(middle, 1), 2.0f);
}

TEST(Edit_mesh, SplitEdgeGridInterpolatesCornerTexcoords)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    // Edge (1, 1) - (2, 1); its first vertex is (1, 1).
    const GEO::index_t edge = edge_of(edit_mesh, grid_vertex(1, 1), grid_vertex(2, 1));
    ASSERT_EQ(edit_mesh.get_edge(edge).vertices[0], grid_vertex(1, 1));
    const GEO::index_t split = edit_mesh.split_edge(edge, 0.25f);
    EXPECT_TRUE(is_near(edit_mesh.get_position(split), GEO::vec3f{1.25f, 1.0f, 0.0f}));
    emit_and_check(operation, destination, Counts{26, 41, 16});

    const GEO::Mesh& mesh = destination.get_mesh();
    EXPECT_TRUE(is_near(erhe::geometry::get_pointf(mesh.vertices, split), GEO::vec3f{1.25f, 1.0f, 0.0f}));
    const erhe::geometry::Mesh_attributes& attributes = destination.get_attributes();
    int split_corner_count = 0;
    for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
        if (mesh.facet_corners.vertex(corner) != split) {
            continue;
        }
        ++split_corner_count;
        const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
        ASSERT_TRUE(texcoord.has_value());
        EXPECT_NEAR(texcoord.value().x, 1.25f / 4.0f, 1e-6f);
        EXPECT_NEAR(texcoord.value().y, 1.0f / 4.0f, 1e-6f);
    }
    EXPECT_EQ(split_corner_count, 2);
}

TEST(Edit_mesh, SplitFacetCube)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry destination{"result"};
    Test_edit_operation operation{*cube, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    EXPECT_EQ(edit_mesh.split_facet(0, 0, 1), GEO::NO_INDEX); // adjacent corners
    const GEO::index_t new_facet = edit_mesh.split_facet(0, 0, 2);
    ASSERT_NE(new_facet, GEO::NO_INDEX);
    EXPECT_EQ(edit_mesh.get_facet_corners(0).size(), 3u);
    EXPECT_EQ(edit_mesh.get_facet_corners(new_facet).size(), 3u);
    EXPECT_EQ(edit_mesh.get_facet(new_facet).source_facet, 0u);
    emit_and_check(operation, destination, Counts{8, 13, 7});
}

TEST(Edit_mesh, JoinFacetsGrid)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();

    GEO::index_t joined = GEO::NO_INDEX;
    const std::array<GEO::index_t, 2> apart{grid_facet(0, 0), grid_facet(2, 2)};
    EXPECT_EQ(edit_mesh.join_facets(apart, joined), Join_result::not_edge_connected);
    const std::array<GEO::index_t, 2> vertex_touching{grid_facet(0, 0), grid_facet(1, 1)};
    EXPECT_EQ(edit_mesh.join_facets(vertex_touching, joined), Join_result::not_edge_connected);
    EXPECT_EQ(joined, GEO::NO_INDEX);
    expect_scratch_counts(edit_mesh, Counts{25, 40, 16});

    const std::array<GEO::index_t, 2> adjacent{grid_facet(1, 1), grid_facet(2, 1)};
    EXPECT_EQ(edit_mesh.join_facets(adjacent, joined), Join_result::joined);
    ASSERT_NE(joined, GEO::NO_INDEX);
    EXPECT_EQ(edit_mesh.get_facet_corners(joined).size(), 6u);
    expect_scratch_counts(edit_mesh, Counts{25, 39, 15});
    emit_and_check(operation, destination, Counts{25, 39, 15});
}

TEST(Edit_mesh, JoinFacetPairCube)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry destination{"result"};
    Test_edit_operation operation{*cube, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    GEO::index_t joined = GEO::NO_INDEX;
    EXPECT_EQ(edit_mesh.join_facet_pair(edge_of(edit_mesh, 0, 1), joined), Join_result::joined);
    ASSERT_NE(joined, GEO::NO_INDEX);
    EXPECT_EQ(edit_mesh.get_facet_corners(joined).size(), 6u);
    emit_and_check(operation, destination, Counts{8, 11, 5});
}

TEST(Edit_mesh, CollapseVertexRestoresSplitEdge)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry destination{"result"};
    Test_edit_operation operation{*cube, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    EXPECT_EQ(edit_mesh.collapse_vertex(0), Collapse_result::not_two_valent);
    const GEO::index_t middle = edit_mesh.split_edge(edge_of(edit_mesh, 0, 1), 0.5f);
    expect_scratch_counts(edit_mesh, Counts{9, 13, 6});
    EXPECT_EQ(edit_mesh.collapse_vertex(middle), Collapse_result::collapsed);
    expect_scratch_counts(edit_mesh, Counts{8, 12, 6});
    EXPECT_NE(edit_mesh.find_edge(0, 1), GEO::NO_INDEX);
    emit_and_check(operation, destination, Counts{8, 12, 6});
}

TEST(Edit_mesh, WeldNonAdjacentPairSplitsFirst)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    // (1, 1) and (2, 2) are diagonal in facet (1, 1): the facet is split
    // between them, both triangles collapse to two vertices and are deleted.
    const std::array<std::pair<GEO::index_t, GEO::index_t>, 1> merges{std::make_pair(grid_vertex(1, 1), grid_vertex(2, 2))};
    edit_mesh.weld_vertices(merges);
    expect_scratch_counts(edit_mesh, Counts{24, 38, 15});
    EXPECT_FALSE(edit_mesh.is_vertex_alive(grid_vertex(1, 1)));
    EXPECT_FALSE(edit_mesh.is_facet_alive(grid_facet(1, 1)));
    EXPECT_TRUE(edit_mesh.are_adjacent_in_facet(grid_facet(0, 0), grid_vertex(1, 0), grid_vertex(2, 2)));
    emit_and_check(operation, destination, Counts{24, 38, 15});
}

TEST(Edit_mesh, WeldEdgeEndpointsCollapsesEdge)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const std::array<std::pair<GEO::index_t, GEO::index_t>, 1> merges{std::make_pair(grid_vertex(1, 1), grid_vertex(2, 1))};
    edit_mesh.weld_vertices(merges);
    expect_scratch_counts(edit_mesh, Counts{24, 39, 16});
    EXPECT_EQ(edit_mesh.get_facet_corners(grid_facet(1, 0)).size(), 3u);
    EXPECT_EQ(edit_mesh.get_facet_corners(grid_facet(1, 1)).size(), 3u);
    EXPECT_EQ(edit_mesh.find_edge(grid_vertex(1, 1), grid_vertex(2, 1)), GEO::NO_INDEX);
    EXPECT_NE(edit_mesh.find_edge(grid_vertex(0, 1), grid_vertex(2, 1)), GEO::NO_INDEX);
    emit_and_check(operation, destination, Counts{24, 39, 16});
}

TEST(Edit_mesh, SeparateVertexAlongTwoEdges)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const GEO::index_t center = grid_vertex(2, 2);
    const std::array<GEO::index_t, 2> cut{
        edge_of(edit_mesh, center, grid_vertex(1, 2)),
        edge_of(edit_mesh, center, grid_vertex(3, 2))
    };
    std::vector<GEO::index_t> vertices;
    edit_mesh.separate_vertex(center, cut, vertices);
    ASSERT_EQ(vertices.size(), 2u);
    EXPECT_TRUE(is_near(edit_mesh.get_position(vertices[1]), edit_mesh.get_position(center)));
    EXPECT_EQ(edit_mesh.get_vertex_facets(vertices[0]).size(), 2u);
    EXPECT_EQ(edit_mesh.get_vertex_facets(vertices[1]).size(), 2u);
    EXPECT_EQ(edit_mesh.get_vertex_edges(vertices[0]).size(), 3u);
    EXPECT_EQ(edit_mesh.get_vertex_edges(vertices[1]).size(), 3u);
    expect_scratch_counts(edit_mesh, Counts{26, 42, 16});
    emit_and_check(operation, destination, Counts{26, 42, 16});
}

TEST(Edit_mesh, DeleteContexts)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    const GEO::index_t corner_vertex = grid_vertex(0, 0);

    class Case
    {
    public:
        Delete_context context;
        Counts         scratch;
        Counts         emitted;
    };
    // Corner facet (0, 0), its two boundary edges at (0, 0), or vertex (2, 2).
    const std::array<Case, 5> cases{
        Case{Delete_context::vertices,             Counts{24, 36, 12}, Counts{24, 36, 12}},
        Case{Delete_context::edges,                Counts{24, 38, 15}, Counts{24, 38, 15}},
        Case{Delete_context::faces,                Counts{24, 38, 15}, Counts{24, 38, 15}},
        Case{Delete_context::only_edges_and_faces, Counts{25, 38, 15}, Counts{25, 38, 15}},
        Case{Delete_context::only_faces,           Counts{25, 40, 15}, Counts{25, 38, 15}}
    };
    for (const Case& test_case : cases) {
        SCOPED_TRACE(static_cast<int>(test_case.context));
        Geometry destination{"result"};
        Test_edit_operation operation{*grid, destination};
        Edit_mesh& edit_mesh = operation.get_edit_mesh();
        std::vector<GEO::index_t> elements;
        switch (test_case.context) {
            case Delete_context::vertices: {
                elements.push_back(grid_vertex(2, 2));
                break;
            }
            case Delete_context::edges:
            case Delete_context::only_edges_and_faces: {
                elements.push_back(edge_of(edit_mesh, corner_vertex, grid_vertex(1, 0)));
                elements.push_back(edge_of(edit_mesh, corner_vertex, grid_vertex(0, 1)));
                break;
            }
            case Delete_context::faces:
            case Delete_context::only_faces: {
                elements.push_back(grid_facet(0, 0));
                break;
            }
        }
        edit_mesh.delete_elements(elements, test_case.context);
        expect_scratch_counts(edit_mesh, test_case.scratch);
        emit_and_check(operation, destination, test_case.emitted);
    }
}

TEST(Edit_mesh, SplitFacetEdgenetChord)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const std::array<std::pair<GEO::index_t, GEO::index_t>, 1> edges{std::make_pair(grid_vertex(1, 1), grid_vertex(2, 2))};
    std::vector<GEO::index_t> facets;
    EXPECT_EQ(edit_mesh.split_facet_edgenet(grid_facet(1, 1), edges, facets), Edgenet_result::split);
    EXPECT_EQ(facets.size(), 2u);
    expect_scratch_counts(edit_mesh, Counts{25, 41, 17});
    emit_and_check(operation, destination, Counts{25, 41, 17});
}

TEST(Edit_mesh, SplitFacetEdgenetChainAndBranch)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    {
        // Chain boundary - interior - boundary: two facets.
        Geometry destination{"result"};
        Test_edit_operation operation{*grid, destination};
        Edit_mesh& edit_mesh = operation.get_edit_mesh();
        const GEO::index_t interior = edit_mesh.add_vertex(GEO::vec3f{1.5f, 1.6f, 0.0f}, std::span<const Edit_source>{});
        const std::array<std::pair<GEO::index_t, GEO::index_t>, 2> edges{
            std::make_pair(grid_vertex(1, 1), interior),
            std::make_pair(interior, grid_vertex(2, 2))
        };
        std::vector<GEO::index_t> facets;
        EXPECT_EQ(edit_mesh.split_facet_edgenet(grid_facet(1, 1), edges, facets), Edgenet_result::split);
        EXPECT_EQ(facets.size(), 2u);
        EXPECT_EQ(edit_mesh.get_vertex_edges(interior).size(), 2u);
        expect_scratch_counts(edit_mesh, Counts{26, 42, 17});
        emit_and_check(operation, destination, Counts{26, 42, 17});
        // The interior corners interpolate the facet's corner texcoords.
        const GEO::Mesh& mesh = destination.get_mesh();
        const erhe::geometry::Mesh_attributes& attributes = destination.get_attributes();
        for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
            if (mesh.facet_corners.vertex(corner) == interior) {
                const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
                ASSERT_TRUE(texcoord.has_value());
                EXPECT_NEAR(texcoord.value().x, 1.5f / 4.0f, 1e-5f);
                EXPECT_NEAR(texcoord.value().y, 1.6f / 4.0f, 1e-5f);
            }
        }
    }
    {
        // A branch at the interior vertex: three facets, valence 3.
        Geometry destination{"result"};
        Test_edit_operation operation{*grid, destination};
        Edit_mesh& edit_mesh = operation.get_edit_mesh();
        const GEO::index_t interior = edit_mesh.add_vertex(GEO::vec3f{1.5f, 1.6f, 0.0f}, std::span<const Edit_source>{});
        const std::array<std::pair<GEO::index_t, GEO::index_t>, 3> edges{
            std::make_pair(grid_vertex(1, 1), interior),
            std::make_pair(interior, grid_vertex(2, 2)),
            std::make_pair(interior, grid_vertex(2, 1))
        };
        std::vector<GEO::index_t> facets;
        EXPECT_EQ(edit_mesh.split_facet_edgenet(grid_facet(1, 1), edges, facets), Edgenet_result::split);
        EXPECT_EQ(facets.size(), 3u);
        EXPECT_EQ(edit_mesh.get_vertex_edges(interior).size(), 3u);
        expect_scratch_counts(edit_mesh, Counts{26, 43, 18});
        emit_and_check(operation, destination, Counts{26, 43, 18});
    }
}

TEST(Edit_mesh, SplitFacetEdgenetDropsDanglingEdge)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const GEO::index_t interior = edit_mesh.add_vertex(GEO::vec3f{1.3f, 1.7f, 0.0f}, std::span<const Edit_source>{});
    const std::array<std::pair<GEO::index_t, GEO::index_t>, 2> edges{
        std::make_pair(grid_vertex(1, 1), grid_vertex(2, 2)),
        std::make_pair(grid_vertex(1, 2), interior)
    };
    std::vector<GEO::index_t> facets;
    EXPECT_EQ(edit_mesh.split_facet_edgenet(grid_facet(1, 1), edges, facets), Edgenet_result::split);
    EXPECT_EQ(facets.size(), 2u);
    EXPECT_TRUE(edit_mesh.get_vertex_edges(interior).empty());
    expect_scratch_counts(edit_mesh, Counts{26, 41, 17}); // the interior vertex stays loose
    emit_and_check(operation, destination, Counts{26, 41, 17});
}

TEST(Edit_mesh, SplitFacetEdgenetFloatingIsland)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const std::array<GEO::index_t, 4> island{
        edit_mesh.add_vertex(GEO::vec3f{1.3f, 1.3f, 0.0f}, std::span<const Edit_source>{}),
        edit_mesh.add_vertex(GEO::vec3f{1.7f, 1.3f, 0.0f}, std::span<const Edit_source>{}),
        edit_mesh.add_vertex(GEO::vec3f{1.7f, 1.7f, 0.0f}, std::span<const Edit_source>{}),
        edit_mesh.add_vertex(GEO::vec3f{1.3f, 1.7f, 0.0f}, std::span<const Edit_source>{})
    };
    const std::array<std::pair<GEO::index_t, GEO::index_t>, 4> edges{
        std::make_pair(island[0], island[1]),
        std::make_pair(island[1], island[2]),
        std::make_pair(island[2], island[3]),
        std::make_pair(island[3], island[0])
    };
    std::vector<GEO::index_t> facets;
    EXPECT_EQ(edit_mesh.split_facet_edgenet(grid_facet(1, 1), edges, facets), Edgenet_result::split);
    // The island facet plus two ring facets (two connecting edges).
    ASSERT_EQ(facets.size(), 3u);
    int island_facets = 0;
    for (const GEO::index_t facet : facets) {
        bool all_island = true;
        for (const erhe::geometry::Edit_corner& corner : edit_mesh.get_facet_corners(facet)) {
            if (std::find(island.begin(), island.end(), corner.vertex) == island.end()) {
                all_island = false;
            }
        }
        if (all_island) {
            ++island_facets;
            EXPECT_EQ(edit_mesh.get_facet_corners(facet).size(), 4u);
        }
    }
    EXPECT_EQ(island_facets, 1);
    for (GEO::index_t edge = 0; edge < edit_mesh.get_edge_slot_count(); ++edge) {
        if (edit_mesh.is_edge_alive(edge)) {
            EXPECT_GT(edit_mesh.get_edge_facet_count(edge), 0u); // no dangling edges
        }
    }
    expect_scratch_counts(edit_mesh, Counts{29, 46, 18});
    emit_and_check(operation, destination, Counts{29, 46, 18});
}

TEST(Edit_mesh, CreateFacetCube)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    Geometry destination{"result"};
    Test_edit_operation operation{*cube, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const std::array<GEO::index_t, 4> quad{1, 4, 7, 5}; // facet 0 (x+)
    EXPECT_EQ(edit_mesh.create_facet(quad, GEO::NO_INDEX), GEO::NO_INDEX); // exists
    const std::array<GEO::index_t, 1> facet_0{0};
    edit_mesh.delete_elements(facet_0, Delete_context::only_faces);
    expect_scratch_counts(edit_mesh, Counts{8, 12, 5});
    const GEO::index_t created = edit_mesh.create_facet(quad, GEO::NO_INDEX);
    ASSERT_NE(created, GEO::NO_INDEX);
    for (const erhe::geometry::Edit_corner& corner : edit_mesh.get_facet_corners(created)) {
        EXPECT_FALSE(corner.sources.empty());
    }
    emit_and_check(operation, destination, Counts{8, 12, 6});
}

TEST(Edit_mesh, SelectionRemapThroughEmission)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Test_edit_operation operation{*grid, destination};
    Edit_mesh& edit_mesh = operation.get_edit_mesh();
    const GEO::index_t selected_facet = grid_facet(1, 1);
    edit_mesh.split_edge(edge_of(edit_mesh, grid_vertex(1, 1), grid_vertex(2, 1)), 0.5f);
    const GEO::index_t second = edit_mesh.split_facet(selected_facet, 0, 3);
    ASSERT_NE(second, GEO::NO_INDEX);
    const std::array<GEO::index_t, 1> removed{grid_facet(3, 3)};
    edit_mesh.delete_elements(removed, Delete_context::only_faces);
    // The deleted facet's two boundary edges stay as wire edges in the
    // scratch and have no destination edge; its corner vertex stays loose.
    emit_and_check(operation, destination, Counts{26, 40, 16});

    Geometry_component_selection source_selection;
    source_selection.facets.insert(selected_facet);
    source_selection.vertices.insert(grid_vertex(0, 0));
    source_selection.vertices.insert(grid_vertex(4, 4));
    Geometry_component_selection destination_selection;
    operation.remap_component_selection(source_selection, destination_selection);
    EXPECT_EQ(destination_selection.facets.size(), 2u);
    // Facet (3, 3) was deleted: every scratch facet after it moved down by one.
    EXPECT_TRUE(destination_selection.facets.contains(selected_facet));
    EXPECT_TRUE(destination_selection.facets.contains(second - 1));
    EXPECT_EQ(destination_selection.vertices.size(), 2u);
    EXPECT_TRUE(destination_selection.vertices.contains(grid_vertex(0, 0)));
    EXPECT_TRUE(destination_selection.vertices.contains(grid_vertex(4, 4)));
}
