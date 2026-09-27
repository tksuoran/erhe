#include "renderers/radiance_cascades_layout.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
#include <vector>

namespace {

using editor::Probe_grid;
using editor::Radiance_cascade;
using editor::Radiance_cascades_layout;
using editor::Radiance_cascades_layout_settings;

[[nodiscard]] auto make_box(const glm::vec3& min, const glm::vec3& max) -> erhe::math::Aabb
{
    erhe::math::Aabb box{};
    box.min = min;
    box.max = max;
    return box;
}

[[nodiscard]] auto max_axis(const glm::ivec3& v) -> int
{
    return std::max(v.x, std::max(v.y, v.z));
}

// The plan's sizing example (doc/plans/radiance_cascades.md section 3):
// a 20 x 10 x 20 m volume at s0 = 0.5 m, q0 = 4.
[[nodiscard]] auto fit_example(const Radiance_cascades_layout_settings& settings) -> Radiance_cascades_layout
{
    return editor::fit_radiance_cascades(make_box(glm::vec3{0.0f}, glm::vec3{20.0f, 10.0f, 20.0f}), settings);
}

TEST(Radiance_cascades_layout, cascade0_is_the_probe_grid_fit)
{
    const Radiance_cascades_layout layout = fit_example(Radiance_cascades_layout_settings{});
    ASSERT_TRUE(layout.is_valid());
    const Probe_grid& grid0 = layout.cascades[0].grid;
    EXPECT_EQ(grid0.counts, (glm::ivec3{41, 21, 41}));
    EXPECT_NEAR(grid0.spacing.x, 0.5f, 1.0e-5f);
    EXPECT_NEAR(grid0.spacing.y, 0.5f, 1.0e-5f);
    EXPECT_EQ(grid0.origin, glm::vec3{0.0f});
    EXPECT_EQ(layout.cascades[0].tile_texels, 4);
}

TEST(Radiance_cascades_layout, cascades_nest_and_stop_at_two_probes)
{
    const Radiance_cascades_layout layout = fit_example(Radiance_cascades_layout_settings{});
    // 41 -> 21 -> 11 -> 6 -> 3 -> 2 on the longest axis.
    ASSERT_EQ(layout.cascade_count, 6);
    EXPECT_EQ(layout.cascades[5].grid.counts, (glm::ivec3{2, 1, 2}));
    for (int i = 1; i < layout.cascade_count; ++i) {
        const Radiance_cascade& lower = layout.cascades[static_cast<std::size_t>(i - 1)];
        const Radiance_cascade& upper = layout.cascades[static_cast<std::size_t>(i)];
        EXPECT_GT(max_axis(lower.grid.counts), 2);
        EXPECT_EQ(upper.grid.counts, (lower.grid.counts + glm::ivec3{1}) / 2);
        EXPECT_EQ(upper.grid.spacing, 2.0f * lower.grid.spacing);
        EXPECT_EQ(upper.tile_texels, 2 * lower.tile_texels);
        // The upper grid is centred on the lower grid: both cover the same
        // volume, so the top cascades stay inside it.
        const glm::vec3 lower_centre = lower.grid.origin + (0.5f * glm::vec3{lower.grid.counts - glm::ivec3{1}} * lower.grid.spacing);
        const glm::vec3 upper_centre = upper.grid.origin + (0.5f * glm::vec3{upper.grid.counts - glm::ivec3{1}} * upper.grid.spacing);
        EXPECT_NEAR(upper_centre.x, lower_centre.x, 1.0e-3f);
        EXPECT_NEAR(upper_centre.y, lower_centre.y, 1.0e-3f);
        EXPECT_NEAR(upper_centre.z, lower_centre.z, 1.0e-3f);
        // Per axis, upper probe j sits at the centre of the lower pair
        // {2j, 2j + 1} for an even lower count, on lower probe 2j for an odd one.
        const glm::ivec3 j{1, 0, 1};
        for (int axis = 0; axis < 3; ++axis) {
            const int   n        = lower.grid.counts[axis];
            const float expected = ((n % 2) == 0)
                ? lower.grid.origin[axis] + ((static_cast<float>(2 * j[axis]) + 0.5f) * lower.grid.spacing[axis])
                : lower.grid.origin[axis] + (static_cast<float>(2 * j[axis]) * lower.grid.spacing[axis]);
            EXPECT_NEAR(upper.grid.get_probe_position(j)[axis], expected, 1.0e-3f) << "cascade " << i << " axis " << axis;
        }
    }
    // Texels: probes x q_i^2 per cascade, summed.
    int64_t sum = 0;
    for (const Radiance_cascade& cascade : layout.get_cascades()) {
        EXPECT_EQ(cascade.get_texel_count(), static_cast<int64_t>(cascade.get_probe_count()) * cascade.tile_texels * cascade.tile_texels);
        sum += cascade.get_texel_count();
    }
    EXPECT_EQ(layout.get_total_texels(), sum);
}

TEST(Radiance_cascades_layout, max_cascades_limits_the_count)
{
    Radiance_cascades_layout_settings settings{};
    settings.max_cascades = 3;
    EXPECT_EQ(fit_example(settings).cascade_count, 3);
    settings.max_cascades = 1;
    EXPECT_EQ(fit_example(settings).cascade_count, 1);
    settings.max_cascades = 100;
    EXPECT_EQ(fit_example(settings).cascade_count, 6);
}

TEST(Radiance_cascades_layout, probe_budget_grows_the_spacing)
{
    Radiance_cascades_layout_settings settings{};
    settings.max_probes_cascade0 = 4096;
    const Radiance_cascades_layout layout = fit_example(settings);
    ASSERT_TRUE(layout.is_valid());
    EXPECT_LE(layout.cascades[0].get_probe_count(), 4096);
    EXPECT_GT(layout.cascades[0].grid.spacing.x, 0.5f);
}

TEST(Radiance_cascades_layout, atlases_stay_within_max_texture_size)
{
    for (const int max_texture_size : {16384, 2048, 512, 256}) {
        Radiance_cascades_layout_settings settings{};
        settings.max_texture_size = max_texture_size;
        const Radiance_cascades_layout layout = fit_example(settings);
        ASSERT_TRUE(layout.is_valid()) << max_texture_size;
        for (const Radiance_cascade& cascade : layout.get_cascades()) {
            EXPECT_LE(cascade.get_atlas_width (), max_texture_size);
            EXPECT_LE(cascade.get_atlas_height(), max_texture_size);
            EXPECT_GE(static_cast<int64_t>(cascade.tiles_per_row) * cascade.tile_rows, cascade.get_probe_count());
        }
    }
}

TEST(Radiance_cascades_layout, neighbour_atlases_stay_within_max_texture_size)
{
    for (const int max_texture_size : {16384, 2048, 512}) {
        Radiance_cascades_layout_settings settings{};
        settings.max_texture_size   = max_texture_size;
        settings.atlas_block_width  = editor::c_neighbour_block_width;
        settings.atlas_block_height = editor::c_neighbour_block_height;
        const Radiance_cascades_layout layout = fit_example(settings);
        ASSERT_TRUE(layout.is_valid()) << max_texture_size;
        for (int i = 0; i < layout.cascade_count; ++i) {
            const Radiance_cascade& cascade = layout.cascades[static_cast<std::size_t>(i)];
            const bool is_top = (i == (layout.cascade_count - 1));
            const int  block_w = is_top ? 1 : editor::c_neighbour_block_width;
            const int  block_h = is_top ? 1 : editor::c_neighbour_block_height;
            EXPECT_LE(cascade.get_atlas_width () * block_w, max_texture_size) << i;
            EXPECT_LE(cascade.get_atlas_height() * block_h, max_texture_size) << i;
            EXPECT_GE(static_cast<int64_t>(cascade.tiles_per_row) * cascade.tile_rows, cascade.get_probe_count());
        }
    }
    // At the default texture size the block does not change the layout.
    Radiance_cascades_layout_settings plain{};
    Radiance_cascades_layout_settings block{};
    block.atlas_block_width  = editor::c_neighbour_block_width;
    block.atlas_block_height = editor::c_neighbour_block_height;
    EXPECT_TRUE(fit_example(plain) == fit_example(block));
}

TEST(Radiance_cascades_layout, tiles_wrap_probe_index_into_rows)
{
    const Radiance_cascades_layout layout = fit_example(Radiance_cascades_layout_settings{});
    for (const Radiance_cascade& cascade : layout.get_cascades()) {
        std::set<std::pair<int, int>> origins;
        for (int probe = 0; probe < cascade.get_probe_count(); ++probe) {
            const glm::ivec2 origin = cascade.get_tile_origin(probe);
            EXPECT_EQ(origin.x % cascade.tile_texels, 0);
            EXPECT_EQ(origin.y % cascade.tile_texels, 0);
            EXPECT_LE(origin.x + cascade.tile_texels, cascade.get_atlas_width ());
            EXPECT_LE(origin.y + cascade.tile_texels, cascade.get_atlas_height());
            origins.insert({origin.x, origin.y});
        }
        EXPECT_EQ(static_cast<int>(origins.size()), cascade.get_probe_count());
    }
}

TEST(Radiance_cascades_layout, interval_bounds)
{
    const float r0 = 2.0f;
    EXPECT_EQ(editor::get_radiance_interval(r0, 0), (glm::vec2{0.0f,  2.0f}));
    EXPECT_EQ(editor::get_radiance_interval(r0, 1), (glm::vec2{2.0f,  6.0f}));
    EXPECT_EQ(editor::get_radiance_interval(r0, 2), (glm::vec2{6.0f, 14.0f}));
    for (int i = 0; i < 10; ++i) {
        const glm::vec2 a = editor::get_radiance_interval(r0, i);
        const glm::vec2 b = editor::get_radiance_interval(r0, i + 1);
        EXPECT_EQ(a.y, b.x);                          // contiguous
        EXPECT_FLOAT_EQ(b.y - b.x, 2.0f * (a.y - a.x)); // length doubles
    }

    const Radiance_cascades_layout layout = fit_example(Radiance_cascades_layout_settings{});
    EXPECT_NEAR(layout.r0, std::sqrt(3.0f) * 0.5f, 1.0e-5f);
    for (int i = 0; i < layout.cascade_count; ++i) {
        const Radiance_cascade& cascade = layout.cascades[static_cast<std::size_t>(i)];
        const glm::vec2 interval = editor::get_radiance_interval(layout.r0, i);
        EXPECT_EQ(cascade.interval_start, interval.x);
        EXPECT_EQ(cascade.interval_end,   interval.y);
    }

    // interval_scale scales r0 and is clamped to >= 1.
    Radiance_cascades_layout_settings settings{};
    settings.interval_scale = 2.0f;
    EXPECT_NEAR(fit_example(settings).r0, 2.0f * std::sqrt(3.0f) * 0.5f, 1.0e-5f);
    settings.interval_scale = 0.25f;
    EXPECT_NEAR(fit_example(settings).r0, std::sqrt(3.0f) * 0.5f, 1.0e-5f);
}

TEST(Radiance_cascades_layout, octahedral_round_trip)
{
    const int q = 16;
    for (int v = 0; v < q; ++v) {
        for (int u = 0; u < q; ++u) {
            const glm::vec3 direction = editor::get_texel_direction(glm::ivec2{u, v}, q);
            EXPECT_NEAR(glm::length(direction), 1.0f, 1.0e-5f);
            EXPECT_EQ(editor::get_direction_texel(direction, q), (glm::ivec2{u, v}));
        }
    }
}

TEST(Radiance_cascades_layout, octahedral_2x2_nesting)
{
    for (const int q : {2, 4, 8}) {
        std::set<std::pair<int, int>> covered;
        for (int v = 0; v < q; ++v) {
            for (int u = 0; u < q; ++u) {
                const glm::ivec2 parent{u, v};
                for (const glm::ivec2& child : editor::get_child_texels(parent)) {
                    EXPECT_EQ(editor::get_parent_texel(child), parent);
                    // The child direction (in the 2q tile) lies inside the
                    // parent texel's footprint (in the q tile).
                    const glm::vec3 direction = editor::get_texel_direction(child, 2 * q);
                    EXPECT_EQ(editor::get_direction_texel(direction, q), parent);
                    covered.insert({child.x, child.y});
                }
            }
        }
        // The children of all parents cover the upper tile exactly once.
        EXPECT_EQ(static_cast<int>(covered.size()), 4 * q * q);
    }
}

TEST(Radiance_cascades_layout, upper_probe_axis_weights)
{
    // Even lower count (20 -> 10):
    // k = 2m -> (m - 1, m) with (0.25, 0.75); k = 2m + 1 -> (m, m + 1) with (0.75, 0.25).
    const editor::Upper_probe_axis even = editor::get_upper_probe_axis(4, 20, 10);
    EXPECT_EQ(even.index[0], 1);
    EXPECT_EQ(even.index[1], 2);
    EXPECT_EQ(even.weight[0], 0.25f);
    EXPECT_EQ(even.weight[1], 0.75f);
    const editor::Upper_probe_axis odd = editor::get_upper_probe_axis(5, 20, 10);
    EXPECT_EQ(odd.index[0], 2);
    EXPECT_EQ(odd.index[1], 3);
    EXPECT_EQ(odd.weight[0], 0.75f);
    EXPECT_EQ(odd.weight[1], 0.25f);
    // Odd lower count (9 -> 5): k = 2m on m; k = 2m + 1 halfway between m and m + 1.
    const editor::Upper_probe_axis on_probe = editor::get_upper_probe_axis(4, 9, 5);
    EXPECT_EQ(on_probe.index[0], 2);
    EXPECT_EQ(on_probe.weight[0], 1.0f);
    EXPECT_EQ(on_probe.weight[1], 0.0f);
    const editor::Upper_probe_axis between = editor::get_upper_probe_axis(5, 9, 5);
    EXPECT_EQ(between.index[0], 2);
    EXPECT_EQ(between.index[1], 3);
    EXPECT_EQ(between.weight[0], 0.5f);
    EXPECT_EQ(between.weight[1], 0.5f);
    // Edges clamp into the upper grid.
    const editor::Upper_probe_axis first = editor::get_upper_probe_axis(0, 6, 3);
    EXPECT_EQ(first.index[0], 0);
    EXPECT_EQ(first.index[1], 0);
    const editor::Upper_probe_axis last = editor::get_upper_probe_axis(5, 6, 3);
    EXPECT_EQ(last.index[0], 2);
    EXPECT_EQ(last.index[1], 2);
    const editor::Upper_probe_axis last_odd = editor::get_upper_probe_axis(8, 9, 5);
    EXPECT_EQ(last_odd.index[0], 4);
    EXPECT_EQ(last_odd.index[1], 4);
}

TEST(Radiance_cascades_layout, trilinear_upper_probes_interpolate_the_lower_position)
{
    // Every cascade pair of the example (41 -> 21 -> 11 -> 6 -> 3 -> 2 on
    // the longest axis: odd and even lower counts).
    const Radiance_cascades_layout layout = fit_example(Radiance_cascades_layout_settings{});
    for (int cascade = 1; cascade < layout.cascade_count; ++cascade) {
        const Probe_grid& lower = layout.cascades[static_cast<std::size_t>(cascade - 1)].grid;
        const Probe_grid& upper = layout.cascades[static_cast<std::size_t>(cascade)].grid;
        for (int z = 0; z < lower.counts.z; ++z) {
            for (int y = 0; y < lower.counts.y; ++y) {
                for (int x = 0; x < lower.counts.x; ++x) {
                    const glm::ivec3 coords{x, y, z};
                    const editor::Upper_probes probes = editor::get_upper_probes(coords, lower.counts, upper.counts);
                    float     weight_sum = 0.0f;
                    glm::vec3 position{0.0f};
                    for (std::size_t n = 0; n < 8; ++n) {
                        EXPECT_TRUE(glm::all(glm::greaterThanEqual(probes.coords[n], glm::ivec3{0})));
                        EXPECT_TRUE(glm::all(glm::lessThan        (probes.coords[n], upper.counts)));
                        weight_sum += probes.weights[n];
                        position   += probes.weights[n] * upper.get_probe_position(probes.coords[n]);
                    }
                    EXPECT_NEAR(weight_sum, 1.0f, 1.0e-6f);
                    // Away from the clamped edges the weights reproduce the
                    // lower probe position exactly: they are its trilinear
                    // weights in the upper grid.
                    const bool interior =
                        (x > 0) && (y > 0) && (z > 0) &&
                        (x < lower.counts.x - 1) && (y < lower.counts.y - 1) && (z < lower.counts.z - 1);
                    if (interior) {
                        const glm::vec3 expected = lower.get_probe_position(coords);
                        EXPECT_NEAR(position.x, expected.x, 1.0e-3f);
                        EXPECT_NEAR(position.y, expected.y, 1.0e-3f);
                        EXPECT_NEAR(position.z, expected.z, 1.0e-3f);
                    }
                }
            }
        }
    }
}

TEST(Probe_field_tiling, classic_layout_when_it_fits)
{
    // Small grids keep DDGI's original layout: tile (x + counts.x * z, y).
    const glm::ivec3 counts{5, 3, 7};
    const int        tiles_per_row = editor::get_probe_field_tiles_per_row(counts, 16, 16384);
    EXPECT_EQ(tiles_per_row, counts.x * counts.z);
    EXPECT_EQ(editor::get_probe_field_tile_rows(counts, tiles_per_row), counts.y);
    for (int z = 0; z < counts.z; ++z) {
        for (int y = 0; y < counts.y; ++y) {
            for (int x = 0; x < counts.x; ++x) {
                EXPECT_EQ(editor::get_probe_field_tile(glm::ivec3{x, y, z}, counts, tiles_per_row), (glm::ivec2{x + (counts.x * z), y}));
            }
        }
    }
}

TEST(Probe_field_tiling, wraps_rows_within_the_texture_limit)
{
    // 64 x 16 x 64 probes with 16-texel tiles: the classic row would be
    // 65536 texels wide.
    const glm::ivec3 counts{64, 16, 64};
    const int        tile          = 16;
    const int        max_size      = 16384;
    const int        tiles_per_row = editor::get_probe_field_tiles_per_row(counts, tile, max_size);
    const int        rows          = editor::get_probe_field_tile_rows(counts, tiles_per_row);
    EXPECT_LE(tiles_per_row * tile, max_size);
    EXPECT_LE(rows * tile, max_size);
    EXPECT_GE(static_cast<int64_t>(tiles_per_row) * rows, static_cast<int64_t>(counts.x) * counts.y * counts.z);
    // Every probe gets its own tile inside the atlas.
    std::set<std::pair<int, int>> seen;
    for (int z = 0; z < counts.z; ++z) {
        for (int y = 0; y < counts.y; ++y) {
            for (int x = 0; x < counts.x; ++x) {
                const glm::ivec2 t = editor::get_probe_field_tile(glm::ivec3{x, y, z}, counts, tiles_per_row);
                ASSERT_LT(t.x, tiles_per_row);
                ASSERT_LT(t.y, rows);
                EXPECT_TRUE(seen.insert({t.x, t.y}).second);
            }
        }
    }
    EXPECT_LE(static_cast<int64_t>(counts.x) * counts.y * counts.z, static_cast<int64_t>(editor::get_probe_field_max_probes(tile, max_size)));
}

TEST(Radiance_cascades_layout, octahedral_texel_solid_angles_cover_the_sphere)
{
    for (const int tile_texels : {1, 2, 4, 8}) {
        std::vector<float> solid_angles;
        editor::compute_octahedral_texel_solid_angles(tile_texels, solid_angles);
        ASSERT_EQ(solid_angles.size(), static_cast<std::size_t>(tile_texels * tile_texels));
        double sum = 0.0;
        for (const float solid_angle : solid_angles) {
            EXPECT_GT(solid_angle, 0.0f);
            sum += static_cast<double>(solid_angle);
        }
        // Midpoint rule over the octahedral parameter: within 0.5 %.
        EXPECT_NEAR(sum, 4.0 * 3.14159265358979, 0.005 * 4.0 * 3.14159265358979) << "tile_texels " << tile_texels;
    }
}

TEST(Radiance_cascades_layout, cosine_lobe_weights_integrate_to_pi)
{
    // The cosine lobe of any direction integrates to pi over the sphere, so
    // the weights of one output texel over all tile texels sum to pi.
    const int output_texels = 6;
    const int tile_texels   = 4;
    std::vector<float> weights;
    editor::compute_octahedral_lobe_weights(output_texels, tile_texels, 1.0f, weights);
    ASSERT_EQ(weights.size(), static_cast<std::size_t>(output_texels * output_texels * tile_texels * tile_texels));
    for (int n = 0; n < (output_texels * output_texels); ++n) {
        double sum = 0.0;
        for (int j = 0; j < (tile_texels * tile_texels); ++j) {
            const float weight = weights[static_cast<std::size_t>((n * tile_texels * tile_texels) + j)];
            EXPECT_GE(weight, 0.0f);
            sum += static_cast<double>(weight);
        }
        EXPECT_NEAR(sum, 3.14159265358979, 0.01 * 3.14159265358979) << "output texel " << n;
    }
    // A sharp lobe puts most of its weight on the tile texel containing its
    // direction.
    std::vector<float> sharp;
    editor::compute_octahedral_lobe_weights(tile_texels, tile_texels, 50.0f, sharp);
    for (int n = 0; n < (tile_texels * tile_texels); ++n) {
        const auto  begin = sharp.begin() + (n * tile_texels * tile_texels);
        const auto  end   = begin + (tile_texels * tile_texels);
        const auto  best  = std::max_element(begin, end);
        EXPECT_EQ(static_cast<int>(best - begin), n);
    }
}

TEST(Radiance_cascades_layout, invalid_box_gives_no_layout)
{
    EXPECT_FALSE(editor::fit_radiance_cascades(erhe::math::Aabb{}, Radiance_cascades_layout_settings{}).is_valid());
}

} // anonymous namespace
