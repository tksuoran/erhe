#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/operation/knife_cut.hpp"
#include "erhe_geometry/shapes/box.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/mesh/mesh.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::operation::Component_remap;
using erhe::geometry::operation::Geometry_component_selection;
using erhe::geometry::operation::Knife_cut;
using erhe::geometry::operation::Knife_options;
using erhe::geometry::operation::Knife_point;
using erhe::geometry::operation::Knife_result;
using erhe::geometry::operation::Knife_snap;
using erhe::geometry::operation::Knife_view;

namespace {

constexpr uint64_t edge_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges;

constexpr float epsilon = 1e-5f;

// Open grid of 4 x 4 unit quads in the XY plane (z = 0), counter-clockwise
// seen from +Z. Vertex (x, y) is y * 5 + x, facet (x, y) is y * 4 + x. Every
// corner carries corner_texcoord_0 = position.xy / 4.
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

// An L-shaped hexagon (the notch is the square (1, 1) - (2, 2)), one facet.
auto make_l_hexagon() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("l_hexagon");
    GEO::Mesh& mesh = geo->get_mesh();
    mesh.vertices.set_single_precision();
    const std::array<GEO::vec3f, 6> positions{
        GEO::vec3f{0.0f, 0.0f, 0.0f},
        GEO::vec3f{2.0f, 0.0f, 0.0f},
        GEO::vec3f{2.0f, 1.0f, 0.0f},
        GEO::vec3f{1.0f, 1.0f, 0.0f},
        GEO::vec3f{1.0f, 2.0f, 0.0f},
        GEO::vec3f{0.0f, 2.0f, 0.0f}
    };
    mesh.vertices.create_vertices(6);
    for (GEO::index_t vertex = 0; vertex < 6; ++vertex) {
        erhe::geometry::set_pointf(mesh.vertices, vertex, positions[vertex]);
    }
    const GEO::index_t facet = mesh.facets.create_polygon(6);
    for (GEO::index_t corner = 0; corner < 6; ++corner) {
        mesh.facets.set_vertex(facet, corner, corner);
    }
    geo->process({.flags = edge_flags});
    return geo;
}

// A 2 x 2 x 2 cube centred at the origin.
auto make_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geo = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_box(geo->get_mesh(), 2.0f, 2.0f, 2.0f);
    geo->process({.flags = edge_flags});
    return geo;
}

auto position(const Geometry& geometry, const GEO::index_t vertex) -> GEO::vec3f
{
    return erhe::geometry::get_pointf(geometry.get_mesh().vertices, vertex);
}

auto is_near(const GEO::vec3f& a, const GEO::vec3f& b) -> bool
{
    return GEO::length(a - b) < epsilon;
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

// Orthographic, straight down the -Z axis; x and y in [-1, 5] fill a
// 1000 x 1000 px viewport.
auto make_top_view() -> Knife_view
{
    Knife_view view;
    view.clip_from_mesh.load_zero();
    view.clip_from_mesh(0, 0) =  1.0 / 3.0;
    view.clip_from_mesh(0, 3) = -2.0 / 3.0;
    view.clip_from_mesh(1, 1) =  1.0 / 3.0;
    view.clip_from_mesh(1, 3) = -2.0 / 3.0;
    view.clip_from_mesh(2, 2) = -0.1;
    view.clip_from_mesh(3, 3) =  1.0;
    view.viewport_width         = 1000.0f;
    view.viewport_height        = 1000.0f;
    view.eye_in_mesh            = GEO::vec3f{2.0f, 2.0f, 10.0f};
    view.view_direction_in_mesh = GEO::vec3f{0.0f, 0.0f, -1.0f};
    view.perspective            = false;
    return view;
}

// Perspective from (0, 0, 5) looking down -Z, 90 degree field of view.
auto make_front_perspective_view() -> Knife_view
{
    Knife_view view;
    view.clip_from_mesh.load_zero();
    view.clip_from_mesh(0, 0) =  1.0;
    view.clip_from_mesh(1, 1) =  1.0;
    view.clip_from_mesh(2, 2) = -1.0;
    view.clip_from_mesh(3, 2) = -1.0;
    view.clip_from_mesh(3, 3) =  5.0;
    view.viewport_width         = 1000.0f;
    view.viewport_height        = 1000.0f;
    view.eye_in_mesh            = GEO::vec3f{0.0f, 0.0f, 5.0f};
    view.view_direction_in_mesh = GEO::vec3f{0.0f, 0.0f, -1.0f};
    view.perspective            = true;
    return view;
}

auto vertex_point(const GEO::index_t vertex, const GEO::vec3f& p) -> Knife_point
{
    return Knife_point{.position = p, .snap = Knife_snap::vertex, .vertex = vertex};
}

auto edge_point(const GEO::index_t v0, const GEO::index_t v1, const GEO::vec3f& p) -> Knife_point
{
    return Knife_point{.position = p, .snap = Knife_snap::edge, .edge_v0 = v0, .edge_v1 = v1};
}

auto facet_point(const GEO::index_t facet, const GEO::vec3f& p) -> Knife_point
{
    return Knife_point{.position = p, .snap = Knife_snap::facet, .facet = facet};
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

auto count_facets_with_corners(const Geometry& geometry, const GEO::index_t corner_count) -> int
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    int count = 0;
    for (GEO::index_t facet = 0; facet < mesh.facets.nb(); ++facet) {
        if (mesh.facets.nb_corners(facet) == corner_count) {
            ++count;
        }
    }
    return count;
}

// The quad (1, 1) of the grid cut from the middle of its bottom edge to the
// middle of its top edge.
auto grid_quad_cut_points() -> std::array<Knife_point, 2>
{
    return std::array<Knife_point, 2>{
        edge_point(grid_vertex(1, 1), grid_vertex(2, 1), GEO::vec3f{1.5f, 1.0f, 0.0f}),
        edge_point(grid_vertex(2, 2), grid_vertex(1, 2), GEO::vec3f{1.5f, 2.0f, 0.0f})
    };
}

} // anonymous namespace

TEST(KnifeCut, OneQuadEdgeToEdge)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Knife_result result;
    const std::array<Knife_point, 2> points = grid_quad_cut_points();
    erhe::geometry::operation::knife_cut(*grid, destination, make_top_view(), points, Knife_options{}, &result);
    expect_counts(destination, Counts{27, 43, 17});
    EXPECT_EQ(result.cut_vertices.size(), 2u);
    ASSERT_EQ(result.cut_edges.size(), 1u);
    EXPECT_EQ(count_facets_with_corners(destination, 4), 15); // the two pieces and the untouched quads
    EXPECT_EQ(count_facets_with_corners(destination, 5), 2);  // the neighbours carry the cut vertices

    // The cut vertices' corners interpolate the texcoords along their edges.
    const GEO::Mesh& mesh = destination.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = destination.get_attributes();
    for (const GEO::index_t vertex : result.cut_vertices) {
        const GEO::vec3f p = position(destination, vertex);
        EXPECT_TRUE(is_near(p, GEO::vec3f{1.5f, 1.0f, 0.0f}) || is_near(p, GEO::vec3f{1.5f, 2.0f, 0.0f}));
        int corner_count = 0;
        for (GEO::index_t corner = 0; corner < mesh.facet_corners.nb(); ++corner) {
            if (mesh.facet_corners.vertex(corner) != vertex) {
                continue;
            }
            ++corner_count;
            const std::optional<GEO::vec2f> texcoord = attributes.corner_texcoord_0.try_get(corner);
            ASSERT_TRUE(texcoord.has_value());
            EXPECT_NEAR(texcoord.value().x, p.x / 4.0f, 1e-6f);
            EXPECT_NEAR(texcoord.value().y, p.y / 4.0f, 1e-6f);
        }
        EXPECT_EQ(corner_count, 3); // two pieces of the cut quad and the neighbour quad
    }
}

TEST(KnifeCut, ThreeQuadsInARow)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Knife_result result;
    const std::array<Knife_point, 2> points{
        edge_point(grid_vertex(1, 1), grid_vertex(1, 2), GEO::vec3f{1.0f, 1.5f, 0.0f}),
        edge_point(grid_vertex(4, 1), grid_vertex(4, 2), GEO::vec3f{4.0f, 1.5f, 0.0f})
    };
    erhe::geometry::operation::knife_cut(*grid, destination, make_top_view(), points, Knife_options{}, &result);
    expect_counts(destination, Counts{29, 47, 19});
    EXPECT_EQ(result.cut_vertices.size(), 4u);
    EXPECT_EQ(result.cut_edges.size(), 3u);
}

TEST(KnifeCut, VertexToVertexDiagonal)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Knife_result result;
    const std::array<Knife_point, 2> points{
        vertex_point(grid_vertex(1, 1), GEO::vec3f{1.0f, 1.0f, 0.0f}),
        vertex_point(grid_vertex(2, 2), GEO::vec3f{2.0f, 2.0f, 0.0f})
    };
    erhe::geometry::operation::knife_cut(*grid, destination, make_top_view(), points, Knife_options{}, &result);
    expect_counts(destination, Counts{25, 41, 17});
    EXPECT_TRUE(result.cut_vertices.empty());
    ASSERT_EQ(result.cut_edges.size(), 1u);
    EXPECT_EQ(count_facets_with_corners(destination, 3), 2);
}

// Two facet points in one facet and no island: the cut edge is dangling,
// finish() drops it and deletes its vertices, and the facet is unchanged.
TEST(KnifeCut, InteriorToInteriorIsDropped)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Knife_cut knife{*grid, destination, make_top_view(), Knife_options{}};
    knife.add_point(facet_point(grid_facet(1, 1), GEO::vec3f{1.3f, 1.5f, 0.0f}));
    knife.add_point(facet_point(grid_facet(1, 1), GEO::vec3f{1.7f, 1.5f, 0.0f}));
    std::vector<std::pair<GEO::vec3f, GEO::vec3f>> segments;
    knife.get_preview_segments(segments);
    EXPECT_EQ(segments.size(), 1u);
    Knife_result result;
    knife.finish(&result, nullptr);
    expect_counts(destination, Counts{25, 40, 16});
    EXPECT_TRUE(result.cut_vertices.empty());
    EXPECT_TRUE(result.cut_edges.empty());
}

TEST(KnifeCut, ClosedSquareIsland)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Knife_result result;
    const std::array<Knife_point, 4> points{
        facet_point(grid_facet(1, 1), GEO::vec3f{1.3f, 1.3f, 0.0f}),
        facet_point(grid_facet(1, 1), GEO::vec3f{1.7f, 1.3f, 0.0f}),
        facet_point(grid_facet(1, 1), GEO::vec3f{1.7f, 1.7f, 0.0f}),
        facet_point(grid_facet(1, 1), GEO::vec3f{1.3f, 1.7f, 0.0f})
    };
    erhe::geometry::operation::knife_cut(*grid, destination, make_top_view(), points, Knife_options{.close_polyline = true}, &result);
    // The island facet plus two ring facets (two connecting edges).
    expect_counts(destination, Counts{29, 46, 18});
    EXPECT_EQ(result.cut_vertices.size(), 4u);
    EXPECT_EQ(result.cut_edges.size(), 4u);
}

// The cut line enters the L, leaves it into the notch and re-enters: the
// chord across the notch is rejected by the midpoint rule.
TEST(KnifeCut, ConcaveFacetMidpointRule)
{
    const std::unique_ptr<Geometry> hexagon = make_l_hexagon();
    Geometry destination{"result"};
    Knife_cut knife{*hexagon, destination, make_top_view(), Knife_options{}};
    knife.add_point(edge_point(4, 5, GEO::vec3f{0.5f, 2.0f, 0.0f}));
    knife.add_point(edge_point(1, 2, GEO::vec3f{2.0f, 0.5f, 0.0f}));
    std::vector<std::pair<GEO::vec3f, GEO::vec3f>> segments;
    knife.get_preview_segments(segments);
    EXPECT_EQ(segments.size(), 2u);
    Knife_result result;
    knife.finish(&result, nullptr);
    // Two corner triangles cut off, the rest an 8-gon.
    expect_counts(destination, Counts{10, 12, 3});
    EXPECT_EQ(result.cut_vertices.size(), 4u);
    EXPECT_EQ(result.cut_edges.size(), 2u);
    EXPECT_EQ(count_facets_with_corners(destination, 3), 2);
    EXPECT_EQ(count_facets_with_corners(destination, 8), 1);
}

TEST(KnifeCut, CubeFrontOnlyWithoutCutThrough)
{
    const std::unique_ptr<Geometry> cube = make_cube();
    const GEO::index_t top_left     = find_vertex(*cube, GEO::vec3f{-1.0f,  1.0f, 1.0f});
    const GEO::index_t top_right    = find_vertex(*cube, GEO::vec3f{ 1.0f,  1.0f, 1.0f});
    const GEO::index_t bottom_left  = find_vertex(*cube, GEO::vec3f{-1.0f, -1.0f, 1.0f});
    const GEO::index_t bottom_right = find_vertex(*cube, GEO::vec3f{ 1.0f, -1.0f, 1.0f});
    ASSERT_NE(top_left, GEO::NO_INDEX);
    ASSERT_NE(bottom_right, GEO::NO_INDEX);
    const std::array<Knife_point, 2> points{
        edge_point(top_left,    top_right,    GEO::vec3f{0.0f,  1.0f, 1.0f}),
        edge_point(bottom_left, bottom_right, GEO::vec3f{0.0f, -1.0f, 1.0f})
    };
    {
        Geometry destination{"front_only"};
        Knife_result result;
        erhe::geometry::operation::knife_cut(*cube, destination, make_front_perspective_view(), points, Knife_options{.cut_through = false}, &result);
        expect_counts(destination, Counts{10, 15, 7});
        EXPECT_EQ(result.cut_vertices.size(), 2u);
        ASSERT_EQ(result.cut_edges.size(), 1u);
        for (const GEO::index_t vertex : result.cut_vertices) {
            EXPECT_NEAR(position(destination, vertex).z, 1.0f, epsilon);
        }
    }
    {
        // Cut through: the ring front, top, back, bottom.
        Geometry destination{"cut_through"};
        Knife_result result;
        erhe::geometry::operation::knife_cut(*cube, destination, make_front_perspective_view(), points, Knife_options{.cut_through = true}, &result);
        expect_counts(destination, Counts{12, 20, 10});
        EXPECT_EQ(result.cut_vertices.size(), 4u);
        EXPECT_EQ(result.cut_edges.size(), 4u);
        int back_vertices = 0;
        for (const GEO::index_t vertex : result.cut_vertices) {
            if (std::abs(position(destination, vertex).z + 1.0f) < epsilon) {
                ++back_vertices;
            }
        }
        EXPECT_EQ(back_vertices, 2);
    }
}

TEST(KnifeCut, UndoLastPoint)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Knife_cut knife{*grid, destination, make_top_view(), Knife_options{}};
    const std::array<Knife_point, 2> points = grid_quad_cut_points();
    knife.add_point(points[0]);
    knife.add_point(points[1]);
    knife.add_point(edge_point(grid_vertex(1, 3), grid_vertex(2, 3), GEO::vec3f{1.5f, 3.0f, 0.0f}));
    std::vector<std::pair<GEO::vec3f, GEO::vec3f>> segments;
    knife.get_preview_segments(segments);
    EXPECT_EQ(segments.size(), 2u);
    knife.undo_last_point();
    EXPECT_EQ(knife.get_point_count(), 2u);
    knife.get_preview_segments(segments);
    EXPECT_EQ(segments.size(), 1u);
    Knife_result result;
    knife.finish(&result, nullptr);
    expect_counts(destination, Counts{27, 43, 17});
    EXPECT_EQ(result.cut_edges.size(), 1u);
}

// Two diagonals of one quad: the second crosses the first, which splits both
// at a new vertex; the side between the diagonals (vertices joined by an
// edge) is no cut.
TEST(KnifeCut, CrossingCutsSplitEachOther)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Knife_result result;
    const std::array<Knife_point, 4> points{
        vertex_point(grid_vertex(1, 1), GEO::vec3f{1.0f, 1.0f, 0.0f}),
        vertex_point(grid_vertex(2, 2), GEO::vec3f{2.0f, 2.0f, 0.0f}),
        vertex_point(grid_vertex(2, 1), GEO::vec3f{2.0f, 1.0f, 0.0f}),
        vertex_point(grid_vertex(1, 2), GEO::vec3f{1.0f, 2.0f, 0.0f})
    };
    erhe::geometry::operation::knife_cut(*grid, destination, make_top_view(), points, Knife_options{}, &result);
    expect_counts(destination, Counts{26, 44, 19});
    ASSERT_EQ(result.cut_vertices.size(), 1u);
    EXPECT_TRUE(is_near(position(destination, result.cut_vertices.front()), GEO::vec3f{1.5f, 1.5f, 0.0f}));
    EXPECT_EQ(result.cut_edges.size(), 4u);
    EXPECT_EQ(count_facets_with_corners(destination, 3), 4);
}

TEST(KnifeCut, RemapCarriesFacetAndEdge)
{
    const std::unique_ptr<Geometry> grid = make_grid();
    Geometry destination{"result"};
    Geometry_component_selection source_selection;
    source_selection.facets.insert(grid_facet(1, 1));
    source_selection.edges.insert(std::make_pair(grid_vertex(1, 1), grid_vertex(2, 1)));
    Geometry_component_selection destination_selection;
    Component_remap remap{.source = &source_selection, .destination = &destination_selection};
    const std::array<Knife_point, 2> points = grid_quad_cut_points();
    erhe::geometry::operation::knife_cut(*grid, destination, make_top_view(), points, Knife_options{}, nullptr, &remap);
    expect_counts(destination, Counts{27, 43, 17});

    // The selected facet maps to its two pieces, both inside the quad.
    ASSERT_EQ(destination_selection.facets.size(), 2u);
    const GEO::Mesh& mesh = destination.get_mesh();
    for (const GEO::index_t facet : destination_selection.facets) {
        for (GEO::index_t local_corner = 0; local_corner < mesh.facets.nb_corners(facet); ++local_corner) {
            const GEO::vec3f p = position(destination, mesh.facets.vertex(facet, local_corner));
            EXPECT_GE(p.x, 1.0f - epsilon);
            EXPECT_LE(p.x, 2.0f + epsilon);
            EXPECT_GE(p.y, 1.0f - epsilon);
            EXPECT_LE(p.y, 2.0f + epsilon);
        }
    }
    // The selected edge maps to its two halves.
    EXPECT_EQ(destination_selection.edges.size(), 2u);
}
