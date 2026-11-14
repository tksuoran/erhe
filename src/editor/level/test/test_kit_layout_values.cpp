#include "level/kit_layout_values.hpp"
#include "level/kit_shape.hpp"

#include "erhe_math/curve_path.hpp"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace {

using editor::Kit_layout_count_mode;
using editor::Kit_layout_item;
using editor::Kit_layout_pattern;
using editor::Kit_layout_run;
using editor::Repeat_path_orientation;
using editor::Repeat_pattern_kind;
using editor::Repeat_pose;
using editor::Repeat_ring_facing;

constexpr float c_tolerance = 1.0e-4f;

auto run_of(const Kit_layout_pattern& pattern, const float extent, Kit_layout_run& run, std::vector<Repeat_pose>& poses, const erhe::math::Curve_path_table* table = nullptr) -> std::string
{
    const std::string error = editor::resolve_layout_pattern(pattern, extent, table, run);
    if (!error.empty()) {
        return error;
    }
    return editor::expand_repeat(run.pattern, Repeat_pose{}, poses);
}

auto make_pattern(const Repeat_pattern_kind kind, const Kit_layout_count_mode mode) -> Kit_layout_pattern
{
    Kit_layout_pattern pattern;
    pattern.kind       = kind;
    pattern.count_mode = mode;
    return pattern;
}

TEST(Kit_layout_values, pattern_text_round_trips)
{
    Kit_layout_pattern pattern;
    pattern.kind        = Repeat_pattern_kind::path;
    pattern.count_mode  = Kit_layout_count_mode::gap;
    pattern.count       = 7;
    pattern.gap         = 0.1f;
    pattern.direction   = glm::vec3{0.0f, 0.0f, -1.0f};
    pattern.step        = 1.25f;
    pattern.length      = 1.0f / 3.0f;
    pattern.count_x     = 2;
    pattern.count_z     = 5;
    pattern.spacing_x   = 0.7f;
    pattern.spacing_z   = 2.5f;
    pattern.radius      = 6.0f;
    pattern.arc_degrees = 90.0f;
    pattern.facing      = Repeat_ring_facing::start_rotation;
    pattern.orientation = Repeat_path_orientation::tangent;
    const std::string text = editor::to_canonical_text(pattern);
    Kit_layout_pattern read;
    ASSERT_EQ(editor::parse_layout_pattern(text, read), "");
    EXPECT_TRUE(read == pattern);
    EXPECT_EQ(editor::to_canonical_text(read), text);
    // Floats are hexadecimal strings, so the third survives bit for bit.
    EXPECT_NE(text.find(editor::format_hexfloat(1.0f / 3.0f)), std::string::npos);
}

TEST(Kit_layout_values, pattern_text_refusals)
{
    Kit_layout_pattern pattern;
    EXPECT_NE(editor::parse_layout_pattern("[]", pattern), "");
    std::string text = editor::to_canonical_text(Kit_layout_pattern{});
    std::string unknown_kind = text;
    unknown_kind.replace(unknown_kind.find("\"line\""), 6, "\"spiral\"");
    EXPECT_NE(editor::parse_layout_pattern(unknown_kind, pattern).find("spiral"), std::string::npos);
    std::string extra = text;
    extra.insert(1, "\"extra\":1,");
    EXPECT_NE(editor::parse_layout_pattern(extra, pattern), "");
    std::string decimal = text;
    const std::size_t step = decimal.find("\"step\":");
    const std::size_t end  = decimal.find_first_of(",}", step);
    decimal.replace(step, end - step, "\"step\":1.0");
    EXPECT_NE(editor::parse_layout_pattern(decimal, pattern).find("step"), std::string::npos);
}

TEST(Kit_layout_values, items_text_round_trips)
{
    std::vector<Kit_layout_item> items{
        Kit_layout_item{.shape = "basic.block", .parameters = "{\"width\":\"0x1p+0\",\"seed\":3,\"derive\":[\"width\"]}", .materials = {"Stone"}},
        Kit_layout_item{.shape = "nature.tree", .parameters = "{\"seed\":0}", .materials = {"", "Leaves"}}
    };
    const std::string text = editor::to_canonical_text(std::span<const Kit_layout_item>{items});
    std::vector<Kit_layout_item> read;
    ASSERT_EQ(editor::parse_layout_items(text, read), "");
    ASSERT_EQ(read.size(), 2u);
    EXPECT_EQ(read[0].shape, "basic.block");
    EXPECT_EQ(read[1].materials, (std::vector<std::string>{"", "Leaves"}));
    EXPECT_EQ(editor::to_canonical_text(std::span<const Kit_layout_item>{read}), text);
    EXPECT_NE(editor::parse_layout_items("[{\"shape\":\"basic.block\"}]", read), "");
    EXPECT_NE(editor::parse_layout_items("{}", read), "");
}

TEST(Kit_layout_values, item_cycling_and_element_seeds)
{
    EXPECT_EQ(editor::get_layout_item_index(0, 2), 0u);
    EXPECT_EQ(editor::get_layout_item_index(1, 2), 1u);
    EXPECT_EQ(editor::get_layout_item_index(4, 3), 1u);
    EXPECT_EQ(editor::get_layout_item_index(5, 1), 0u);
    std::set<int> seeds;
    for (std::size_t i = 0; i < 16; ++i) {
        const int seed = editor::derive_layout_element_seed(7, i);
        EXPECT_EQ(seed, editor::derive_layout_element_seed(7, i));
        EXPECT_GE(seed, 0);
        EXPECT_LE(seed, editor::kit_seed_max);
        seeds.insert(seed);
    }
    EXPECT_EQ(seeds.size(), 16u);
    EXPECT_NE(editor::derive_layout_element_seed(7, 0), editor::derive_layout_element_seed(8, 0));
}

TEST(Kit_layout_values, line_count_mode_steps_along_the_direction)
{
    Kit_layout_pattern pattern = make_pattern(Repeat_pattern_kind::line, Kit_layout_count_mode::count);
    pattern.count     = 4;
    pattern.step      = 2.0f;
    pattern.direction = glm::vec3{0.0f, 0.0f, 3.0f};
    Kit_layout_run run;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    ASSERT_EQ(poses.size(), 4u);
    EXPECT_EQ(run.count, 4u);
    EXPECT_NEAR(poses[3].position.z, 6.0f, c_tolerance);
    EXPECT_NEAR(run.space, 6.0f, c_tolerance);
}

TEST(Kit_layout_values, line_gap_mode_spreads_the_remainder)
{
    // Extent 1 + gap 0.5: a 1.5 m step; 7 m holds floor(7 / 1.5) + 1 = 5 centers, spread to 1.75 m.
    Kit_layout_pattern pattern = make_pattern(Repeat_pattern_kind::line, Kit_layout_count_mode::gap);
    pattern.length = 7.0f;
    pattern.gap    = 0.5f;
    Kit_layout_run run;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    ASSERT_EQ(poses.size(), 5u);
    EXPECT_NEAR(run.step, 1.75f, c_tolerance);
    EXPECT_NEAR(poses[0].position.x, 0.0f, c_tolerance);
    EXPECT_NEAR(poses[4].position.x, 7.0f, c_tolerance);
    // An exact fit: 6 m at 1.5 m holds 5 centers at the stated step.
    pattern.length = 6.0f;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    EXPECT_EQ(poses.size(), 5u);
    EXPECT_NEAR(run.step, 1.5f, c_tolerance);
    // Just short of the fit drops one: 4 centers spread over 5.99 m.
    pattern.length = 5.99f;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    EXPECT_EQ(poses.size(), 4u);
    // A line shorter than one step holds element 0 alone.
    pattern.length = 1.0f;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    ASSERT_EQ(poses.size(), 1u);
    EXPECT_NEAR(poses[0].position.x, 0.0f, c_tolerance);
    // A wider item makes a longer step and fewer pieces.
    pattern.length = 7.0f;
    ASSERT_EQ(run_of(pattern, 2.0f, run, poses), "");
    EXPECT_EQ(poses.size(), 3u);
}

TEST(Kit_layout_values, ring_gap_mode_closes_a_full_ring)
{
    // Circumference 2 pi 4 = 25.13 m; a 2 m step holds 12 pieces, 30 degrees apart.
    Kit_layout_pattern pattern = make_pattern(Repeat_pattern_kind::ring, Kit_layout_count_mode::gap);
    pattern.radius = 4.0f;
    pattern.gap    = 1.0f;
    Kit_layout_run run;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    ASSERT_EQ(poses.size(), 12u);
    EXPECT_NEAR(run.step, (glm::two_pi<float>() * 4.0f) / 12.0f, c_tolerance);
    // An arc holds elements at both ends: 90 degrees at radius 4 is 6.28 m, floor(6.28 / 2) + 1 = 4.
    pattern.arc_degrees = 90.0f;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    EXPECT_EQ(poses.size(), 4u);
    // A ring shorter than one step holds element 0 alone.
    pattern.arc_degrees = 360.0f;
    pattern.radius      = 0.1f;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses), "");
    EXPECT_EQ(poses.size(), 1u);
}

auto make_table(const erhe::math::Curve_path& path) -> erhe::math::Curve_path_table
{
    erhe::math::Curve_path_table table;
    table.build(path);
    return table;
}

auto corner_path(const std::vector<glm::vec3>& positions, const erhe::math::Curve_closure closure) -> erhe::math::Curve_path
{
    erhe::math::Curve_path path;
    for (const glm::vec3& position : positions) {
        path.points.push_back(erhe::math::Curve_point{.position = position});
    }
    path.closure = closure;
    return path;
}

TEST(Kit_layout_values, path_modes_on_a_corner_curve)
{
    // An L of corner points 4 m + 3 m long; count mode spreads 8 elements 1 m apart, both ends holding one.
    const erhe::math::Curve_path_table table = make_table(corner_path({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 3.0f}}, erhe::math::Curve_closure::open));
    Kit_layout_pattern pattern = make_pattern(Repeat_pattern_kind::path, Kit_layout_count_mode::count);
    pattern.count = 8;
    Kit_layout_run run;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses, &table), "");
    ASSERT_EQ(poses.size(), 8u);
    EXPECT_NEAR(run.space, 7.0f, c_tolerance);
    EXPECT_NEAR(run.step, 1.0f, c_tolerance);
    EXPECT_NEAR(poses[7].position.x, 4.0f, c_tolerance);
    EXPECT_NEAR(poses[7].position.z, 3.0f, c_tolerance);
    EXPECT_NEAR(poses[5].position.z, 1.0f, c_tolerance);
    // The corner piece takes the leaving span: x along +z.
    EXPECT_NEAR((poses[4].rotation * glm::vec3{1.0f, 0.0f, 0.0f}).z, 1.0f, c_tolerance);
    pattern.count = 1;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses, &table), "");
    EXPECT_EQ(poses.size(), 1u);
    // Gap mode: extent 1.5 + gap 0.5 is a 2 m step; 7 m holds 4 centers, spread to 2.333 m.
    pattern.count_mode = Kit_layout_count_mode::gap;
    pattern.gap        = 0.5f;
    ASSERT_EQ(run_of(pattern, 1.5f, run, poses, &table), "");
    ASSERT_EQ(poses.size(), 4u);
    EXPECT_NEAR(run.step, 7.0f / 3.0f, c_tolerance);
    EXPECT_NEAR(poses[3].position.z, 3.0f, c_tolerance);
}

TEST(Kit_layout_values, path_modes_on_a_closed_curve)
{
    // A closed square of 2 m sides: 8 m around, no piece twice at the seam.
    const erhe::math::Curve_path_table table = make_table(corner_path({{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 2.0f}}, erhe::math::Curve_closure::closed));
    Kit_layout_pattern pattern = make_pattern(Repeat_pattern_kind::path, Kit_layout_count_mode::count);
    pattern.count = 8;
    Kit_layout_run run;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses, &table), "");
    ASSERT_EQ(poses.size(), 8u);
    EXPECT_NEAR(run.step, 1.0f, c_tolerance);
    EXPECT_NEAR(poses[7].position.x, 0.0f, c_tolerance);
    EXPECT_NEAR(poses[7].position.z, 1.0f, c_tolerance);
    // Gap mode closes as a full ring: extent 1 + gap 0.5 is 1.5 m, 8 m holds 5, spread to 1.6 m.
    pattern.count_mode = Kit_layout_count_mode::gap;
    pattern.gap        = 0.5f;
    ASSERT_EQ(run_of(pattern, 1.0f, run, poses, &table), "");
    ASSERT_EQ(poses.size(), 5u);
    EXPECT_NEAR(run.step, 1.6f, c_tolerance);
    EXPECT_NEAR(poses[1].position.x, 1.6f, c_tolerance);
}

TEST(Kit_layout_values, path_on_a_smooth_curve_sits_at_the_table_distances)
{
    // A cubic quarter-circle approximation of radius 4 in the xz plane, rising 1 m.
    erhe::math::Curve_path path;
    path.points.push_back(erhe::math::Curve_point{.position = {0.0f, 0.0f, 0.0f}, .in_handle = {-2.2f, 0.0f, 0.0f}, .out_handle = {2.2f, 0.0f, 0.0f}, .kind = erhe::math::Curve_point_kind::smooth});
    path.points.push_back(erhe::math::Curve_point{.position = {4.0f, 1.0f, 4.0f}, .in_handle = {0.0f, 0.0f, -2.2f}, .out_handle = {0.0f, 0.0f, 2.2f}, .kind = erhe::math::Curve_point_kind::smooth});
    const erhe::math::Curve_path_table table = make_table(path);
    Kit_layout_pattern pattern = make_pattern(Repeat_pattern_kind::path, Kit_layout_count_mode::count);
    pattern.count = 5;
    for (const Repeat_path_orientation orientation : {Repeat_path_orientation::upright, Repeat_path_orientation::tangent}) {
        pattern.orientation = orientation;
        Kit_layout_run run;
        std::vector<Repeat_pose> poses;
        ASSERT_EQ(run_of(pattern, 1.0f, run, poses, &table), "");
        ASSERT_EQ(poses.size(), 5u);
        for (std::size_t i = 0; i < poses.size(); ++i) {
            const float distance = table.get_length() * static_cast<float>(i) / 4.0f;
            const erhe::math::Curve_sample sample = table.sample_at_distance(distance);
            EXPECT_NEAR(glm::length(poses[i].position - sample.position), 0.0f, c_tolerance);
            const glm::vec3 x_axis = poses[i].rotation * glm::vec3{1.0f, 0.0f, 0.0f};
            const glm::vec3 y_axis = poses[i].rotation * glm::vec3{0.0f, 1.0f, 0.0f};
            if (orientation == Repeat_path_orientation::tangent) {
                EXPECT_NEAR(glm::dot(x_axis, sample.tangent), 1.0f, c_tolerance);
            } else {
                const glm::vec3 heading = glm::normalize(glm::vec3{sample.tangent.x, 0.0f, sample.tangent.z});
                EXPECT_NEAR(glm::dot(x_axis, heading), 1.0f, c_tolerance);
                EXPECT_NEAR(y_axis.y, 1.0f, c_tolerance);
            }
        }
    }
}

TEST(Kit_layout_values, transformed_curve_path)
{
    erhe::math::Curve_path path;
    path.points.push_back(erhe::math::Curve_point{.position = {0.0f, 0.0f, 0.0f}, .out_handle = {1.0f, 0.0f, 0.0f}, .kind = erhe::math::Curve_point_kind::free});
    path.points.push_back(erhe::math::Curve_point{.position = {3.0f, 0.0f, 0.0f}, .in_handle = {0.0f, 0.0f, 1.0f}, .kind = erhe::math::Curve_point_kind::free});
    const glm::mat4 transform = glm::rotate(glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 1.0f, 0.0f}), glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    erhe::math::Curve_path moved;
    editor::transform_curve_path(path, transform, moved);
    ASSERT_EQ(moved.points.size(), 2u);
    EXPECT_NEAR(glm::length(moved.points[1].position - glm::vec3{5.0f, 1.0f, -3.0f}), 0.0f, c_tolerance);
    EXPECT_NEAR(glm::length(moved.points[0].out_handle - glm::vec3{0.0f, 0.0f, -1.0f}), 0.0f, c_tolerance);
    EXPECT_EQ(moved.points[1].kind, erhe::math::Curve_point_kind::free);
    EXPECT_NEAR(make_table(moved).get_length(), make_table(path).get_length(), c_tolerance);
}

TEST(Kit_layout_values, refusals)
{
    Kit_layout_run run;
    Kit_layout_pattern line = make_pattern(Repeat_pattern_kind::line, Kit_layout_count_mode::gap);
    line.length = 5000.0f;
    line.gap    = 0.0f;
    EXPECT_NE(editor::resolve_layout_pattern(line, 1.0f, nullptr, run).find("more than the 1024"), std::string::npos);
    line.length = 4.0f;
    line.gap    = -1.0f;
    EXPECT_NE(editor::resolve_layout_pattern(line, 1.0f, nullptr, run).find("not positive"), std::string::npos);
    line.gap    = 0.0f;
    line.length = -1.0f;
    EXPECT_NE(editor::resolve_layout_pattern(line, 1.0f, nullptr, run), "");
    Kit_layout_pattern counted = make_pattern(Repeat_pattern_kind::ring, Kit_layout_count_mode::count);
    counted.count = 0;
    EXPECT_NE(editor::resolve_layout_pattern(counted, 1.0f, nullptr, run), "");
    counted.count = 1025;
    EXPECT_NE(editor::resolve_layout_pattern(counted, 1.0f, nullptr, run), "");
    Kit_layout_pattern path = make_pattern(Repeat_pattern_kind::path, Kit_layout_count_mode::count);
    EXPECT_NE(editor::resolve_layout_pattern(path, 1.0f, nullptr, run).find("needs a curve"), std::string::npos);
    const erhe::math::Curve_path_table point = make_table(corner_path({glm::vec3{0.0f}, glm::vec3{0.0f}}, erhe::math::Curve_closure::open));
    path.count  = 3;
    EXPECT_NE(editor::resolve_layout_pattern(path, 1.0f, &point, run).find("no length"), std::string::npos);
    const erhe::math::Curve_path_table long_line = make_table(corner_path({glm::vec3{0.0f}, glm::vec3{2000.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::open));
    path.count_mode = Kit_layout_count_mode::gap;
    EXPECT_NE(editor::resolve_layout_pattern(path, 1.0f, &long_line, run).find("1024"), std::string::npos);
    Kit_layout_pattern grid = make_pattern(Repeat_pattern_kind::grid, Kit_layout_count_mode::gap);
    grid.count_x = 40;
    grid.count_z = 40;
    EXPECT_NE(editor::resolve_layout_pattern(grid, 1.0f, nullptr, run), "");
}

TEST(Kit_layout_values, spacing_from_extent)
{
    editor::Kit_layout_pattern pattern;
    editor::set_layout_spacing_from_extent(pattern, glm::vec3{4.0f, 3.0f, 0.25f});
    EXPECT_EQ(pattern.step, 4.0f);
    EXPECT_EQ(pattern.spacing_x, 4.0f);
    EXPECT_EQ(pattern.spacing_z, 0.25f);
    editor::set_layout_spacing_from_extent(pattern, glm::vec3{0.0f});
    EXPECT_EQ(pattern.step, 1.0f);
    EXPECT_EQ(pattern.spacing_z, 1.0f);
}

} // anonymous namespace
