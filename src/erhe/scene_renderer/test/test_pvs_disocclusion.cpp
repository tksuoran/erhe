// The CPU reference of the potentially visible set bake's disocclusion
// passes (doc/plans/pvs.md "Bake" 5 and "Verification";
// erhe_scene_renderer/pvs_disocclusion.hpp) over the synthetic layered
// buffers of test_pvs_disocclusion_cases.hpp: the paper's Figure 2, an
// empty buffer, a closed first layer, a surface crossing a layer boundary
// and the degenerate (no closed neighbour) window; and the cone rectangle
// against a brute-force sampling of the rays from the cell box through a
// window. The GPU passes are compared with this reference bit for bit by
// test_pvs_disocclusion_gpu.cpp.

#include "test_pvs_disocclusion_cases.hpp"

#include "erhe_scene_renderer/pvs_disocclusion.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <random>

using namespace erhe::scene_renderer;
using namespace erhe::scene_renderer::test;

namespace {

[[nodiscard]] auto run(const Pvs_test_case& test_case) -> Pvs_disocclusion_result
{
    Pvs_disocclusion_result result{};
    run_pvs_disocclusion_reference(test_case.parameters, std::span<const uint32_t>{test_case.layered_words}, std::span<const glm::vec4>{test_case.column_bounds}, result);
    return result;
}

[[nodiscard]] auto column_bit(const Pvs_disocclusion_result& result, const uint32_t x, const uint32_t y, const uint32_t layer) -> bool
{
    const uint32_t column = (y * c_pvs_test_tiles) + x;
    return (result.column_masks[(column * 2u) + (layer / 32u)] & (1u << (layer % 32u))) != 0u;
}

} // anonymous namespace

TEST(Pvs_disocclusion, figure_2_tile_in_both_cones_is_disoccluded)
{
    const Pvs_test_case           test_case = make_figure_2_case();
    const Pvs_disocclusion_result result    = run(test_case);
    // The two windows are cones (every neighbour closed), not degenerate.
    bool window_1 = false;
    bool window_2 = false;
    for (const glm::uvec2& cone : result.cones) {
        window_1 = window_1 || ((cone.x == pvs_test_tile_index(2, 4, 1)) && (cone.y == 15u));
        window_2 = window_2 || ((cone.x >= pvs_test_tile_index(3, 4, 2)) && (cone.x < pvs_test_tile_index(0, 5, 2)) && (cone.y == 15u));
    }
    EXPECT_TRUE(window_1);
    EXPECT_TRUE(window_2);
    EXPECT_TRUE (has_pvs_test_entry(result, c_figure_2_b)) << "B is seen through both windows";
    EXPECT_FALSE(has_pvs_test_entry(result, c_figure_2_a)) << "A is behind layer 1 outside W1's cone";
    EXPECT_TRUE (has_pvs_test_entry(result, 1)) << "layer 1 is in front";
    EXPECT_TRUE (has_pvs_test_entry(result, 2)) << "layer 2 is seen through W1";
}

TEST(Pvs_disocclusion, empty_buffer_is_disoccluded_everywhere)
{
    const Pvs_test_case           test_case = make_empty_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_TRUE(result.cones.empty());
    for (uint32_t layer = 0; layer < c_pvs_test_layer_count; ++layer) {
        for (uint32_t y = 0; y < c_pvs_test_tiles; ++y) {
            for (uint32_t x = 0; x < c_pvs_test_tiles; ++x) {
                EXPECT_TRUE(is_pvs_tile_disoccluded(test_case.parameters, result, x, y, layer)) << x << " " << y << " " << layer;
                if ((layer + 1u) < c_pvs_test_layer_count) {
                    EXPECT_TRUE(column_bit(result, x, y, layer));
                }
            }
        }
    }
    for (const uint32_t word : result.column_words) {
        EXPECT_EQ(word, 0u);
    }
}

TEST(Pvs_disocclusion, closed_layer_0_occludes_everything_behind_it)
{
    const Pvs_test_case           test_case = make_closed_layer_0_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_TRUE(has_pvs_test_entry(result, 1));
    EXPECT_FALSE(has_pvs_test_entry(result, 7));
    for (uint32_t layer = 1; layer < c_pvs_test_layer_count; ++layer) {
        for (uint32_t y = 0; y < c_pvs_test_tiles; ++y) {
            for (uint32_t x = 0; x < c_pvs_test_tiles; ++x) {
                EXPECT_FALSE(is_pvs_tile_disoccluded(test_case.parameters, result, x, y, layer)) << x << " " << y << " " << layer;
            }
        }
    }
}

TEST(Pvs_disocclusion, surface_crossing_a_layer_boundary_occludes)
{
    const Pvs_test_case           test_case = make_intersection_case();
    const Pvs_disocclusion_result result    = run(test_case);
    // Contiguous-run closure: the tile's first hits are layers 1 and 2.
    EXPECT_EQ(result.closed[pvs_test_tile_index(3, 3, 1)], 1u);
    EXPECT_EQ(result.closed[pvs_test_tile_index(3, 3, 2)], 1u);
    EXPECT_EQ(result.closed[pvs_test_tile_index(3, 3, 3)], 0u);
    EXPECT_TRUE (has_pvs_test_entry(result, 3)) << "the surface's front half";
    EXPECT_TRUE (has_pvs_test_entry(result, 4)) << "the surface's back half";
    EXPECT_FALSE(has_pvs_test_entry(result, 8)) << "behind the surface";
}

TEST(Pvs_disocclusion, degenerate_window_sets_its_column)
{
    const Pvs_test_case           test_case = make_degenerate_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_TRUE(column_bit(result, 4, 4, 1));
    for (const glm::uvec2& cone : result.cones) {
        EXPECT_NE(cone.x, pvs_test_tile_index(4, 4, 1));
    }
    // The ring around it has closed neighbours: cones.
    bool ring_cone = false;
    for (const glm::uvec2& cone : result.cones) {
        ring_cone = ring_cone || (cone.x == pvs_test_tile_index(3, 3, 1));
    }
    EXPECT_TRUE(ring_cone);
    EXPECT_TRUE(has_pvs_test_entry(result, 6));
}

// Rays from random points of the cell box through random points of an open
// tile's frustum segment in layer L, followed to a random depth of layer J:
// the tile they land in is inside the cone rectangle (sides all closed: no
// clamp to the column).
TEST(Pvs_disocclusion, cone_rectangle_contains_sampled_rays)
{
    std::mt19937 random{20261030u};
    std::uniform_real_distribution<double> unit{0.0, 1.0};
    for (uint32_t trial = 0; trial < 40; ++trial) {
        const float cell     = 0.2f + (1.6f * static_cast<float>(unit(random)));
        const float offset_x = (static_cast<float>(unit(random)) - 0.5f) * 0.5f * cell;
        const float offset_y = (static_cast<float>(unit(random)) - 0.5f) * 0.5f * cell;
        const float offset_z = (static_cast<float>(unit(random)) - 0.5f) * 0.5f * cell;
        const glm::vec3 cell_min{(-0.5f * cell) + offset_x, (-0.5f * cell) + offset_y, (-0.5f * cell) + offset_z};
        const glm::vec3 cell_max{( 0.5f * cell) + offset_x, ( 0.5f * cell) + offset_y, ( 0.5f * cell) + offset_z};
        const float z_near = std::max(std::max(-cell_min.z, cell_max.z), std::max(std::max(-cell_min.x, cell_max.x), std::max(-cell_min.y, cell_max.y))) + 0.01f;
        const float pixel  = 2.0f * std::tan(1.0471976f) / 64.0f;
        const Pvs_disocclusion_parameters parameters = make_pvs_disocclusion_parameters(
            Pvs_face_geometry{
                .resolution = 64, .tile_size = 4, .layer_count = 16,
                .z_near = z_near, .z_far = 60.0f,
                .tan_x0 = -32.0f * pixel, .tan_dx = pixel, .tan_y0 = 32.0f * pixel, .tan_dy = -pixel,
                .cell_min = cell_min, .cell_max = cell_max
            },
            1
        );
        const uint32_t tiles = parameters.get_tiles();
        const uint32_t tile_x = static_cast<uint32_t>(unit(random) * tiles) % tiles;
        const uint32_t tile_y = static_cast<uint32_t>(unit(random) * tiles) % tiles;
        const uint32_t layer  = static_cast<uint32_t>(unit(random) * 12.0) % 12u;
        for (uint32_t target = layer + 1u; target < parameters.get_layer_count(); ++target) {
            const glm::uvec4 rectangle = pvs_cone_rectangle(parameters, tile_x, tile_y, layer, 15u, target);
            for (uint32_t sample = 0; sample < 200; ++sample) {
                const double cx = cell_min.x + (unit(random) * (cell_max.x - cell_min.x));
                const double cy = cell_min.y + (unit(random) * (cell_max.y - cell_min.y));
                const double cz = cell_min.z + (unit(random) * (cell_max.z - cell_min.z));
                const double px = (static_cast<double>(tile_x) + unit(random)) * 4.0;
                const double py = (static_cast<double>(tile_y) + unit(random)) * 4.0;
                const double zl = parameters.layers[layer].x + (unit(random) * (parameters.layers[layer + 1].x - parameters.layers[layer].x));
                const double wx = zl * (parameters.tangents.x + (px * parameters.tangents.y));
                const double wy = zl * (parameters.tangents.z + (py * parameters.tangents.w));
                const double zj = parameters.layers[target].x + (unit(random) * (parameters.layers[target + 1].x - parameters.layers[target].x));
                const double t  = (zj - cz) / (zl - cz);
                const double hx = cx + (t * (wx - cx));
                const double hy = cy + (t * (wy - cy));
                const double fx = std::floor(((hx / zj) - parameters.tangents.x) / (parameters.tangents.y * 4.0));
                const double fy = std::floor(((hy / zj) - parameters.tangents.z) / (parameters.tangents.w * 4.0));
                if ((fx < 0.0) || (fy < 0.0) || (fx >= tiles) || (fy >= tiles)) {
                    continue; // outside the face
                }
                const uint32_t hit_x = static_cast<uint32_t>(fx);
                const uint32_t hit_y = static_cast<uint32_t>(fy);
                EXPECT_TRUE((hit_x >= rectangle.x) && (hit_x < rectangle.z) && (hit_y >= rectangle.y) && (hit_y < rectangle.w))
                    << "trial " << trial << " tile " << tile_x << ", " << tile_y << " layer " << layer << " target " << target
                    << " hit " << hit_x << ", " << hit_y << " rectangle " << rectangle.x << " " << rectangle.y << " " << rectangle.z << " " << rectangle.w;
            }
        }
    }
}

TEST(Pvs_disocclusion, grazing_band_closes_its_run)
{
    const Pvs_test_case           test_case = make_grazing_band_case();
    const Pvs_disocclusion_result result    = run(test_case);
    for (uint32_t layer = 2; layer <= 4; ++layer) {
        EXPECT_EQ(result.closed[pvs_test_tile_index(3, 3, layer)], 1u) << "layer " << layer;
    }
    EXPECT_EQ(result.closed[pvs_test_tile_index(3, 3, 5)], 0u);
    EXPECT_FALSE(is_pvs_tile_disoccluded(test_case.parameters, result, 4, 3, 7)) << "behind the band";
    EXPECT_FALSE(has_pvs_test_entry(result, 9));
}

TEST(Pvs_disocclusion, gated_spawning_needs_a_reachable_window)
{
    const Pvs_test_case           closed_case   = make_gating_closed_case();
    const Pvs_disocclusion_result closed_result = run(closed_case);
    const Pvs_test_case           open_case     = make_gating_open_case();
    const Pvs_disocclusion_result open_result   = run(open_case);
    const auto has_cone = [](const Pvs_disocclusion_result& result, const uint32_t tile) -> bool {
        for (const glm::uvec2& cone : result.cones) {
            if (cone.x == tile) {
                return true;
            }
        }
        return false;
    };
    EXPECT_FALSE(has_cone(closed_result, pvs_test_tile_index(4, 4, 3))) << "behind the sheet: not a window";
    EXPECT_FALSE(has_pvs_test_entry(closed_result, 5));
    EXPECT_TRUE (has_cone(open_result, pvs_test_tile_index(4, 4, 3))) << "reached through the hole: a window";
    EXPECT_TRUE (has_pvs_test_entry(open_result, 5));
}

namespace {

[[nodiscard]] auto has_cone_side(const Pvs_disocclusion_result& result, const uint32_t tile, const uint32_t side_bit) -> bool
{
    for (const glm::uvec2& cone : result.cones) {
        if ((cone.x == tile) && ((cone.y & side_bit) != 0u)) {
            return true;
        }
    }
    return false;
}

} // anonymous namespace

TEST(Pvs_disocclusion, rule_3_stair_step_plane_has_no_silhouette)
{
    const Pvs_test_case           test_case = make_rule_3_stair_case();
    const Pvs_disocclusion_result result    = run(test_case);
    for (const glm::uvec2& cone : result.cones) {
        ADD_FAILURE() << "cone at tile " << cone.x << " sides " << cone.y;
    }
    EXPECT_TRUE (has_pvs_test_entry(result, 1)) << "the plane is seen";
    EXPECT_FALSE(has_pvs_test_entry(result, c_rule_3_behind)) << "behind the plane";
}

TEST(Pvs_disocclusion, rule_3_box_edge_keeps_its_sloped_side)
{
    const Pvs_test_case           test_case = make_rule_3_box_edge_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_TRUE(has_cone_side(result, pvs_test_tile_index(4, 3, 2), 1u)) << "the far wall's window grows toward the box";
    EXPECT_TRUE(has_pvs_test_entry(result, c_rule_3_behind)) << "seen past the box edge";
}

TEST(Pvs_disocclusion, rule_3_crease_has_no_silhouette)
{
    const Pvs_test_case           test_case = make_rule_3_crease_case();
    const Pvs_disocclusion_result result    = run(test_case);
    for (const glm::uvec2& cone : result.cones) {
        ADD_FAILURE() << "cone at tile " << cone.x << " sides " << cone.y;
    }
    EXPECT_FALSE(has_pvs_test_entry(result, c_rule_3_behind)) << "behind the crease";
}

TEST(Pvs_disocclusion, rule_3_one_gap_row_keeps_the_sloped_side)
{
    const Pvs_test_case           test_case = make_rule_3_one_row_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_TRUE(has_cone_side(result, pvs_test_tile_index(4, 3, 2), 1u)) << "one gap row of K is a silhouette";
    EXPECT_TRUE(has_pvs_test_entry(result, c_rule_3_behind));
}

TEST(Pvs_disocclusion, rule_3_silhouette_inside_the_window_keeps_the_sloped_side)
{
    const Pvs_test_case           test_case = make_rule_3_inner_edge_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_TRUE(has_cone_side(result, pvs_test_tile_index(4, 3, 2), 1u)) << "the step inside the window is a silhouette";
    EXPECT_TRUE(has_pvs_test_entry(result, c_rule_3_behind));
}

TEST(Pvs_disocclusion, rule_3_step_above_the_tolerance_is_a_silhouette)
{
    const Pvs_test_case           test_case = make_rule_3_small_step_case();
    const Pvs_disocclusion_result result    = run(test_case);
    bool sloped = false;
    for (const glm::uvec2& cone : result.cones) {
        const uint32_t x = cone.x % c_pvs_test_tiles;
        sloped = sloped || ((x == 5u) && ((cone.y & 1u) != 0u));
    }
    EXPECT_TRUE(sloped) << "a 1.5-slope step is a silhouette at tolerance " << pvs_gap_slope_tolerance;
}

TEST(Pvs_disocclusion, rule_3_ledge_inside_the_window_keeps_the_sloped_side)
{
    const Pvs_test_case           test_case = make_rule_3_ledge_case();
    const Pvs_disocclusion_result result    = run(test_case);
    // 4 x 4 tiles of 4 pixels: tile (2, 1, 2) has index (2 * 4 + 1) * 4 + 2.
    const uint32_t window = (((2u * 4u) + 1u) * 4u) + 2u;
    bool sloped = false;
    for (const glm::uvec2& cone : result.cones) {
        sloped = sloped || ((cone.x == window) && ((cone.y & 1u) != 0u));
    }
    EXPECT_TRUE(sloped) << "the ledge between rows 5 and 6 is a silhouette";
}

TEST(Pvs_disocclusion, rule_1_run_with_an_inner_step_does_not_close)
{
    const Pvs_test_case           test_case = make_rule_1_step_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_EQ(result.closed[pvs_test_tile_index(4, 3, 2)], 0u);
    EXPECT_EQ(result.closed[pvs_test_tile_index(4, 3, 3)], 0u);
    EXPECT_EQ(result.runs[(3u * c_pvs_test_tiles) + 4u] & 0xffu, c_pvs_test_layer_count) << "no run";
}

TEST(Pvs_disocclusion, gap_test_has_a_depth_epsilon)
{
    const Pvs_disocclusion_result ulp  = run(make_epsilon_case(Pvs_epsilon_step::none));
    const Pvs_disocclusion_result step = run(make_epsilon_case(Pvs_epsilon_step::one_centimeter));
    const uint32_t column = (3u * c_pvs_test_tiles) + 4u;
    EXPECT_NE(ulp.runs[column] & 0xffu, c_pvs_test_layer_count) << "one-ULP row noise keeps the sheet continuous";
    EXPECT_EQ(step.runs[column] & 0xffu, c_pvs_test_layer_count) << "a 1 cm step at 10 m with zero slope is a gap";
}

TEST(Pvs_disocclusion, word_boundary_case_uses_the_high_mask_word)
{
    const Pvs_test_case           test_case = make_word_boundary_case();
    const Pvs_disocclusion_result result    = run(test_case);
    // Tile (2, 1)'s run [32, 33] sets column bit 32 (the high word).
    const uint32_t column = (1u * 8u) + 2u;
    EXPECT_NE(result.column_masks[(column * 2u) + 1u] & 1u, 0u);
    // A cone of layer 32 (a window next to the layer 32 sheet at (5, 5)).
    bool layer_32_cone = false;
    for (const glm::uvec2& cone : result.cones) {
        layer_32_cone = layer_32_cone || ((cone.x / 64u) == 32u);
    }
    EXPECT_TRUE(layer_32_cone);
}

TEST(Pvs_disocclusion, bounds_vote_cases)
{
    const Pvs_test_case           test_case = make_bounds_vote_case();
    const Pvs_disocclusion_result result    = run(test_case);
    EXPECT_TRUE (has_pvs_test_entry(result, 10)) << "reaches into the near cube";
    EXPECT_TRUE (has_pvs_test_entry(result, 11)) << "spans the face: the holes' chunks through the coarse map";
    EXPECT_TRUE (has_pvs_test_entry(result, 12)) << "reaches past the left edge into the hole (0, 3)";
    EXPECT_FALSE(has_pvs_test_entry(result, 13)) << "wholly left of the face";
    EXPECT_TRUE (has_pvs_test_entry(result, 14)) << "behind the hole (5, 5), past z_far";
    EXPECT_FALSE(has_pvs_test_entry(result, 15)) << "behind layer 0 outside every cone";
    // Column 11 is found only through a level above 0 (its rectangle spans
    // the face).
    EXPECT_GT(test_case.parameters.get_map_level_count(), 2u);
}

// The random buffer the GPU test compares bit for bit exercises every pass:
// cones, confirmed intersections, degenerate columns and votes.
TEST(Pvs_disocclusion, random_case_exercises_every_pass)
{
    for (const uint32_t seed : {1u, 2u, 3u}) {
        const Pvs_test_case           test_case = make_random_case(seed);
        const Pvs_disocclusion_result result    = run(test_case);
        EXPECT_FALSE(result.cones.empty()) << "seed " << seed;
        uint32_t column_bits = 0;
        for (const uint32_t word : result.column_masks) {
            column_bits |= word;
        }
        EXPECT_NE(column_bits, 0u) << "seed " << seed;
        uint32_t entry_bits = 0;
        for (const uint32_t word : result.column_words) {
            entry_bits |= word;
        }
        EXPECT_NE(entry_bits, 0u) << "seed " << seed;
    }
}
