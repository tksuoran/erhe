#include "level/level_repeat.hpp"

#include "erhe_math/curve_path.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

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

} // anonymous namespace
