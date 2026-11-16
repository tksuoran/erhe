#include "level/kit_layout_values.hpp"
#include "level/kit_mesh_builder.hpp"
#include "level/kit_shape.hpp"

#include "erhe_math/aabb.hpp"
#include "erhe_math/curve_path.hpp"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <glm/gtc/constants.hpp>

#include <bit>
#include <cmath>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace {

using editor::Kit_layout_count_mode;
using editor::Kit_layout_item;
using editor::Kit_layout_item_sizes;
using editor::Repeat_scatter_item;
using editor::Kit_layout_pattern;
using editor::Kit_layout_run;
using editor::Repeat_path_orientation;
using editor::Repeat_pattern_kind;
using editor::Repeat_pose;
using editor::Repeat_ring_facing;
using editor::Repeat_scatter_region;
using editor::Kit_scatter_mode;

constexpr float c_tolerance = 1.0e-4f;

// The canonical text of the default pattern, written before segments existed: it must not change.
constexpr const char* c_default_pattern_text =
    R"({"arc_degrees":"0x1.68p+8","count":3,"count_mode":"count","count_x":3,"count_z":3,"density":"0x1p-2","direction":["0x1p+0","0x0p+0","0x0p+0"],"facing":"outward","gap":"0x0p+0","kind":"line","length":"0x1p+2","min_spacing":"0x1p+0","orientation":"upright","radius":"0x1p+2","region":"rectangle","region_radius":"0x1p+2","scatter_mode":"count","size_x":"0x1p+3","size_z":"0x1p+3","spacing_x":"0x1p+0","spacing_z":"0x1p+0","step":"0x1p+0","yaw_max":"0x1.68p+8","yaw_min":"0x0p+0"})";

// One item of weight 1 and no footprint with this extent along x.
auto sizes_of(const float extent) -> Kit_layout_item_sizes
{
    Kit_layout_item_sizes sizes;
    sizes.extent = extent;
    sizes.items.push_back(Repeat_scatter_item{.weight = 1, .radius = 0.0f});
    return sizes;
}

auto run_of(const Kit_layout_pattern& pattern, const float extent, Kit_layout_run& run, std::vector<Repeat_pose>& poses, const erhe::math::Curve_path_table* table = nullptr) -> std::string
{
    const Kit_layout_item_sizes sizes = sizes_of(extent);
    const std::string error = editor::resolve_layout_pattern(pattern, 0, sizes, table, run, nullptr);
    if (!error.empty()) {
        return error;
    }
    return editor::expand_repeat(run.pattern, Repeat_pose{}, poses);
}

auto run_of_seeded(const Kit_layout_pattern& pattern, const int seed, Kit_layout_run& run, std::vector<Repeat_pose>& poses) -> std::string
{
    return editor::resolve_layout_pattern(pattern, seed, sizes_of(0.0f), nullptr, run, &poses);
}

// The bits of every position, so a changed accept decision anywhere changes the value.
auto position_bits_hash(const std::vector<Repeat_pose>& poses) -> uint64_t
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const Repeat_pose& pose : poses) {
        for (glm::vec3::length_type i = 0; i < 3; ++i) {
            hash ^= static_cast<uint64_t>(std::bit_cast<uint32_t>(pose.position[i]));
            hash *= 0x100000001b3ull;
        }
    }
    return hash;
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
    EXPECT_NE(editor::resolve_layout_pattern(line, 0, sizes_of(1.0f), nullptr, run, nullptr).find("more than the 1024"), std::string::npos);
    line.length = 4.0f;
    line.gap    = -1.0f;
    EXPECT_NE(editor::resolve_layout_pattern(line, 0, sizes_of(1.0f), nullptr, run, nullptr).find("not positive"), std::string::npos);
    line.gap    = 0.0f;
    line.length = -1.0f;
    EXPECT_NE(editor::resolve_layout_pattern(line, 0, sizes_of(1.0f), nullptr, run, nullptr), "");
    Kit_layout_pattern counted = make_pattern(Repeat_pattern_kind::ring, Kit_layout_count_mode::count);
    counted.count = 0;
    EXPECT_NE(editor::resolve_layout_pattern(counted, 0, sizes_of(1.0f), nullptr, run, nullptr), "");
    counted.count = 1025;
    EXPECT_NE(editor::resolve_layout_pattern(counted, 0, sizes_of(1.0f), nullptr, run, nullptr), "");
    Kit_layout_pattern path = make_pattern(Repeat_pattern_kind::path, Kit_layout_count_mode::count);
    EXPECT_NE(editor::resolve_layout_pattern(path, 0, sizes_of(1.0f), nullptr, run, nullptr).find("needs a curve"), std::string::npos);
    const erhe::math::Curve_path_table point = make_table(corner_path({glm::vec3{0.0f}, glm::vec3{0.0f}}, erhe::math::Curve_closure::open));
    path.count  = 3;
    EXPECT_NE(editor::resolve_layout_pattern(path, 0, sizes_of(1.0f), &point, run, nullptr).find("no length"), std::string::npos);
    const erhe::math::Curve_path_table long_line = make_table(corner_path({glm::vec3{0.0f}, glm::vec3{2000.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::open));
    path.count_mode = Kit_layout_count_mode::gap;
    EXPECT_NE(editor::resolve_layout_pattern(path, 0, sizes_of(1.0f), &long_line, run, nullptr).find("1024"), std::string::npos);
    Kit_layout_pattern grid = make_pattern(Repeat_pattern_kind::grid, Kit_layout_count_mode::gap);
    grid.count_x = 40;
    grid.count_z = 40;
    EXPECT_NE(editor::resolve_layout_pattern(grid, 0, sizes_of(1.0f), nullptr, run, nullptr), "");
}

auto make_scatter(const Repeat_scatter_region region, const int count, const float spacing) -> Kit_layout_pattern
{
    Kit_layout_pattern pattern;
    pattern.kind          = Repeat_pattern_kind::scatter;
    pattern.region        = region;
    pattern.count         = count;
    pattern.size_x        = 10.0f;
    pattern.size_z        = 6.0f;
    pattern.region_radius = 5.0f;
    pattern.min_spacing   = spacing;
    pattern.yaw_min       = -30.0f;
    pattern.yaw_max       = 45.0f;
    return pattern;
}

// Every pair at least the spacing apart, with the sampler's own float arithmetic.
void expect_spacing_holds(const std::vector<Repeat_pose>& poses, const float spacing)
{
    const float spacing_squared = spacing * spacing;
    for (std::size_t i = 0; i < poses.size(); ++i) {
        for (std::size_t j = i + 1; j < poses.size(); ++j) {
            const float dx  = poses[i].position.x - poses[j].position.x;
            const float dz  = poses[i].position.z - poses[j].position.z;
            const float dxx = dx * dx;
            const float dzz = dz * dz;
            const float dd  = dxx + dzz;
            ASSERT_GE(dd, spacing_squared) << i << " " << j;
        }
    }
}

TEST(Kit_layout_values, scatter_text_round_trips_and_old_texts_read)
{
    Kit_layout_pattern pattern = make_scatter(Repeat_scatter_region::disc, 17, 0.75f);
    pattern.scatter_mode = Kit_scatter_mode::density;
    pattern.density      = 0.3f;
    const std::string text = editor::to_canonical_text(pattern);
    Kit_layout_pattern read;
    ASSERT_EQ(editor::parse_layout_pattern(text, read), "");
    EXPECT_EQ(read, pattern);
    EXPECT_EQ(editor::to_canonical_text(read), text);

    // A text written before scatter existed lacks its keys and reads with the scatter defaults.
    nlohmann::json old_text = nlohmann::json::parse(editor::to_canonical_text(Kit_layout_pattern{}));
    for (const char* key : {"scatter_mode", "density", "region", "size_x", "size_z", "region_radius", "min_spacing", "yaw_min", "yaw_max"}) {
        ASSERT_EQ(old_text.erase(key), 1u) << key;
    }
    Kit_layout_pattern old_read = pattern;
    ASSERT_EQ(editor::parse_layout_pattern(old_text.dump(), old_read), "");
    EXPECT_EQ(old_read, Kit_layout_pattern{});

    // The scatter values ride a kind change.
    Kit_layout_pattern line = pattern;
    line.kind = Repeat_pattern_kind::line;
    ASSERT_EQ(editor::parse_layout_pattern(editor::to_canonical_text(line), read), "");
    read.kind = Repeat_pattern_kind::scatter;
    EXPECT_EQ(read, pattern);

    nlohmann::json bad = nlohmann::json::parse(text);
    bad["region"] = "square";
    EXPECT_NE(editor::parse_layout_pattern(bad.dump(), read).find("unknown scatter region"), std::string::npos);
}

TEST(Kit_layout_values, scatter_is_deterministic_and_prefix_stable)
{
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    const Kit_layout_pattern pattern = make_scatter(Repeat_scatter_region::rectangle, 20, 1.0f);
    ASSERT_EQ(run_of_seeded(pattern, 7, run, poses), "");
    ASSERT_EQ(poses.size(), 20u);
    EXPECT_EQ(run.count, 20u);
    EXPECT_EQ(run.requested, 20u);
    // The first points of seed 7, the same bits on every platform.
    const char* const expected[3][2] = {
        {"0x1.291acap+1", "-0x1.35d6ccp+1"}, {"0x1.0a486ep+2", "0x1.5b51b8p-1"}, {"0x1.c0fd4p+0", "-0x1.37ef5cp+0"}
    };
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(editor::format_hexfloat(poses[i].position.x), expected[i][0]) << i;
        EXPECT_EQ(editor::format_hexfloat(poses[i].position.z), expected[i][1]) << i;
        EXPECT_EQ(poses[i].position.y, 0.0f);
    }
    std::vector<Repeat_pose> again;
    ASSERT_EQ(run_of_seeded(pattern, 7, run, again), "");
    for (std::size_t i = 0; i < poses.size(); ++i) {
        EXPECT_EQ(poses[i].position, again[i].position);
        EXPECT_EQ(poses[i].rotation, again[i].rotation);
    }
    // More pieces keep the first ones.
    Kit_layout_pattern more = pattern;
    more.count = 30;
    ASSERT_EQ(run_of_seeded(more, 7, run, again), "");
    ASSERT_EQ(again.size(), 30u);
    for (std::size_t i = 0; i < poses.size(); ++i) {
        EXPECT_EQ(poses[i].position, again[i].position) << i;
    }
    // Another seed places them elsewhere.
    ASSERT_EQ(run_of_seeded(pattern, 8, run, again), "");
    EXPECT_NE(poses[0].position, again[0].position);
}

TEST(Kit_layout_values, scatter_holds_spacing_region_and_yaw)
{
    for (const Repeat_scatter_region region : {Repeat_scatter_region::rectangle, Repeat_scatter_region::disc}) {
        Kit_layout_run           run;
        std::vector<Repeat_pose> poses;
        const Kit_layout_pattern pattern = make_scatter(region, 25, 0.9f);
        ASSERT_EQ(run_of_seeded(pattern, 3, run, poses), "");
        ASSERT_EQ(poses.size(), 25u);
        expect_spacing_holds(poses, pattern.min_spacing);
        for (const Repeat_pose& pose : poses) {
            if (region == Repeat_scatter_region::disc) {
                EXPECT_LE((pose.position.x * pose.position.x) + (pose.position.z * pose.position.z), (5.0f * 5.0f) + c_tolerance);
            } else {
                EXPECT_LE(std::abs(pose.position.x), 5.0f);
                EXPECT_LE(std::abs(pose.position.z), 3.0f);
            }
            // The yaw about +y: the rotated x axis's heading.
            const glm::vec3 x_axis = pose.rotation * glm::vec3{1.0f, 0.0f, 0.0f};
            EXPECT_NEAR(x_axis.y, 0.0f, c_tolerance);
            const float yaw = glm::degrees(std::atan2(-x_axis.z, x_axis.x));
            EXPECT_GE(yaw, -30.0f - 1.0e-3f);
            EXPECT_LE(yaw,  45.0f + 1.0e-3f);
        }
    }
}

TEST(Kit_layout_values, scatter_density_mode_limit_and_what_fits)
{
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    Kit_layout_pattern pattern = make_scatter(Repeat_scatter_region::rectangle, 1, 0.0f);
    pattern.scatter_mode = Kit_scatter_mode::density;
    pattern.density      = 0.25f; // 60 square meters: 15 pieces
    ASSERT_EQ(run_of_seeded(pattern, 1, run, poses), "");
    EXPECT_EQ(poses.size(), 15u);
    EXPECT_EQ(run.requested, 15u);
    EXPECT_NEAR(run.space, 60.0f, c_tolerance);
    pattern.density = 0.001f; // rounds to none: element 0 alone
    ASSERT_EQ(run_of_seeded(pattern, 1, run, poses), "");
    EXPECT_EQ(poses.size(), 1u);
    pattern.density = 20.0f; // 1200 pieces
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 1, sizes_of(0.0f), nullptr, run, nullptr).find("more than the 1024"), std::string::npos);
    pattern.density = 0.0f;
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 1, sizes_of(0.0f), nullptr, run, nullptr), "");

    // 2 m apart over 10 x 6 m: far fewer than 200 fit; the ones that do are placed and the request is reported.
    pattern.scatter_mode = Kit_scatter_mode::count;
    pattern.count        = 200;
    pattern.min_spacing  = 2.0f;
    ASSERT_EQ(run_of_seeded(pattern, 1, run, poses), "");
    EXPECT_EQ(run.requested, 200u);
    EXPECT_EQ(run.count, poses.size());
    EXPECT_LT(poses.size(), 30u);
    EXPECT_GT(poses.size(), 5u);
    expect_spacing_holds(poses, 2.0f);

    pattern.count = 1025;
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 1, sizes_of(0.0f), nullptr, run, nullptr), "");
    pattern.count   = 3;
    pattern.yaw_min = 50.0f;
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 1, sizes_of(0.0f), nullptr, run, nullptr).find("yaw"), std::string::npos);
    pattern.yaw_min = 0.0f;
    pattern.size_x  = 0.0f;
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 1, sizes_of(0.0f), nullptr, run, nullptr), "");
    // A line ignores the scatter values; a scatter's count mode ignores the line's gap mode.
    pattern.size_x     = 10.0f;
    pattern.count_mode = Kit_layout_count_mode::gap;
    ASSERT_EQ(run_of_seeded(pattern, 1, run, poses), "");
    EXPECT_EQ(poses.size(), 3u);
}

TEST(Kit_layout_values, scatter_crowded_runs_are_pinned)
{
    // Most candidates are rejected here, so the count, the last point and the hash pin every accept decision.
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    const Kit_layout_pattern rectangle = make_scatter(Repeat_scatter_region::rectangle, 200, 2.0f);
    ASSERT_EQ(run_of_seeded(rectangle, 1, run, poses), "");
    EXPECT_EQ(poses.size(), 15u);
    EXPECT_EQ(editor::format_hexfloat(poses.back().position.x), "0x1.539b6p-3");
    EXPECT_EQ(editor::format_hexfloat(poses.back().position.z), "0x1.190b4cp+1");
    EXPECT_EQ(position_bits_hash(poses), 6115425081161868583ull);
    const Kit_layout_pattern disc = make_scatter(Repeat_scatter_region::disc, 200, 1.5f);
    ASSERT_EQ(run_of_seeded(disc, 4, run, poses), "");
    EXPECT_EQ(poses.size(), 29u);
    EXPECT_EQ(editor::format_hexfloat(poses.back().position.x), "-0x1.56055cp+1");
    EXPECT_EQ(editor::format_hexfloat(poses.back().position.z), "0x1.0bc548p+2");
    EXPECT_EQ(position_bits_hash(poses), 2821524147864653875ull);
    // Resolving without poses and expanding the run gives the same points.
    std::vector<Repeat_pose> expanded;
    const Kit_layout_item_sizes sizes = sizes_of(0.0f);
    ASSERT_EQ(editor::resolve_layout_pattern(disc, 4, sizes, nullptr, run, nullptr), "");
    ASSERT_EQ(editor::expand_repeat(run.pattern, Repeat_pose{}, expanded), "");
    EXPECT_EQ(position_bits_hash(expanded), position_bits_hash(poses));
}

TEST(Kit_layout_values, scatter_edge_values)
{
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    // Spacing 0: every candidate is taken.
    ASSERT_EQ(run_of_seeded(make_scatter(Repeat_scatter_region::rectangle, 50, 0.0f), 2, run, poses), "");
    EXPECT_EQ(poses.size(), 50u);
    // A spacing wider than the region holds one piece.
    ASSERT_EQ(run_of_seeded(make_scatter(Repeat_scatter_region::rectangle, 5, 20.0f), 2, run, poses), "");
    EXPECT_EQ(poses.size(), 1u);
    EXPECT_EQ(run.requested, 5u);
    ASSERT_EQ(run_of_seeded(make_scatter(Repeat_scatter_region::disc, 1, 1.0f), 2, run, poses), "");
    EXPECT_EQ(poses.size(), 1u);
    // A yaw outside -360 - 360 is refused.
    Kit_layout_pattern wide = make_scatter(Repeat_scatter_region::disc, 3, 1.0f);
    wide.yaw_max = 400.0f;
    EXPECT_NE(editor::resolve_layout_pattern(wide, 2, sizes_of(0.0f), nullptr, run, nullptr).find("-360 - 360"), std::string::npos);
    // A scatter key of the wrong type is refused as any key of the pattern is.
    nlohmann::json text = nlohmann::json::parse(editor::to_canonical_text(Kit_layout_pattern{}));
    text["density"] = 0.5;
    Kit_layout_pattern read;
    EXPECT_NE(editor::parse_layout_pattern(text.dump(), read).find("'density' is not a hexadecimal float string"), std::string::npos);
}

// Every pair's footprints at least the spacing apart, with the sampler's own float arithmetic.
void expect_footprint_gap_holds(const std::vector<Repeat_pose>& poses, const Kit_layout_item_sizes& sizes, const float spacing)
{
    for (std::size_t i = 0; i < poses.size(); ++i) {
        for (std::size_t j = i + 1; j < poses.size(); ++j) {
            const float dx       = poses[i].position.x - poses[j].position.x;
            const float dz       = poses[i].position.z - poses[j].position.z;
            const float dxx      = dx * dx;
            const float dzz      = dz * dz;
            const float dd       = dxx + dzz;
            const float radii    = sizes.items[poses[i].item].radius + sizes.items[poses[j].item].radius;
            const float limit    = radii + spacing;
            const float limit_sq = limit * limit;
            ASSERT_GE(dd, limit_sq) << i << " " << j;
        }
    }
}

TEST(Kit_layout_values, item_weight_text)
{
    std::vector<Kit_layout_item> items{
        Kit_layout_item{.shape = "basic.block", .parameters = "{\"seed\":0}", .materials = {}, .weight = 1},
        Kit_layout_item{.shape = "nature.tree", .parameters = "{\"seed\":0}", .materials = {}, .weight = 0},
        Kit_layout_item{.shape = "nature.rock", .parameters = "{\"seed\":0}", .materials = {}, .weight = 1000}
    };
    const std::string text = editor::to_canonical_text(std::span<const Kit_layout_item>{items});
    // Weight 1 is left out, so a text written before weights is canonical as it stands.
    const nlohmann::json list = nlohmann::json::parse(text);
    EXPECT_FALSE(list[0].contains("weight"));
    EXPECT_EQ(list[1].at("weight"), 0);
    EXPECT_EQ(list[2].at("weight"), 1000);
    std::vector<Kit_layout_item> read;
    ASSERT_EQ(editor::parse_layout_items(text, read), "");
    EXPECT_EQ(read, items);
    EXPECT_EQ(editor::to_canonical_text(std::span<const Kit_layout_item>{read}), text);
    EXPECT_NE(editor::parse_layout_items("[{\"shape\":\"basic.block\",\"parameters\":{},\"materials\":[],\"weight\":1001}]", read).find("weight"), std::string::npos);
    EXPECT_NE(editor::parse_layout_items("[{\"shape\":\"basic.block\",\"parameters\":{},\"materials\":[],\"weight\":-1}]", read).find("weight"), std::string::npos);
    EXPECT_NE(editor::parse_layout_items("[{\"shape\":\"basic.block\",\"parameters\":{},\"materials\":[],\"weight\":1.5}]", read).find("weight"), std::string::npos);
}

TEST(Kit_layout_values, scatter_item_pick_is_an_exact_integer_pick)
{
    const std::vector<Repeat_scatter_item> items{{.weight = 1, .radius = 0.0f}, {.weight = 0, .radius = 0.0f}, {.weight = 3, .radius = 0.0f}};
    // (draw * 4) >> 32: the quarters of the draw range.
    EXPECT_EQ(editor::pick_scatter_item(items, 4, 0u), 0u);
    EXPECT_EQ(editor::pick_scatter_item(items, 4, 0x3fffffffu), 0u);
    EXPECT_EQ(editor::pick_scatter_item(items, 4, 0x40000000u), 2u);
    EXPECT_EQ(editor::pick_scatter_item(items, 4, 0xffffffffu), 2u);
    // W = 1: the single item at both ends of the draw range.
    const std::vector<Repeat_scatter_item> single{{.weight = 1, .radius = 0.0f}};
    EXPECT_EQ(editor::pick_scatter_item(single, 1, 0u), 0u);
    EXPECT_EQ(editor::pick_scatter_item(single, 1, 0xffffffffu), 0u);
    // A weight 0 item first or last is never picked, the lowest and highest draws included.
    const std::vector<Repeat_scatter_item> zero_first{{.weight = 0, .radius = 0.0f}, {.weight = 2, .radius = 0.0f}, {.weight = 1, .radius = 0.0f}};
    EXPECT_EQ(editor::pick_scatter_item(zero_first, 3, 0u), 1u);
    EXPECT_EQ(editor::pick_scatter_item(zero_first, 3, 0xaaaaaaaau), 1u); // target 1
    EXPECT_EQ(editor::pick_scatter_item(zero_first, 3, 0xaaaaaaabu), 2u); // target 2
    EXPECT_EQ(editor::pick_scatter_item(zero_first, 3, 0xffffffffu), 2u);
    const std::vector<Repeat_scatter_item> zero_last{{.weight = 2, .radius = 0.0f}, {.weight = 1, .radius = 0.0f}, {.weight = 0, .radius = 0.0f}};
    EXPECT_EQ(editor::pick_scatter_item(zero_last, 3, 0u), 0u);
    EXPECT_EQ(editor::pick_scatter_item(zero_last, 3, 0xffffffffu), 1u);
}

TEST(Kit_layout_values, scatter_picks_items_by_weight_from_the_fourth_draw)
{
    // Spacing 0 and no footprints take every candidate, so pose i is candidate i and its item the pick of draw 4 i + 3.
    Kit_layout_item_sizes sizes;
    sizes.items = {{.weight = 1, .radius = 0.0f}, {.weight = 0, .radius = 0.0f}, {.weight = 3, .radius = 0.0f}};
    const Kit_layout_pattern pattern = make_scatter(Repeat_scatter_region::rectangle, 400, 0.0f);
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(editor::resolve_layout_pattern(pattern, 9, sizes, nullptr, run, &poses), "");
    ASSERT_EQ(poses.size(), 400u);
    editor::Kit_random random{9, "layout scatter"};
    std::size_t counts[3] = {0, 0, 0};
    for (const Repeat_pose& pose : poses) {
        static_cast<void>(random.next_u32());
        static_cast<void>(random.next_u32());
        static_cast<void>(random.next_u32());
        const uint32_t draw = random.next_u32();
        ASSERT_EQ(pose.item, editor::pick_scatter_item(sizes.items, 4, draw));
        ++counts[pose.item];
    }
    EXPECT_EQ(counts[1], 0u);
    EXPECT_GT(counts[2], 2 * counts[0]);

    // The other kinds take item i modulo the item count, whatever the weights.
    Kit_layout_pattern line;
    line.count = 5;
    ASSERT_EQ(editor::resolve_layout_pattern(line, 9, sizes, nullptr, run, &poses), "");
    ASSERT_EQ(poses.size(), 5u);
    for (std::size_t i = 0; i < poses.size(); ++i) {
        EXPECT_EQ(poses[i].item, i % 3);
    }

    // Every weight 0 is refused; a line does not read the weights.
    sizes.items[0].weight = 0;
    sizes.items[2].weight = 0;
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 9, sizes, nullptr, run, nullptr).find("weight is 0"), std::string::npos);
    EXPECT_EQ(editor::resolve_layout_pattern(line, 9, sizes, nullptr, run, nullptr), "");
}

TEST(Kit_layout_values, scatter_spacing_is_the_gap_between_footprints)
{
    // The circle about the vertical axis around the xz rectangle of the bounds.
    const erhe::math::Aabb bounds{glm::vec3{-1.0f, 0.0f, -0.5f}, glm::vec3{0.5f, 2.0f, 0.25f}};
    EXPECT_EQ(editor::get_scatter_footprint_radius(bounds), std::sqrt(1.25f));
    EXPECT_EQ(editor::get_scatter_footprint_radius(erhe::math::Aabb{}), 0.0f);

    Kit_layout_item_sizes sizes;
    sizes.items = {{.weight = 1, .radius = 0.25f}, {.weight = 1, .radius = 1.0f}};
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    for (const float spacing : {0.0f, 0.5f}) {
        for (const Repeat_scatter_region region : {Repeat_scatter_region::rectangle, Repeat_scatter_region::disc}) {
            const Kit_layout_pattern pattern = make_scatter(region, 200, spacing);
            ASSERT_EQ(editor::resolve_layout_pattern(pattern, 5, sizes, nullptr, run, &poses), "");
            EXPECT_LT(poses.size(), 200u);
            EXPECT_GT(poses.size(), 5u);
            EXPECT_EQ(run.requested, 200u);
            expect_footprint_gap_holds(poses, sizes, spacing);
            std::size_t wide = 0;
            for (const Repeat_pose& pose : poses) {
                wide += (pose.item == 1) ? 1 : 0;
            }
            EXPECT_GT(wide, 0u);
            EXPECT_LT(wide, poses.size());
        }
    }
    // The crowded run of the mixed items pinned by its count and a hash over every position's bits.
    ASSERT_EQ(editor::resolve_layout_pattern(make_scatter(Repeat_scatter_region::rectangle, 200, 0.5f), 5, sizes, nullptr, run, &poses), "");
    EXPECT_EQ(poses.size(), 36u);
    EXPECT_EQ(position_bits_hash(poses), 5671890562682965881ull);
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


TEST(Kit_layout_values, segments_text_and_resolve)
{
    // The kind reads and writes as 'segments'; a pattern of another kind keeps its canonical text.
    Kit_layout_pattern pattern;
    pattern.kind        = Repeat_pattern_kind::segments;
    pattern.orientation = Repeat_path_orientation::tangent;
    const std::string text = editor::to_canonical_text(pattern);
    EXPECT_NE(text.find("\"kind\":\"segments\""), std::string::npos) << text;
    Kit_layout_pattern read;
    ASSERT_EQ(editor::parse_layout_pattern(text, read), "");
    EXPECT_EQ(read, pattern);
    EXPECT_EQ(editor::to_canonical_text(Kit_layout_pattern{}), std::string{c_default_pattern_text});

    // Each item's own x bounds: the run is one element per segment, the space the chords' sum.
    const erhe::math::Curve_path_table room = make_table(
        corner_path({{0.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 3.0f}}, erhe::math::Curve_closure::closed)
    );
    Kit_layout_item_sizes sizes;
    Kit_layout_item wall;
    sizes.add(wall, erhe::math::Aabb{glm::vec3{-1.0f, 0.0f, -0.1f}, glm::vec3{1.0f, 3.0f, 0.1f}});
    ASSERT_EQ(sizes.segment_items.size(), 1u);
    EXPECT_EQ(sizes.segment_items[0].min_x, -1.0f);
    EXPECT_EQ(sizes.segment_items[0].max_x, 1.0f);
    pattern.orientation = Repeat_path_orientation::upright;
    pattern.count_mode  = Kit_layout_count_mode::gap; // not used by segments
    pattern.count       = 99;
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(editor::resolve_layout_pattern(pattern, 0, sizes, &room, run, &poses), "");
    ASSERT_EQ(run.count, 3u);
    ASSERT_EQ(poses.size(), 3u);
    EXPECT_NEAR(run.space, 5.0f + 3.0f + std::sqrt(34.0f), c_tolerance);
    EXPECT_NEAR(poses[0].scale.x, 2.5f, c_tolerance);
    EXPECT_NEAR(poses[1].scale.x, 1.5f, c_tolerance);
    EXPECT_NEAR(poses[0].position.x, 2.5f, c_tolerance);

    // Without poses, and without a curve.
    ASSERT_EQ(editor::resolve_layout_pattern(pattern, 0, sizes, &room, run, nullptr), "");
    EXPECT_EQ(run.count, 3u);
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 0, sizes, nullptr, run, &poses).find("curve"), std::string::npos);
    EXPECT_TRUE(poses.empty());
    EXPECT_TRUE(editor::is_curve_layout_kind(Repeat_pattern_kind::segments));
    EXPECT_TRUE(editor::is_curve_layout_kind(Repeat_pattern_kind::path));
    EXPECT_FALSE(editor::is_curve_layout_kind(Repeat_pattern_kind::line));
}

TEST(Kit_layout_values, stairs_text_and_resolve)
{
    // The kind reads and writes as 'stairs'; default stairs values are left out, so the texts written before them stay canonical.
    Kit_layout_pattern pattern;
    pattern.kind = Repeat_pattern_kind::stairs;
    std::string text = editor::to_canonical_text(pattern);
    EXPECT_NE(text.find("\"kind\":\"stairs\""), std::string::npos) << text;
    EXPECT_EQ(text.find("stairs_width"), std::string::npos) << text;
    Kit_layout_pattern read;
    ASSERT_EQ(editor::parse_layout_pattern(text, read), "");
    EXPECT_EQ(read, pattern);
    EXPECT_EQ(editor::to_canonical_text(Kit_layout_pattern{}), std::string{c_default_pattern_text});
    EXPECT_TRUE(editor::is_curve_layout_kind(Repeat_pattern_kind::stairs));

    // Values off the defaults ride the text, of every kind, bit for bit.
    pattern.stairs          = editor::Stairs_values{.width = 1.7f, .max_rise = 0.17f, .min_tread = 0.29f, .thickness = 0.11f, .tolerance = 0.003f};
    pattern.stairs_material = "Kit Wood";
    text = editor::to_canonical_text(pattern);
    ASSERT_EQ(editor::parse_layout_pattern(text, read), "");
    EXPECT_EQ(read, pattern);
    EXPECT_EQ(editor::to_canonical_text(read), text);
    Kit_layout_pattern path = pattern;
    path.kind = Repeat_pattern_kind::path;
    ASSERT_EQ(editor::parse_layout_pattern(editor::to_canonical_text(path), read), "");
    read.kind = Repeat_pattern_kind::stairs;
    EXPECT_EQ(read, pattern);
    nlohmann::json bad = nlohmann::json::parse(text);
    bad["stairs_width"] = 1.7;
    EXPECT_NE(editor::parse_layout_pattern(bad.dump(), read).find("stairs_width"), std::string::npos);

    // The run is the one generated child at the node's origin, the space the curve's plan length; items are not read.
    const erhe::math::Curve_path_table slope = make_table(corner_path({{0.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 4.0f}}, erhe::math::Curve_closure::open));
    const Kit_layout_item_sizes none;
    Kit_layout_run           run;
    std::vector<Repeat_pose> poses;
    pattern.stairs = editor::Stairs_values{};
    ASSERT_EQ(editor::resolve_layout_pattern(pattern, 0, none, &slope, run, &poses), "");
    EXPECT_EQ(run.count, 1u);
    EXPECT_EQ(run.requested, 1u);
    EXPECT_NEAR(run.space, 5.0f, c_tolerance);
    ASSERT_EQ(poses.size(), 1u);
    EXPECT_EQ(poses[0].position, glm::vec3{0.0f});
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 0, none, nullptr, run, &poses).find("curve"), std::string::npos);
    // A refusal of the fit is the refusal of the run.
    const erhe::math::Curve_path_table steep = make_table(corner_path({{0.0f, 0.0f, 0.0f}, {1.0f, 3.0f, 0.0f}}, erhe::math::Curve_closure::open));
    EXPECT_NE(editor::resolve_layout_pattern(pattern, 0, none, &steep, run, &poses).find("too short"), std::string::npos);

    // The source text names the curve and the values: equal inputs give equal text, any change another.
    const erhe::math::Curve_path curve = corner_path({{0.0f, 0.0f, 0.0f}, {3.0f, 1.0f, 4.0f}}, erhe::math::Curve_closure::open);
    const std::string source = editor::make_stairs_source_text(curve, editor::Stairs_values{});
    EXPECT_EQ(editor::make_stairs_source_text(curve, editor::Stairs_values{}), source);
    erhe::math::Curve_path moved = curve;
    moved.points[1].position.y = std::nextafter(1.0f, 2.0f);
    EXPECT_NE(editor::make_stairs_source_text(moved, editor::Stairs_values{}), source);
    erhe::math::Curve_path handled = curve;
    handled.points[0].out_handle = glm::vec3{0.5f, 0.0f, 0.0f};
    EXPECT_NE(editor::make_stairs_source_text(handled, editor::Stairs_values{}), source);
    EXPECT_NE(editor::make_stairs_source_text(curve, editor::Stairs_values{.tolerance = 0.02f}), source);

    // It reads back bit for bit; a text of another generator version does not, so its staircase is regenerated.
    erhe::math::Curve_path read_path;
    editor::Stairs_values  read_values;
    const editor::Stairs_values odd{.width = 1.7f, .max_rise = 0.17f, .min_tread = 0.29f, .thickness = 0.11f, .tolerance = 0.003f};
    ASSERT_EQ(editor::parse_stairs_source_text(editor::make_stairs_source_text(handled, odd), read_path, read_values), "");
    EXPECT_EQ(read_values, odd);
    EXPECT_EQ(editor::make_stairs_source_text(read_path, read_values), editor::make_stairs_source_text(handled, odd));
    nlohmann::json other = nlohmann::json::parse(source);
    EXPECT_EQ(other.at("generator"), editor::c_stairs_generator_version);
    other["generator"] = editor::c_stairs_generator_version + 1;
    EXPECT_NE(editor::parse_stairs_source_text(other.dump(), read_path, read_values).find("generator version"), std::string::npos);
    EXPECT_NE(other.dump(), source);
    EXPECT_NE(editor::parse_stairs_source_text("{}", read_path, read_values), "");
}

} // anonymous namespace
