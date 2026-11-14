#include "level/level_repeat.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace {

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

TEST(Level_repeat, polyline_turns_at_a_corner_and_drops_the_remainder)
{
    Repeat_pattern pattern;
    pattern.kind         = Repeat_pattern_kind::polyline;
    pattern.points       = {{4.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 3.0f}};
    pattern.path_spacing = 2.0f;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(pattern, Repeat_pose{}, poses), "");
    // 7 m of path at 2 m: arc lengths 0, 2, 4 (the corner) and 6; the last 1 m holds no piece.
    ASSERT_EQ(poses.size(), 4u);
    expect_position(poses[0], {0.0f, 0.0f, 0.0f});
    expect_position(poses[1], {2.0f, 0.0f, 0.0f});
    expect_position(poses[2], {4.0f, 0.0f, 0.0f});
    expect_position(poses[3], {4.0f, 0.0f, 2.0f});
    expect_rotation(poses[0], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    expect_rotation(poses[1], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    // The piece at the corner takes the segment that leaves it: x along +z.
    expect_rotation(poses[2], yaw(-90.0f));
    expect_rotation(poses[3], yaw(-90.0f));
    const glm::vec3 x_axis = poses[3].rotation * glm::vec3{1.0f, 0.0f, 0.0f};
    EXPECT_NEAR(x_axis.z, 1.0f, c_tolerance);
}

TEST(Level_repeat, polyline_end_on_a_step_gets_a_piece)
{
    Repeat_pattern pattern;
    pattern.kind         = Repeat_pattern_kind::polyline;
    pattern.points       = {{3.0f, 0.0f, 0.0f}};
    pattern.path_spacing = 1.5f;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(pattern, Repeat_pose{.position = glm::vec3{0.0f}, .rotation = yaw(90.0f)}, poses), "");
    ASSERT_EQ(poses.size(), 3u);
    expect_position(poses[2], {3.0f, 0.0f, 0.0f});
    // The start's x axis (world -z after the yaw) turns onto the path.
    for (const Repeat_pose& pose : poses) {
        expect_rotation(pose, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    }
}

TEST(Level_repeat, polyline_slopes_and_vertical_segments_keep_upright)
{
    Repeat_pattern pattern;
    pattern.kind         = Repeat_pattern_kind::polyline;
    pattern.points       = {{3.0f, 4.0f, 0.0f}};
    pattern.path_spacing = 2.5f;
    std::vector<Repeat_pose> poses;
    ASSERT_EQ(expand(pattern, Repeat_pose{}, poses), "");
    ASSERT_EQ(poses.size(), 3u);
    expect_position(poses[1], {1.5f, 2.0f, 0.0f});
    expect_position(poses[2], {3.0f, 4.0f, 0.0f});
    expect_rotation(poses[1], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});

    // A vertical segment has no heading: its pieces keep the heading they had.
    pattern.points       = {{0.0f, 2.0f, 0.0f}, {2.0f, 2.0f, 0.0f}};
    pattern.path_spacing = 1.0f;
    ASSERT_EQ(expand(pattern, Repeat_pose{.position = glm::vec3{0.0f}, .rotation = yaw(30.0f)}, poses), "");
    ASSERT_EQ(poses.size(), 5u);
    expect_rotation(poses[0], yaw(30.0f));
    expect_rotation(poses[1], yaw(30.0f));
    expect_position(poses[1], {0.0f, 1.0f, 0.0f});
    expect_position(poses[3], {1.0f, 2.0f, 0.0f});
    expect_rotation(poses[2], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    expect_rotation(poses[4], glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
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
    path.kind = Repeat_pattern_kind::polyline;
    EXPECT_NE(refusal(path), "");
    path.points       = {{2000.0f, 0.0f, 0.0f}};
    path.path_spacing = 0.0f;
    EXPECT_NE(refusal(path), "");
    path.path_spacing = 1.0f;
    const std::string path_text = refusal(path);
    EXPECT_NE(path_text.find("2001"), std::string::npos) << path_text;
}

} // anonymous namespace
