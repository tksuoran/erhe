// Phase 1 of doc/subdivision_crease_edges.md: the per-edge "edge_sharpness"
// attribute channel. Geogram's edges.clear() keeps attribute bindings but
// wipes values, so Geometry::build_edges() snapshots present sharpness values
// by canonical vertex pair and reapplies them after the rebuild; repeated
// process() runs must not lose or corrupt crease data.

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <memory>

using erhe::geometry::Geometry;

namespace {

constexpr uint64_t process_flags =
    Geometry::process_flag_connect |
    Geometry::process_flag_build_edges |
    Geometry::process_flag_compute_facet_centroids |
    Geometry::process_flag_compute_smooth_vertex_normals |
    Geometry::process_flag_generate_facet_texture_coordinates;

auto make_processed_cube() -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> geometry = std::make_unique<Geometry>("cube");
    erhe::geometry::shapes::make_cube(geometry->get_mesh(), 1.0f);
    geometry->process({.flags = process_flags});
    return geometry;
}

} // anonymous namespace

TEST(Edge_sharpness, accessors_roundtrip_and_default_zero)
{
    std::unique_ptr<Geometry> geometry = make_processed_cube();
    const GEO::Mesh& mesh = geometry->get_mesh();
    ASSERT_GT(mesh.edges.nb(), 0u);

    const GEO::index_t v0 = mesh.edges.vertex(0, 0);
    const GEO::index_t v1 = mesh.edges.vertex(0, 1);

    EXPECT_EQ(geometry->get_edge_sharpness(v0, v1), 0.0f);
    geometry->set_edge_sharpness(v0, v1, 2.5f);
    EXPECT_EQ(geometry->get_edge_sharpness(v0, v1), 2.5f);
    // Vertex order must not matter (canonical pair).
    EXPECT_EQ(geometry->get_edge_sharpness(v1, v0), 2.5f);

    // Nonexistent edge: get returns 0, set is a no-op (no crash).
    const GEO::index_t nonexistent_a = 0;
    GEO::index_t nonexistent_b = GEO::NO_VERTEX;
    for (GEO::index_t candidate = 1; candidate < mesh.vertices.nb(); ++candidate) {
        if (geometry->get_edge(nonexistent_a, candidate) == GEO::NO_EDGE) {
            nonexistent_b = candidate;
            break;
        }
    }
    ASSERT_NE(nonexistent_b, GEO::NO_VERTEX); // cube: opposite corner is not connected
    geometry->set_edge_sharpness(nonexistent_a, nonexistent_b, 9.0f);
    EXPECT_EQ(geometry->get_edge_sharpness(nonexistent_a, nonexistent_b), 0.0f);
}

TEST(Edge_sharpness, survives_build_edges_rebuild)
{
    std::unique_ptr<Geometry> geometry = make_processed_cube();
    const GEO::Mesh& mesh = geometry->get_mesh();

    // Tag every third edge with a distinct value, including an infinite one.
    std::vector<std::tuple<GEO::index_t, GEO::index_t, float>> expected;
    for (GEO::index_t edge = 0; edge < mesh.edges.nb(); edge += 3) {
        const GEO::index_t v0 = mesh.edges.vertex(edge, 0);
        const GEO::index_t v1 = mesh.edges.vertex(edge, 1);
        const float sharpness = (edge == 0) ? std::numeric_limits<float>::infinity() : (0.5f + static_cast<float>(edge));
        geometry->set_edge_sharpness(v0, v1, sharpness);
        expected.emplace_back(v0, v1, sharpness);
    }
    ASSERT_FALSE(expected.empty());

    // Two full process() runs, each of which clears and rebuilds the edge store.
    geometry->process({.flags = process_flags});
    geometry->process({.flags = process_flags});

    for (const std::tuple<GEO::index_t, GEO::index_t, float>& entry : expected) {
        EXPECT_EQ(geometry->get_edge_sharpness(std::get<0>(entry), std::get<1>(entry)), std::get<2>(entry));
    }

    // Untagged edges stay smooth (absent, reported as 0).
    const erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
    for (GEO::index_t edge = 0; edge < geometry->get_mesh().edges.nb(); ++edge) {
        if ((edge % 3) != 0) {
            EXPECT_FALSE(attributes.edge_sharpness.has(edge));
        }
    }
}
