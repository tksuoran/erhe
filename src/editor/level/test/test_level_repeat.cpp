#include "level/level_repeat.hpp"

#include "erhe_math/curve_path.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

using editor::Repeat_path_orientation;
using editor::Repeat_pattern;
using editor::Repeat_pattern_kind;
using editor::Repeat_pose;
using editor::Repeat_ring_facing;

constexpr float c_tolerance = 1.0e-4f;

auto yaw(const float degrees) -> glm::quat
{
    return glm::angleAxis(glm::radians(degrees), glm::vec3{0.0f, 1.0f, 0.0f});
}

void expect_position(const Repeat_pose& pose, const glm::vec3 expected)
{
    EXPECT_NEAR(pose.position.x, expected.x, c_tolerance);
    EXPECT_NEAR(pose.position.y, expected.y, c_tolerance);
    EXPECT_NEAR(pose.position.z, expected.z, c_tolerance);
}

void expect_rotation(const Repeat_pose& pose, const glm::quat expected)
{
    // q and -q are the same rotation.
    EXPECT_NEAR(std::abs(glm::dot(pose.rotation, expected)), 1.0f, c_tolerance);
}

auto expand(const Repeat_pattern& pattern, const Repeat_pose& start, std::vector<Repeat_pose>& poses) -> std::string
{
    return editor::expand_repeat(pattern, start, poses);
}

TEST(Level_repeat, line_steps_in_the_start_frame)
{
    Repeat_pattern pattern;
    pattern.kind    = Repeat_pattern_kind::line;
    pattern.count   = 3;
    pattern.spacing = glm::vec3{2.0f, 0.0f, 0.0f};
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(pattern, Repeat_pose{.position = {1.0f, 0.0f, 2.0f}, .rotation = yaw(90.0f)}, poses), "");
    ASSERT_EQ(poses.size(), 3u);
    // The start frame's x axis is world -z after the 90 degree yaw.
    expect_position(poses[0], {1.0f, 0.0f,  2.0f});
    expect_position(poses[1], {1.0f, 0.0f,  0.0f});
    expect_position(poses[2], {1.0f, 0.0f, -2.0f});
    for (const Repeat_pose& pose : poses) {
        expect_rotation(pose, yaw(90.0f));
    }
}

TEST(Level_repeat, grid_rows_along_z_columns_along_x)
{
    Repeat_pattern pattern;
    pattern.kind      = Repeat_pattern_kind::grid;
    pattern.count_x   = 3;
    pattern.count_z   = 2;
    pattern.spacing_x = 2.0f;
    pattern.spacing_z = 1.0f;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(pattern, Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 6u);
    const glm::vec3 expected[6]{{0, 0, 0}, {2, 0, 0}, {4, 0, 0}, {0, 0, 1}, {2, 0, 1}, {4, 0, 1}};
    for (std::size_t i = 0; i < 6; ++i) {
        expect_position(poses[i], expected[i]);
        expect_rotation(poses[i], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    }
}

TEST(Level_repeat, full_ring_faces_outward)
{
    Repeat_pattern pattern;
    pattern.kind   = Repeat_pattern_kind::ring;
    pattern.count  = 4;
    pattern.radius = 2.0f;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(pattern, Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 4u);
    // The center is one radius behind the start (along -z, the start's back).
    const glm::vec3 center{0.0f, 0.0f, -2.0f};
    const glm::vec3 expected[4]{{0, 0, 0}, {2, 0, -2}, {0, 0, -4}, {-2, 0, -2}};
    for (std::size_t i = 0; i < 4; ++i) {
        expect_position(poses[i], expected[i]);
        expect_rotation(poses[i], yaw(90.0f * static_cast<float>(i)));
        const glm::vec3 front   = poses[i].rotation * glm::vec3{0.0f, 0.0f, 1.0f};
        const glm::vec3 outward = glm::normalize(poses[i].position - center);
        EXPECT_NEAR(glm::dot(front, outward), 1.0f, c_tolerance);
    }
}

TEST(Level_repeat, ring_arc_includes_both_ends)
{
    Repeat_pattern pattern;
    pattern.kind        = Repeat_pattern_kind::ring;
    pattern.count       = 3;
    pattern.radius      = 1.0f;
    pattern.arc_degrees = 180.0f;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(pattern, Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 3u);
    expect_position(poses[0], {0.0f, 0.0f,  0.0f});
    expect_position(poses[1], {1.0f, 0.0f, -1.0f});
    expect_position(poses[2], {0.0f, 0.0f, -2.0f});
    expect_rotation(poses[2], yaw(180.0f));

    pattern.facing = Repeat_ring_facing::start_rotation;
    ASSERT_EQ(expand(pattern, Repeat_pose{.position = glm::vec3{0.0f}, .rotation = yaw(30.0f)}, poses), "");
    ASSERT_EQ(poses.size(), 3u);
    for (const Repeat_pose& pose : poses) {
        expect_rotation(pose, yaw(30.0f));
    }
}

// A curve of corner points: a polyline.
auto corner_table(const std::vector<glm::vec3>& positions, const erhe::math::Curve_closure closure) -> erhe::math::Curve_path_table
{
    erhe::math::Curve_path path;
    for (const glm::vec3& position : positions) {
        path.points.push_back(erhe::math::Curve_point{.position = position});
    }
    path.closure = closure;
    erhe::math::Curve_path_table table;
    table.build(path);
    return table;
}

auto path_pattern(const erhe::math::Curve_path_table& table, const int count, const float step, const Repeat_path_orientation orientation) -> Repeat_pattern
{
    Repeat_pattern pattern;
    pattern.kind             = Repeat_pattern_kind::path;
    pattern.count            = count;
    pattern.path             = &table;
    pattern.path_step        = step;
    pattern.path_orientation = orientation;
    return pattern;
}

TEST(Level_repeat, path_turns_at_a_corner_point)
{
    const erhe::math::Curve_path_table table = corner_table({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 3.0f}}, erhe::math::Curve_closure::open);
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(path_pattern(table, 4, 2.0f, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    // Distances 0, 2, 4 (the corner) and 6 of the 7 m L.
    ASSERT_EQ(poses.size(), 4u);
    expect_position(poses[0], {0.0f, 0.0f, 0.0f});
    expect_position(poses[1], {2.0f, 0.0f, 0.0f});
    expect_position(poses[2], {4.0f, 0.0f, 0.0f});
    expect_position(poses[3], {4.0f, 0.0f, 2.0f});
    expect_rotation(poses[0], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    expect_rotation(poses[1], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    // The piece at the corner takes the span that leaves it: x along +z.
    expect_rotation(poses[2], yaw(-90.0f));
    expect_rotation(poses[3], yaw(-90.0f));
    const glm::vec3 x_axis = poses[3].rotation * glm::vec3{1.0f, 0.0f, 0.0f};
    EXPECT_NEAR(x_axis.z, 1.0f, c_tolerance);
    // A distance a rounding short of the corner is the corner's.
    ASSERT_EQ(expand(path_pattern(table, 3, 2.0f - 1.0e-6f, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    expect_rotation(poses[2], yaw(-90.0f));
}

TEST(Level_repeat, path_orientations_on_a_slope_and_a_vertical_span)
{
    const erhe::math::Curve_path_table slope = corner_table({{0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 0.0f}}, erhe::math::Curve_closure::open);
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(path_pattern(slope, 3, 2.5f, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 3u);
    expect_position(poses[1], {1.5f, 2.0f, 0.0f});
    expect_position(poses[2], {3.0f, 4.0f, 0.0f});
    expect_rotation(poses[1], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    // Tangent: the x axis along the slope, pitched about z.
    ASSERT_EQ(expand(path_pattern(slope, 3, 2.5f, Repeat_path_orientation::tangent), Repeat_pose{}, poses), "");
    const glm::quat pitch = glm::angleAxis(std::atan2(4.0f, 3.0f), glm::vec3{0.0f, 0.0f, 1.0f});
    for (const Repeat_pose& pose : poses) {
        expect_rotation(pose, pitch);
    }

    // A vertical span has no heading: its pieces take the heading of the first span that has one.
    const erhe::math::Curve_path_table riser = corner_table({{0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {2.0f, 2.0f, 0.0f}}, erhe::math::Curve_closure::open);
    ASSERT_EQ(expand(path_pattern(riser, 5, 1.0f, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 5u);
    expect_position(poses[1], {0.0f, 1.0f, 0.0f});
    expect_position(poses[3], {1.0f, 2.0f, 0.0f});
    for (const Repeat_pose& pose : poses) {
        expect_rotation(pose, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    }
}

TEST(Level_repeat, path_on_a_closed_curve_and_from_a_start_pose)
{
    const erhe::math::Curve_path_table square = corner_table({{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 2.0f}, {0.0f, 0.0f, 2.0f}}, erhe::math::Curve_closure::closed);
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(path_pattern(square, 4, 2.0f, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 4u);
    expect_position(poses[1], {2.0f, 0.0f, 0.0f});
    expect_position(poses[3], {0.0f, 0.0f, 2.0f});
    expect_rotation(poses[0], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    expect_rotation(poses[1], yaw(-90.0f));
    expect_rotation(poses[2], yaw(180.0f));
    expect_rotation(poses[3], yaw(90.0f));
    // The curve in the start frame: the poses move and turn with it.
    const Repeat_pose start{.position = glm::vec3{10.0f, 0.0f, 0.0f}, .rotation = yaw(90.0f)};
    ASSERT_EQ(expand(path_pattern(square, 4, 2.0f, Repeat_path_orientation::upright), start, poses), "");
    expect_position(poses[1], {10.0f, 0.0f, -2.0f});
    expect_rotation(poses[1], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
}

TEST(Level_repeat, refusals_leave_no_poses)
{
    std::vector<Repeat_pose> poses;
    const auto refusal = [&poses](const Repeat_pattern& pattern) -> std::string {
        poses.push_back(Repeat_pose{});
        const std::string text = editor::expand_repeat(pattern, Repeat_pose{}, poses);
        EXPECT_TRUE(poses.empty()) << text;
        return text;
    };
    Repeat_pattern line;
    line.count = 0;
    EXPECT_NE(refusal(line), "");
    line.count = 1025;
    EXPECT_NE(refusal(line).find("1024"), std::string::npos);
    line.count   = 2;
    line.spacing = glm::vec3{0.0f};
    EXPECT_NE(refusal(line), "");

    Repeat_pattern grid;
    grid.kind    = Repeat_pattern_kind::grid;
    grid.count_x = 33;
    grid.count_z = 32;
    const std::string grid_text = refusal(grid);
    EXPECT_NE(grid_text.find("1056"), std::string::npos) << grid_text;
    EXPECT_NE(grid_text.find("1024"), std::string::npos) << grid_text;

    Repeat_pattern ring;
    ring.kind   = Repeat_pattern_kind::ring;
    ring.radius = 0.0f;
    EXPECT_NE(refusal(ring), "");
    ring.radius      = 1.0f;
    ring.arc_degrees = 0.0f;
    EXPECT_NE(refusal(ring), "");
    ring.arc_degrees = 400.0f;
    EXPECT_NE(refusal(ring), "");

    Repeat_pattern path;
    path.kind = Repeat_pattern_kind::path;
    EXPECT_NE(refusal(path).find("curve"), std::string::npos);
    const erhe::math::Curve_path_table table = corner_table({{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::open);
    path.path      = &table;
    path.path_step = -1.0f;
    EXPECT_NE(refusal(path), "");
    path.path_step = 1.0f;
    path.count     = 1025;
    EXPECT_NE(refusal(path).find("1024"), std::string::npos);
}


auto segments_pattern(const erhe::math::Curve_path_table& table, const std::vector<editor::Repeat_segment_item>& items, const Repeat_path_orientation orientation) -> Repeat_pattern
{
    Repeat_pattern pattern;
    pattern.kind             = Repeat_pattern_kind::segments;
    pattern.path             = &table;
    pattern.path_orientation = orientation;
    pattern.segment_items    = items;
    return pattern;
}

// The world point of a pose's local x coordinate on its x axis.
auto pose_point(const Repeat_pose& pose, const float x) -> glm::vec3
{
    return pose.position + (pose.rotation * glm::vec3{pose.scale.x * x, 0.0f, 0.0f});
}

void expect_near(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, c_tolerance);
    EXPECT_NEAR(actual.y, expected.y, c_tolerance);
    EXPECT_NEAR(actual.z, expected.z, c_tolerance);
}

TEST(Level_repeat, segments_span_each_chord_of_a_closed_room)
{
    const std::vector<glm::vec3> corners{{0.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 4.0f}, {0.0f, 0.0f, 4.0f}};
    const erhe::math::Curve_path_table room = corner_table(corners, erhe::math::Curve_closure::closed);
    // Two items with other x bounds: element i takes item i modulo 2 and spans by its own bounds.
    const std::vector<editor::Repeat_segment_item> items{{.min_x = -0.5f, .max_x = 0.5f}, {.min_x = 0.0f, .max_x = 2.0f}};
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(editor::expand_repeat(segments_pattern(room, items, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 4u);
    const std::array<float, 4> lengths{6.0f, 4.0f, 6.0f, 4.0f};
    for (std::size_t i = 0; i < 4; ++i) {
        const editor::Repeat_segment_item& item = items[i % 2];
        EXPECT_EQ(poses[i].item, i % 2);
        EXPECT_NEAR(poses[i].scale.x, lengths[i] / (item.max_x - item.min_x), c_tolerance);
        EXPECT_EQ(poses[i].scale.y, 1.0f);
        EXPECT_EQ(poses[i].scale.z, 1.0f);
        expect_near(pose_point(poses[i], item.min_x), corners[i]);
        expect_near(pose_point(poses[i], item.max_x), corners[(i + 1) % 4]);
        // Upright: the up axis stays the start frame's.
        expect_near(poses[i].rotation * glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
    }
    // An open curve of the same corners has one segment fewer.
    const erhe::math::Curve_path_table open = corner_table(corners, erhe::math::Curve_closure::open);
    ASSERT_EQ(editor::expand_repeat(segments_pattern(open, items, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    EXPECT_EQ(poses.size(), 3u);
}

TEST(Level_repeat, segments_on_a_slope_and_from_a_start_pose)
{
    const erhe::math::Curve_path_table slope = corner_table({{0.0f, 0.0f, 0.0f}, {3.0f, 4.0f, 0.0f}}, erhe::math::Curve_closure::open);
    const std::vector<editor::Repeat_segment_item> items{{.min_x = 0.0f, .max_x = 1.0f}};
    std::vector<Repeat_pose> poses;
    // Upright spans the plan length at the first point's height.
    ASSERT_EQ(editor::expand_repeat(segments_pattern(slope, items, Repeat_path_orientation::upright), Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 1u);
    EXPECT_NEAR(poses[0].scale.x, 3.0f, c_tolerance);
    expect_near(pose_point(poses[0], 1.0f), glm::vec3{3.0f, 0.0f, 0.0f});
    expect_rotation(poses[0], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    // Tangent spans the chord itself and pitches with it.
    ASSERT_EQ(editor::expand_repeat(segments_pattern(slope, items, Repeat_path_orientation::tangent), Repeat_pose{}, poses), "");
    EXPECT_NEAR(poses[0].scale.x, 5.0f, c_tolerance);
    expect_near(pose_point(poses[0], 1.0f), glm::vec3{3.0f, 4.0f, 0.0f});
    expect_near(poses[0].rotation * glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{-0.8f, 0.6f, 0.0f});
    // A start pose moves and turns the run; the scale stays the local one.
    const Repeat_pose start{.position = glm::vec3{1.0f, 2.0f, 3.0f}, .rotation = yaw(90.0f)};
    ASSERT_EQ(editor::expand_repeat(segments_pattern(slope, items, Repeat_path_orientation::upright), start, poses), "");
    expect_position(poses[0], glm::vec3{1.0f, 2.0f, 3.0f});
    expect_rotation(poses[0], yaw(90.0f));
    EXPECT_NEAR(poses[0].scale.x, 3.0f, c_tolerance);
}

TEST(Level_repeat, segments_refusals_name_the_segment)
{
    const auto refusal = [](const Repeat_pattern& pattern) -> std::string {
        std::vector<Repeat_pose> poses;
        const std::string error = editor::expand_repeat(pattern, Repeat_pose{}, poses);
        EXPECT_TRUE(error.empty() || poses.empty());
        return error;
    };
    const std::vector<editor::Repeat_segment_item> items{{.min_x = 0.0f, .max_x = 1.0f}};
    Repeat_pattern no_curve;
    no_curve.kind          = Repeat_pattern_kind::segments;
    no_curve.segment_items = items;
    EXPECT_NE(refusal(no_curve).find("curve"), std::string::npos);

    const erhe::math::Curve_path_table single = corner_table({{0.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::open);
    EXPECT_NE(refusal(segments_pattern(single, items, Repeat_path_orientation::upright)).find("fewer than 2 points"), std::string::npos);

    const erhe::math::Curve_path_table repeated = corner_table({{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::open);
    const std::string zero = refusal(segments_pattern(repeated, items, Repeat_path_orientation::upright));
    EXPECT_NE(zero.find("segment 1 (points 1 - 2)"), std::string::npos) << zero;

    const erhe::math::Curve_path_table riser = corner_table({{0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}}, erhe::math::Curve_closure::open);
    EXPECT_NE(refusal(segments_pattern(riser, items, Repeat_path_orientation::upright)).find("vertical"), std::string::npos);
    EXPECT_NE(refusal(segments_pattern(riser, items, Repeat_path_orientation::tangent)).find("vertical"), std::string::npos);

    const erhe::math::Curve_path_table line = corner_table({{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::open);
    const std::vector<editor::Repeat_segment_item> flat{{.min_x = 0.5f, .max_x = 0.5f}};
    EXPECT_NE(refusal(segments_pattern(line, flat, Repeat_path_orientation::upright)).find("no extent along x"), std::string::npos);
    EXPECT_NE(refusal(segments_pattern(line, {}, Repeat_path_orientation::upright)).find("item"), std::string::npos);
}

auto corner_path(const std::vector<glm::vec3>& positions, const erhe::math::Curve_closure closure) -> erhe::math::Curve_path
{
    erhe::math::Curve_path path;
    path.closure = closure;
    for (const glm::vec3& position : positions) {
        path.points.push_back(erhe::math::Curve_point{.position = position});
    }
    return path;
}

void expect_vec3(const glm::vec3 actual, const glm::vec3 expected)
{
    EXPECT_NEAR(actual.x, expected.x, c_tolerance);
    EXPECT_NEAR(actual.y, expected.y, c_tolerance);
    EXPECT_NEAR(actual.z, expected.z, c_tolerance);
}

auto plan_heading(const glm::vec3 a, const glm::vec3 b) -> glm::vec2
{
    return glm::normalize(glm::vec2{b.x - a.x, b.z - a.z});
}

auto moved_path(const erhe::math::Curve_path& path, const std::size_t segment, const float offset) -> erhe::math::Curve_path
{
    editor::Segment_move move;
    const std::string refusal = editor::move_curve_segment(path, segment, offset, move);
    EXPECT_TRUE(refusal.empty()) << refusal;
    erhe::math::Curve_path after = path;
    editor::apply_segment_move(move, after);
    return after;
}

TEST(Level_repeat, segment_move_of_a_rectangle_keeps_right_angles)
{
    const erhe::math::Curve_path room = corner_path({{0.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 6.0f}, {0.0f, 0.0f, 6.0f}}, erhe::math::Curve_closure::closed);
    glm::vec3 normal{0.0f};
    EXPECT_TRUE(editor::get_segment_normal(room, 0, normal).empty());
    expect_vec3(normal, {0.0f, 0.0f, 1.0f}); // the right of +x with y up
    // Wall 0 pushed 1 m against its normal (out of the room): walls 1 and 3 grow by 1 m, wall 2 stays.
    const erhe::math::Curve_path after = moved_path(room, 0, -1.0f);
    expect_vec3(after.points[0].position, {0.0f, 0.0f, -1.0f});
    expect_vec3(after.points[1].position, {8.0f, 0.0f, -1.0f});
    expect_vec3(after.points[2].position, {8.0f, 0.0f, 6.0f});
    expect_vec3(after.points[3].position, {0.0f, 0.0f, 6.0f});
    EXPECT_NEAR(glm::distance(after.points[1].position, after.points[2].position), 7.0f, c_tolerance);
    EXPECT_NEAR(glm::distance(after.points[3].position, after.points[0].position), 7.0f, c_tolerance);
    EXPECT_NEAR(glm::distance(after.points[0].position, after.points[1].position), 8.0f, c_tolerance);
}

TEST(Level_repeat, segment_move_slides_along_slanted_neighbours)
{
    // A trapezoid: the end points of the base slide along the slanted sides, whose headings stay; heights are kept.
    const erhe::math::Curve_path trapezoid = corner_path({{0.0f, 0.5f, 0.0f}, {6.0f, 1.5f, 0.0f}, {4.0f, 0.0f, 3.0f}, {1.0f, 0.0f, 3.0f}}, erhe::math::Curve_closure::closed);
    const erhe::math::Curve_path after = moved_path(trapezoid, 0, 1.0f);
    EXPECT_NEAR(after.points[0].position.z, 1.0f, c_tolerance);
    EXPECT_NEAR(after.points[1].position.z, 1.0f, c_tolerance);
    EXPECT_NEAR(after.points[0].position.y, 0.5f, c_tolerance);
    EXPECT_NEAR(after.points[1].position.y, 1.5f, c_tolerance);
    // On the side lines: (0,0)-(1,3) at z = 1 is x = 1/3; (6,0)-(4,3) at z = 1 is x = 6 - 2/3.
    EXPECT_NEAR(after.points[0].position.x, 1.0f / 3.0f, c_tolerance);
    EXPECT_NEAR(after.points[1].position.x, 6.0f - (2.0f / 3.0f), c_tolerance);
    const glm::vec2 side_3 = plan_heading(trapezoid.points[3].position, trapezoid.points[0].position);
    const glm::vec2 side_1 = plan_heading(trapezoid.points[1].position, trapezoid.points[2].position);
    EXPECT_NEAR(glm::dot(plan_heading(after.points[3].position, after.points[0].position), side_3), 1.0f, c_tolerance);
    EXPECT_NEAR(glm::dot(plan_heading(after.points[1].position, after.points[2].position), side_1), 1.0f, c_tolerance);
    expect_vec3(after.points[2].position, trapezoid.points[2].position);
    expect_vec3(after.points[3].position, trapezoid.points[3].position);
}

TEST(Level_repeat, segment_move_of_open_ends_and_parallel_neighbours)
{
    // An open curve of 3 points: the free end of segment 0 moves along the normal, its other end along segment 1.
    const erhe::math::Curve_path open = corner_path({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 2.0f}}, erhe::math::Curve_closure::open);
    const erhe::math::Curve_path after_0 = moved_path(open, 0, 1.0f);
    expect_vec3(after_0.points[0].position, {0.0f, 0.0f, 1.0f});
    expect_vec3(after_0.points[1].position, {5.0f, 0.0f, 1.0f});
    expect_vec3(after_0.points[2].position, open.points[2].position);
    // Segment 1's last point is the open end: it moves along the normal.
    glm::vec3 normal_1{0.0f};
    EXPECT_TRUE(editor::get_segment_normal(open, 1, normal_1).empty());
    const erhe::math::Curve_path after_1 = moved_path(open, 1, 0.5f);
    expect_vec3(after_1.points[2].position, open.points[2].position + (0.5f * normal_1));
    EXPECT_NEAR(after_1.points[1].position.z, 0.0f, c_tolerance); // slid along segment 0's line

    // A straight neighbour (collinear points): parallel, so that end point moves along the normal.
    const erhe::math::Curve_path straight = corner_path({{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::open);
    const erhe::math::Curve_path after_straight = moved_path(straight, 1, 1.0f);
    expect_vec3(after_straight.points[1].position, {2.0f, 0.0f, 1.0f});
    expect_vec3(after_straight.points[2].position, {4.0f, 0.0f, 1.0f});
    expect_vec3(after_straight.points[0].position, {0.0f, 0.0f, 0.0f});
}

TEST(Level_repeat, segment_move_slides_only_beyond_the_parallel_cut_off)
{
    // Segment 1 turns theta from segment 0; point 1 slides along segment 0 only when theta is at least 15 degrees.
    const auto bent = [](const float degrees) -> erhe::math::Curve_path {
        const float theta = glm::radians(degrees);
        return corner_path({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {4.0f + (4.0f * std::cos(theta)), 0.0f, 4.0f * std::sin(theta)}}, erhe::math::Curve_closure::open);
    };
    const erhe::math::Curve_path outside = bent(16.0f);
    const erhe::math::Curve_path slid = moved_path(outside, 1, 0.5f);
    EXPECT_NEAR(slid.points[1].position.z, 0.0f, c_tolerance);
    EXPECT_NEAR(slid.points[1].position.x, 4.0f - (0.5f / std::sin(glm::radians(16.0f))), c_tolerance);
    EXPECT_LE(glm::distance(slid.points[1].position, outside.points[1].position), 0.5f / editor::c_segment_slide_min_sine);

    const erhe::math::Curve_path inside = bent(14.0f);
    glm::vec3 normal{0.0f};
    EXPECT_TRUE(editor::get_segment_normal(inside, 1, normal).empty());
    const erhe::math::Curve_path straight = moved_path(inside, 1, 0.5f);
    expect_vec3(straight.points[1].position, inside.points[1].position + (0.5f * normal));
}

TEST(Level_repeat, segment_move_of_a_closed_triangle_and_a_closed_pair)
{
    // A triangle: both neighbours of segment 0 share point 2, which stays.
    const erhe::math::Curve_path triangle = corner_path({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 3.0f}}, erhe::math::Curve_closure::closed);
    const erhe::math::Curve_path moved_triangle = moved_path(triangle, 0, 1.0f);
    expect_vec3(moved_triangle.points[0].position, {0.0f, 0.0f, 1.0f});
    expect_vec3(moved_triangle.points[1].position, {8.0f / 3.0f, 0.0f, 1.0f});
    expect_vec3(moved_triangle.points[2].position, triangle.points[2].position);

    // Two points closed: the neighbour lies on the segment's own line (parallel), so both ends move along the normal.
    const erhe::math::Curve_path pair = corner_path({{0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}}, erhe::math::Curve_closure::closed);
    const erhe::math::Curve_path moved_0 = moved_path(pair, 0, 1.0f);
    expect_vec3(moved_0.points[0].position, {0.0f, 0.0f, 1.0f});
    expect_vec3(moved_0.points[1].position, {4.0f, 0.0f, 1.0f});
    const erhe::math::Curve_path moved_1 = moved_path(pair, 1, 1.0f); // from point 1 to point 0 along -x: its right is -z
    expect_vec3(moved_1.points[0].position, {0.0f, 0.0f, -1.0f});
    expect_vec3(moved_1.points[1].position, {4.0f, 0.0f, -1.0f});
}

TEST(Level_repeat, segment_move_wraps_the_last_segment_of_a_closed_curve)
{
    const erhe::math::Curve_path room = corner_path({{0.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 6.0f}, {0.0f, 0.0f, 6.0f}}, erhe::math::Curve_closure::closed);
    editor::Segment_move move;
    EXPECT_TRUE(editor::move_curve_segment(room, 3, 2.0f, move).empty());
    EXPECT_EQ(move.first_point, 3u);
    EXPECT_EQ(move.second_point, 0u);
    // Segment 3 runs from (0, 6) to (0, 0) along -z; its right is +x, into the room.
    expect_vec3(move.first,  {2.0f, 0.0f, 6.0f});
    expect_vec3(move.second, {2.0f, 0.0f, 0.0f});

    // Refusals: no such segment, and a segment of no plan length.
    EXPECT_FALSE(editor::move_curve_segment(room, 4, 1.0f, move).empty());
    const erhe::math::Curve_path open_room = corner_path({{0.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 0.0f}, {8.0f, 0.0f, 6.0f}}, erhe::math::Curve_closure::open);
    EXPECT_FALSE(editor::move_curve_segment(open_room, 2, 1.0f, move).empty());
    const erhe::math::Curve_path riser = corner_path({{0.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}}, erhe::math::Curve_closure::open);
    EXPECT_FALSE(editor::move_curve_segment(riser, 0, 1.0f, move).empty());
}

} // anonymous namespace
