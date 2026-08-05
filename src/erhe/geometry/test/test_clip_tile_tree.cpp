#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/clip_tile_tree.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <geogram/basic/geometry.h>

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using erhe::geometry::get_pointf;
using erhe::geometry::operation::Clip_tree_node;
using erhe::geometry::operation::Clip_tile_piece;
using erhe::geometry::operation::clip_by_tile_tree;

namespace {

auto make_cube() -> std::unique_ptr<erhe::geometry::Geometry>
{
    std::unique_ptr<erhe::geometry::Geometry> geo = std::make_unique<erhe::geometry::Geometry>("cube");
    erhe::geometry::shapes::make_cube(geo->get_mesh(), 1.0f);
    const uint64_t flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_facet_centroids |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates;
    geo->process({.flags = flags});
    return geo;
}

[[nodiscard]] auto surface_area(const erhe::geometry::Geometry& geometry) -> double
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    double area = 0.0;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        const GEO::vec3f p0 = get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, 0)));
        for (GEO::index_t k = 2; k < corner_count; ++k) {
            const GEO::vec3f p1 = get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, k - 1)));
            const GEO::vec3f p2 = get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, k)));
            area += 0.5 * static_cast<double>(GEO::length(GEO::cross(p1 - p0, p2 - p0)));
        }
    }
    return area;
}

template <typename T>
[[nodiscard]] auto bits_equal(const T& lhs, const T& rhs) -> bool
{
    return std::memcmp(&lhs, &rhs, sizeof(T)) == 0;
}

class Boundary_vertex
{
public:
    GEO::vec3f position;
    bool       has_normal{false};
    GEO::vec3f normal_smooth{0.0f, 0.0f, 0.0f};
};

[[nodiscard]] auto collect_boundary_vertices(const erhe::geometry::Geometry& geometry, const int axis, const float value, const float tolerance)
    -> std::vector<Boundary_vertex>
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const auto&      attributes = geometry.get_attributes();
    std::vector<Boundary_vertex> result;
    for (GEO::index_t vertex : mesh.vertices) {
        const GEO::vec3f position = get_pointf(mesh.vertices, vertex);
        if (std::abs(position[axis] - value) > tolerance) {
            continue;
        }
        Boundary_vertex entry;
        entry.position = position;
        if (attributes.vertex_normal_smooth.has(vertex)) {
            entry.has_normal    = true;
            entry.normal_smooth = attributes.vertex_normal_smooth.get(vertex);
        }
        result.push_back(entry);
    }
    return result;
}

// Every boundary vertex of lhs must have a counterpart in rhs whose position
// AND interpolated attributes are bitwise identical.
void expect_boundary_bitwise_shared(const std::vector<Boundary_vertex>& lhs, const std::vector<Boundary_vertex>& rhs)
{
    ASSERT_FALSE(lhs.empty());
    ASSERT_FALSE(rhs.empty());
    for (const Boundary_vertex& l : lhs) {
        bool found = false;
        for (const Boundary_vertex& r : rhs) {
            if (!bits_equal(l.position, r.position)) {
                continue;
            }
            found = true;
            EXPECT_EQ(l.has_normal, r.has_normal);
            if (l.has_normal && r.has_normal) {
                EXPECT_TRUE(bits_equal(l.normal_smooth, r.normal_smooth))
                    << "normal mismatch at boundary vertex (" << l.position.x << ", " << l.position.y << ", " << l.position.z << ")";
            }
            break;
        }
        EXPECT_TRUE(found)
            << "no bitwise position match for boundary vertex (" << l.position.x << ", " << l.position.y << ", " << l.position.z << ")";
    }
}

} // anonymous namespace

TEST(ClipTileTree, SingleLeaf_WholeMesh)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_cube();
    const std::vector<Clip_tree_node> tree{
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 3}
    };
    std::vector<Clip_tile_piece> pieces;
    clip_by_tile_tree(*cube, tree, -1, pieces);

    ASSERT_EQ(pieces.size(), 1u);
    EXPECT_EQ(pieces[0].tile, 3);
    // Cube quads are fan-triangulated: 6 quads -> 12 triangles.
    EXPECT_EQ(pieces[0].geometry->get_mesh().facets.nb(), 12u);
    EXPECT_NEAR(surface_area(*pieces[0].geometry), surface_area(*cube), 1.0e-3);
}

TEST(ClipTileTree, TwoTiles_AreaConserved_BoundaryBitwiseExact)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_cube();
    // Split plane x = 0.25 (avoids source vertices; cube spans -1..1).
    const std::vector<Clip_tree_node> tree{
        Clip_tree_node{.axis = 0, .value = 0.25f, .child = {1, 2}, .tile = -1},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 0},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 1}
    };
    std::vector<Clip_tile_piece> pieces;
    clip_by_tile_tree(*cube, tree, -1, pieces);

    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].tile, 0);
    EXPECT_EQ(pieces[1].tile, 1);

    const double area_sum = surface_area(*pieces[0].geometry) + surface_area(*pieces[1].geometry);
    EXPECT_NEAR(area_sum, surface_area(*cube), 1.0e-3);

    const std::vector<Boundary_vertex> boundary_0 = collect_boundary_vertices(*pieces[0].geometry, 0, 0.25f, 1.0e-3f);
    const std::vector<Boundary_vertex> boundary_1 = collect_boundary_vertices(*pieces[1].geometry, 0, 0.25f, 1.0e-3f);
    expect_boundary_bitwise_shared(boundary_0, boundary_1);
    expect_boundary_bitwise_shared(boundary_1, boundary_0);
}

TEST(ClipTileTree, FourTiles_CornerBitwiseExact)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_cube();
    // x = 0 then z = 0 on both sides: four quadrant tiles.
    const std::vector<Clip_tree_node> tree{
        Clip_tree_node{.axis = 0,  .value = 0.0f, .child = {1, 2},   .tile = -1},
        Clip_tree_node{.axis = 2,  .value = 0.0f, .child = {3, 4},   .tile = -1},
        Clip_tree_node{.axis = 2,  .value = 0.0f, .child = {5, 6},   .tile = -1},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 0},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 1},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 2},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 3}
    };
    std::vector<Clip_tile_piece> pieces;
    clip_by_tile_tree(*cube, tree, -1, pieces);

    ASSERT_EQ(pieces.size(), 4u);
    double area_sum = 0.0;
    for (const Clip_tile_piece& piece : pieces) {
        area_sum += surface_area(*piece.geometry);
    }
    EXPECT_NEAR(area_sum, surface_area(*cube), 1.0e-3);

    // The 4-tile corner line (x = 0, z = 0) crosses the cube's top and bottom
    // faces: every piece must contain corner vertices there, and every such
    // vertex must be bitwise shared with each neighboring piece.
    std::vector<std::vector<Boundary_vertex>> corner_vertices;
    for (const Clip_tile_piece& piece : pieces) {
        std::vector<Boundary_vertex> on_x = collect_boundary_vertices(*piece.geometry, 0, 0.0f, 1.0e-3f);
        std::vector<Boundary_vertex> on_corner;
        for (const Boundary_vertex& vertex : on_x) {
            if (std::abs(vertex.position.z) <= 1.0e-3f) {
                on_corner.push_back(vertex);
            }
        }
        ASSERT_FALSE(on_corner.empty()) << "piece for tile " << piece.tile << " has no corner-line vertex";
        corner_vertices.push_back(std::move(on_corner));
    }
    for (std::size_t i = 0; i < corner_vertices.size(); ++i) {
        for (std::size_t j = i + 1; j < corner_vertices.size(); ++j) {
            expect_boundary_bitwise_shared(corner_vertices[i], corner_vertices[j]);
        }
    }
}

TEST(ClipTileTree, OverflowSplit_RoutesToAssignedTile)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_cube();
    const std::vector<Clip_tree_node> tree{
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {1, 2},   .tile = -1},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 5},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 7}
    };
    std::vector<Clip_tile_piece> pieces;
    clip_by_tile_tree(*cube, tree, 7, pieces);

    ASSERT_EQ(pieces.size(), 1u);
    EXPECT_EQ(pieces[0].tile, 7);
    EXPECT_NEAR(surface_area(*pieces[0].geometry), surface_area(*cube), 1.0e-3);
}

TEST(ClipTileTree, LightmapUv_Interpolated_BitwiseExact)
{
    std::unique_ptr<erhe::geometry::Geometry> cube = make_cube();
    // Author a synthetic channel-2 corner UV so the clip has a lightmap
    // channel to interpolate (planar projection of world XZ).
    {
        erhe::geometry::Geometry& geometry = *cube;
        const GEO::Mesh& mesh = geometry.get_mesh();
        auto& texcoord_2 = geometry.get_attributes().corner_texcoord_2;
        for (GEO::index_t facet : mesh.facets) {
            for (GEO::index_t corner : mesh.facets.corners(facet)) {
                const GEO::vec3f p = get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
                texcoord_2.set(corner, GEO::vec2f{p.x * 0.5f + 0.5f, p.z * 0.5f + 0.5f});
            }
        }
    }

    const std::vector<Clip_tree_node> tree{
        Clip_tree_node{.axis = 0, .value = 0.25f, .child = {1, 2}, .tile = -1},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 0},
        Clip_tree_node{.axis = -1, .value = 0.0f, .child = {-1, -1}, .tile = 1}
    };
    std::vector<Clip_tile_piece> pieces;
    clip_by_tile_tree(*cube, tree, -1, pieces);
    ASSERT_EQ(pieces.size(), 2u);

    // Collect (position bits, uv bits) pairs for corners at the cut plane;
    // every pair present on one side must be present bitwise on the other
    // (each crossing facet leaves fragments on both sides sharing the cut).
    const auto collect_corner_pairs = [](const erhe::geometry::Geometry& geometry) {
        const GEO::Mesh& mesh = geometry.get_mesh();
        const auto& texcoord_2 = geometry.get_attributes().corner_texcoord_2;
        std::vector<std::pair<GEO::vec3f, GEO::vec2f>> pairs;
        for (GEO::index_t facet : mesh.facets) {
            for (GEO::index_t corner : mesh.facets.corners(facet)) {
                const GEO::vec3f p = get_pointf(mesh.vertices, mesh.facet_corners.vertex(corner));
                if (std::abs(p.x - 0.25f) > 1.0e-3f) {
                    continue;
                }
                if (texcoord_2.has(corner)) {
                    pairs.emplace_back(p, texcoord_2.get(corner));
                }
            }
        }
        return pairs;
    };
    const auto pairs_0 = collect_corner_pairs(*pieces[0].geometry);
    const auto pairs_1 = collect_corner_pairs(*pieces[1].geometry);
    ASSERT_FALSE(pairs_0.empty());
    ASSERT_FALSE(pairs_1.empty());
    for (const auto& [position, uv] : pairs_0) {
        bool found = false;
        for (const auto& [other_position, other_uv] : pairs_1) {
            if (bits_equal(position, other_position) && bits_equal(uv, other_uv)) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found)
            << "no bitwise (position, uv2) match for cut corner at (" << position.x << ", " << position.y << ", " << position.z << ")";
    }
}
