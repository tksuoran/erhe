// Phase 2 of doc/subdivision_crease_edges.md: semi-sharp crease rules in
// Catmull-Clark subdivision (DeRose/Kass/Truong 1998; rule selection and
// fractional blending per OpenSubdiv Sdc semantics).
//
// Setup used throughout: a cube of radius 1 (vertices at +-0.5) with the top
// (y = +0.5) edge loop tagged: vertices {2, 4, 6, 7}, edges (2,6) (6,7) (4,7)
// (2,4). Each top corner then has exactly 2 incident sharp edges (crease
// rule), each bottom vertex none (smooth rule).
//
// Internal-but-stable destination vertex layout relied on by the tests:
// the initial-points phase maps source vertex v to destination vertex v
// (cube: 0..7), and edge midpoints follow in source edge order, so the
// midpoint of source edge e is destination vertex 8 + e.

#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/post_processing.hpp"
#include "erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp"
#include "erhe_geometry/shapes/regular_polyhedron.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using erhe::geometry::Geometry;
using erhe::geometry::get_pointf;
using erhe::geometry::operation::Post_processing;
using erhe::geometry::operation::catmull_clark_subdivision;

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

// Source edge indices of the top (y = +0.5) loop.
auto top_loop_edges(const Geometry& geometry) -> std::vector<GEO::index_t>
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    std::vector<GEO::index_t> result;
    for (GEO::index_t edge = 0; edge < mesh.edges.nb(); ++edge) {
        const GEO::vec3f a = get_pointf(mesh.vertices, mesh.edges.vertex(edge, 0));
        const GEO::vec3f b = get_pointf(mesh.vertices, mesh.edges.vertex(edge, 1));
        if ((a.y == 0.5f) && (b.y == 0.5f)) {
            result.push_back(edge);
        }
    }
    return result;
}

void tag_top_loop(Geometry& geometry, const float sharpness)
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (GEO::index_t edge : top_loop_edges(geometry)) {
        geometry.set_edge_sharpness(mesh.edges.vertex(edge, 0), mesh.edges.vertex(edge, 1), sharpness);
    }
}

auto subdivide(const Geometry& source, const Post_processing post_processing_level = Post_processing::full_default) -> std::unique_ptr<Geometry>
{
    std::unique_ptr<Geometry> destination = std::make_unique<Geometry>("subdivided");
    catmull_clark_subdivision(source, *destination, nullptr, nullptr, post_processing_level);
    return destination;
}

void expect_near(const GEO::vec3f& actual, const GEO::vec3f& expected, const float tolerance = 1e-6f)
{
    EXPECT_NEAR(actual.x, expected.x, tolerance);
    EXPECT_NEAR(actual.y, expected.y, tolerance);
    EXPECT_NEAR(actual.z, expected.z, tolerance);
}

// Destination edges carrying a present sharpness value, as (value) list.
auto collect_destination_sharpness(const Geometry& geometry) -> std::vector<float>
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    const erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    std::vector<float> result;
    for (GEO::index_t edge = 0; edge < mesh.edges.nb(); ++edge) {
        const std::optional<float> sharpness = attributes.edge_sharpness.try_get(edge);
        if (sharpness.has_value()) {
            result.push_back(sharpness.value());
        }
    }
    return result;
}

} // anonymous namespace

// Present-but-zero sharpness must take the exact same code path as an absent
// attribute: bit-identical positions.
TEST(Catmull_clark_crease, zero_values_match_absent)
{
    std::unique_ptr<Geometry> plain  = make_processed_cube();
    std::unique_ptr<Geometry> tagged = make_processed_cube();
    tag_top_loop(*tagged, 0.0f);

    std::unique_ptr<Geometry> plain_result  = subdivide(*plain);
    std::unique_ptr<Geometry> tagged_result = subdivide(*tagged);

    ASSERT_EQ(plain_result->get_mesh().vertices.nb(), tagged_result->get_mesh().vertices.nb());
    for (GEO::index_t vertex : plain_result->get_mesh().vertices) {
        const GEO::vec3f a = get_pointf(plain_result ->get_mesh().vertices, vertex);
        const GEO::vec3f b = get_pointf(tagged_result->get_mesh().vertices, vertex);
        EXPECT_EQ(a.x, b.x);
        EXPECT_EQ(a.y, b.y);
        EXPECT_EQ(a.z, b.z);
    }
    EXPECT_TRUE(collect_destination_sharpness(*tagged_result).empty());
}

// Sharpness >= 1: edge points are plain midpoints (paper eq. 8) and crease
// vertices (2 sharp edges) follow eq. 9: (e_j + 6 v + e_k) / 8. Vertices with
// no incident sharp edge must match the smooth run exactly.
TEST(Catmull_clark_crease, sharp_rules_at_level_one)
{
    std::unique_ptr<Geometry> source = make_processed_cube();
    std::unique_ptr<Geometry> smooth_reference = subdivide(*source);
    tag_top_loop(*source, 2.0f);
    std::unique_ptr<Geometry> result = subdivide(*source);

    const GEO::Mesh& src_mesh = source->get_mesh();
    const GEO::Mesh& dst_mesh = result->get_mesh();

    // Midpoints of tagged edges lie exactly on the segment midpoint.
    for (GEO::index_t edge : top_loop_edges(*source)) {
        const GEO::vec3f a = get_pointf(src_mesh.vertices, src_mesh.edges.vertex(edge, 0));
        const GEO::vec3f b = get_pointf(src_mesh.vertices, src_mesh.edges.vertex(edge, 1));
        const GEO::vec3f midpoint = get_pointf(dst_mesh.vertices, 8 + edge);
        expect_near(midpoint, 0.5f * (a + b));
    }

    // Crease vertex rule at the four top corners. Loop neighbors on the top
    // face {2, 6, 7, 4}: 2-6, 6-7, 7-4, 4-2.
    const GEO::index_t loop[4]      = { 2, 6, 7, 4 };
    for (int i = 0; i < 4; ++i) {
        const GEO::index_t v     = loop[i];
        const GEO::index_t far_a = loop[(i + 3) % 4];
        const GEO::index_t far_b = loop[(i + 1) % 4];
        const GEO::vec3f p_v     = get_pointf(src_mesh.vertices, v);
        const GEO::vec3f p_far_a = get_pointf(src_mesh.vertices, far_a);
        const GEO::vec3f p_far_b = get_pointf(src_mesh.vertices, far_b);
        const GEO::vec3f expected = (1.0f / 8.0f) * (p_far_a + (6.0f * p_v) + p_far_b);
        expect_near(get_pointf(dst_mesh.vertices, v), expected);
    }

    // Bottom vertices (no incident sharp edge) keep the smooth vertex rule;
    // that rule reads only source positions, so they match the smooth run.
    for (GEO::index_t v : { GEO::index_t{0}, GEO::index_t{1}, GEO::index_t{3}, GEO::index_t{5} }) {
        expect_near(get_pointf(dst_mesh.vertices, v), get_pointf(smooth_reference->get_mesh().vertices, v));
    }
}

// 3+ incident sharp edges: corner rule, the vertex does not move.
TEST(Catmull_clark_crease, corner_vertex_pinned)
{
    std::unique_ptr<Geometry> source = make_processed_cube();
    // The three edges incident to vertex 7: (6,7), (4,7), (5,7).
    source->set_edge_sharpness(6, 7, 3.0f);
    source->set_edge_sharpness(4, 7, 3.0f);
    source->set_edge_sharpness(5, 7, 3.0f);
    std::unique_ptr<Geometry> result = subdivide(*source);

    const GEO::vec3f original = get_pointf(source->get_mesh().vertices, 7);
    expect_near(get_pointf(result->get_mesh().vertices, 7), original);
}

// Fractional sharpness 0 < s < 1: edge points blend smooth and sharp results
// by s (paper eq. 11); crease vertices blend the crease mask with the smooth
// mask by the transitional weight (= s, since both incident sharpness values
// decay to zero in one step).
TEST(Catmull_clark_crease, fractional_blend)
{
    const float s = 0.5f;
    std::unique_ptr<Geometry> source = make_processed_cube();
    std::unique_ptr<Geometry> smooth_result = subdivide(*source);
    tag_top_loop(*source, s);
    std::unique_ptr<Geometry> creased_result = subdivide(*source);

    const GEO::Mesh& src_mesh    = source->get_mesh();
    const GEO::Mesh& smooth_mesh = smooth_result->get_mesh();
    const GEO::Mesh& dst_mesh    = creased_result->get_mesh();

    for (GEO::index_t edge : top_loop_edges(*source)) {
        const GEO::vec3f a = get_pointf(src_mesh.vertices, src_mesh.edges.vertex(edge, 0));
        const GEO::vec3f b = get_pointf(src_mesh.vertices, src_mesh.edges.vertex(edge, 1));
        const GEO::vec3f smooth_midpoint = get_pointf(smooth_mesh.vertices, 8 + edge);
        const GEO::vec3f sharp_midpoint  = 0.5f * (a + b);
        const GEO::vec3f expected        = ((1.0f - s) * smooth_midpoint) + (s * sharp_midpoint);
        expect_near(get_pointf(dst_mesh.vertices, 8 + edge), expected);
    }

    const GEO::index_t loop[4] = { 2, 6, 7, 4 };
    for (int i = 0; i < 4; ++i) {
        const GEO::index_t v     = loop[i];
        const GEO::index_t far_a = loop[(i + 3) % 4];
        const GEO::index_t far_b = loop[(i + 1) % 4];
        const GEO::vec3f p_v     = get_pointf(src_mesh.vertices, v);
        const GEO::vec3f p_far_a = get_pointf(src_mesh.vertices, far_a);
        const GEO::vec3f p_far_b = get_pointf(src_mesh.vertices, far_b);
        const GEO::vec3f crease_position = (1.0f / 8.0f) * (p_far_a + (6.0f * p_v) + p_far_b);
        const GEO::vec3f smooth_position = get_pointf(smooth_mesh.vertices, v);
        const GEO::vec3f expected        = (s * crease_position) + ((1.0f - s) * smooth_position);
        expect_near(get_pointf(dst_mesh.vertices, v), expected);
    }
}

// Sharpness 3.6 decays by 1 per level (Chaikin on the uniform loop reduces to
// the uniform rule: every neighbor average equals the edge's own sharpness).
// Level counts: 4 edges -> 8 children at 2.6 -> 16 at 1.6 -> 32 at 0.6 -> none.
// Intermediates run structural_only like the editor's iterated chain does;
// propagation must not depend on the full post-processing flag set.
TEST(Catmull_clark_crease, sharpness_decays_across_chain)
{
    std::unique_ptr<Geometry> current = make_processed_cube();
    tag_top_loop(*current, 3.6f);

    const std::vector<std::pair<std::size_t, float>> expected_levels = {
        { 8u, 2.6f }, { 16u, 1.6f }, { 32u, 0.6f }, { 0u, 0.0f },
    };
    for (std::size_t level = 0; level < expected_levels.size(); ++level) {
        const bool last = (level + 1) == expected_levels.size();
        std::unique_ptr<Geometry> next = subdivide(*current, last ? Post_processing::full_default : Post_processing::structural_only);
        const std::vector<float> values = collect_destination_sharpness(*next);
        EXPECT_EQ(values.size(), expected_levels[level].first) << "level " << level;
        for (const float value : values) {
            EXPECT_NEAR(value, expected_levels[level].second, 1e-5f) << "level " << level;
        }
        current = std::move(next);
    }
}

// Infinitely sharp edges stay infinitely sharp forever.
TEST(Catmull_clark_crease, infinite_sharpness_persists)
{
    std::unique_ptr<Geometry> current = make_processed_cube();
    tag_top_loop(*current, std::numeric_limits<float>::infinity());

    std::size_t expected_count = 8;
    for (int level = 0; level < 3; ++level) {
        std::unique_ptr<Geometry> next = subdivide(*current);
        const std::vector<float> values = collect_destination_sharpness(*next);
        EXPECT_EQ(values.size(), expected_count) << "level " << level;
        for (const float value : values) {
            EXPECT_TRUE(std::isinf(value)) << "level " << level;
        }
        current = std::move(next);
        expected_count *= 2;
    }

    // And the crease stays geometrically sharp: every creased edge midpoint
    // lies on its parent segment at every level (spot-check level 1).
    std::unique_ptr<Geometry> source = make_processed_cube();
    tag_top_loop(*source, std::numeric_limits<float>::infinity());
    std::unique_ptr<Geometry> result = subdivide(*source);
    const GEO::Mesh& src_mesh = source->get_mesh();
    for (GEO::index_t edge : top_loop_edges(*source)) {
        const GEO::vec3f a = get_pointf(src_mesh.vertices, src_mesh.edges.vertex(edge, 0));
        const GEO::vec3f b = get_pointf(src_mesh.vertices, src_mesh.edges.vertex(edge, 1));
        expect_near(get_pointf(result->get_mesh().vertices, 8 + edge), 0.5f * (a + b));
    }
}
