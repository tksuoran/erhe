#include "level/surface_base_frame.hpp"
#include "level/surface_options.hpp"
#include "level/surface_snap_search.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <span>
#include <vector>

// The Transform tool drag snap of the anchor (U51, doc/editor/level_tools.md "Transform tool"): its one line, its
// direction and its turn, without rays.
namespace {

using editor::Camera_ray_projection;
using editor::Orient_half_turn;
using editor::Point_rest;
using editor::Surface_hit_point;
using editor::Surface_line;
using editor::Surface_offset;
using editor::Surface_search_window;

const glm::vec3 c_down{0.0f, -1.0f, 0.0f};

[[nodiscard]] auto close(const glm::vec3 a, const glm::vec3 b) -> bool
{
    return glm::length(a - b) < 1.0e-5f;
}

[[nodiscard]] auto find_point(const std::vector<Surface_hit_point>& hits, const Surface_search_window window) -> Surface_offset
{
    const Surface_line line{.hits = hits, .span = 0.0f};
    return editor::find_snap_height(std::span<const Surface_line>{&line, 1}, window);
}

[[nodiscard]] auto rest_point(
    const glm::vec3                       point,
    const glm::vec3                       direction,
    const std::vector<Surface_hit_point>& hits,
    const Surface_search_window           window
) -> Point_rest
{
    const Surface_line line{.hits = hits, .span = 0.0f};
    return editor::find_point_rest(point, direction, line, window);
}

TEST(Surface_drag_anchor, the_point_rest_moves_the_point_by_the_height_along_the_direction)
{
    const Surface_search_window window{.forward_distance = 1.0f, .backward_distance = 1.0f};
    const glm::vec3 point{1.0f, 2.0f, 3.0f};
    // A floor 0.4 below: the point drops 0.4 along the direction.
    const Point_rest down = rest_point(point, c_down, {Surface_hit_point{.height = -0.4f, .surface = true}}, window);
    ASSERT_TRUE(down.found);
    EXPECT_TRUE(close(down.position, glm::vec3{1.0f, 1.6f, 3.0f}));
    EXPECT_FLOAT_EQ(down.distance, 0.4f);
    // A surface 0.3 behind the point (it is sunk): it rises against the direction, a negative distance.
    const Point_rest up = rest_point(point, c_down, {Surface_hit_point{.height = 0.3f, .surface = true}}, window);
    ASSERT_TRUE(up.found);
    EXPECT_TRUE(close(up.position, glm::vec3{1.0f, 2.3f, 3.0f}));
    EXPECT_FLOAT_EQ(up.distance, -0.3f);
    // Along an explicit sideways direction: the wall 0.25 ahead along +X.
    const glm::vec3  east{1.0f, 0.0f, 0.0f};
    const Point_rest wall = rest_point(point, east, {Surface_hit_point{.height = -0.25f, .surface = true}}, window);
    ASSERT_TRUE(wall.found);
    EXPECT_TRUE(close(wall.position, glm::vec3{1.25f, 2.0f, 3.0f}));
    // Outside the window, or a back face only: no rest.
    EXPECT_FALSE(rest_point(point, c_down, {Surface_hit_point{.height = -1.5f, .surface = true}}, window).found);
    EXPECT_FALSE(rest_point(point, c_down, {Surface_hit_point{.height = -0.4f, .surface = false}}, window).found);
}

TEST(Surface_drag_anchor, a_point_within_the_tolerance_of_its_rest_stays_bit_for_bit)
{
    const Surface_search_window window{.forward_distance = 1.0f, .backward_distance = 1.0f};
    const glm::vec3  point{0.1f, 0.7f, -0.3f};
    const float      inside = 0.5f * editor::c_surface_snap_tolerance;
    const Point_rest near_rest = rest_point(point, c_down, {Surface_hit_point{.height = -inside, .surface = true}}, window);
    ASSERT_TRUE(near_rest.found);
    EXPECT_EQ(near_rest.position, point);
    EXPECT_EQ(near_rest.distance, 0.0f);
    // Just past the tolerance the point moves.
    const float      outside = 2.0f * editor::c_surface_snap_tolerance;
    const Point_rest far_rest  = rest_point(point, c_down, {Surface_hit_point{.height = -outside, .surface = true}}, window);
    ASSERT_TRUE(far_rest.found);
    EXPECT_NE(far_rest.position, point);
    EXPECT_FLOAT_EQ(far_rest.distance, outside);
}

TEST(Surface_drag_anchor, a_point_line_takes_the_nearest_surface_and_nothing_blocks_it)
{
    // A floor 0.4 below the anchor and the underside of a slab 0.1 above it: a piece would not fit, a point does.
    const std::vector<Surface_hit_point> hits{
        Surface_hit_point{.height = -0.4f, .surface = true,  .normal = glm::vec3{0.0f, 1.0f, 0.0f}},
        Surface_hit_point{.height =  0.1f, .surface = false, .normal = glm::vec3{0.0f, -1.0f, 0.0f}},
        Surface_hit_point{.height =  0.3f, .surface = true,  .normal = glm::vec3{0.0f, 1.0f, 0.0f}}
    };
    const Surface_offset both = find_point(hits, Surface_search_window{.forward_distance = 1.0f, .backward_distance = 1.0f});
    ASSERT_TRUE(both.found);
    EXPECT_FLOAT_EQ(both.height, 0.3f); // the slab top above is nearer than the floor
    const Surface_offset down = find_point(hits, Surface_search_window{.forward_distance = 1.0f, .backward_distance = 0.0f});
    ASSERT_TRUE(down.found);
    EXPECT_FLOAT_EQ(down.height, -0.4f);
    const Surface_offset near_offset = find_point(hits, Surface_search_window{.forward_distance = 0.2f, .backward_distance = 0.2f});
    EXPECT_FALSE(near_offset.found);
}

TEST(Surface_drag_anchor, a_point_on_a_surface_stays_there)
{
    const std::vector<Surface_hit_point> hits{
        Surface_hit_point{.height = 0.0f, .surface = true, .normal = glm::vec3{0.0f, 1.0f, 0.0f}},
        Surface_hit_point{.height = 0.5f, .surface = true, .normal = glm::vec3{0.0f, 1.0f, 0.0f}}
    };
    const Surface_offset offset = find_point(hits, Surface_search_window{.forward_distance = 1.0f, .backward_distance = 1.0f});
    ASSERT_TRUE(offset.found);
    EXPECT_EQ(offset.height, 0.0f);
}

TEST(Surface_drag_anchor, the_camera_ray_runs_from_the_camera_through_the_anchor)
{
    glm::vec3 direction{0.0f};
    ASSERT_TRUE(editor::get_camera_ray_direction(Camera_ray_projection::perspective, glm::vec3{0.0f, 4.0f, 3.0f}, glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{0.0f, 0.0f, 0.0f}, direction));
    EXPECT_TRUE(close(direction, glm::vec3{0.0f, -0.8f, -0.6f}));
    // Orthographic: the view direction, wherever the anchor is.
    ASSERT_TRUE(editor::get_camera_ray_direction(Camera_ray_projection::orthographic, glm::vec3{0.0f, 4.0f, 3.0f}, glm::vec3{0.0f, 0.0f, -2.0f}, glm::vec3{5.0f, 0.0f, 0.0f}, direction));
    EXPECT_TRUE(close(direction, glm::vec3{0.0f, 0.0f, -1.0f}));
    // An anchor at the camera has no ray.
    EXPECT_FALSE(editor::get_camera_ray_direction(Camera_ray_projection::perspective, glm::vec3{1.0f, 2.0f, 3.0f}, glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}, direction));
}

TEST(Surface_drag_anchor, the_axis_rule_holds_an_axis_parallel_to_the_direction_either_sign)
{
    EXPECT_TRUE (editor::is_axis_along_direction(glm::vec3{0.0f, 1.0f, 0.0f}, c_down));
    EXPECT_TRUE (editor::is_axis_along_direction(glm::vec3{0.0f, 2.0f, 0.0f}, c_down));
    EXPECT_FALSE(editor::is_axis_along_direction(glm::vec3{1.0f, 0.0f, 0.0f}, c_down));
    EXPECT_TRUE (editor::is_axis_along_direction(glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{-3.0f, 0.0f, 0.0f}));
    // Five degrees off is not parallel.
    const float angle = glm::radians(5.0f);
    EXPECT_FALSE(editor::is_axis_along_direction(glm::vec3{std::sin(angle), std::cos(angle), 0.0f}, c_down));
}

TEST(Surface_drag_anchor, the_orient_turn_takes_the_up_axis_to_the_normal)
{
    const editor::Surface_base_frame frame = editor::get_orient_base_frame(glm::vec3{1.0f});
    const glm::quat yaw    = glm::angleAxis(glm::radians(30.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::vec3 normal = glm::normalize(glm::vec3{1.0f, 1.0f, 0.0f});
    EXPECT_EQ(editor::get_orient_half_turn(yaw, frame, c_down), Orient_half_turn::off);
    const glm::quat turn   = editor::get_orient_turn(yaw, frame, Orient_half_turn::off, normal);
    EXPECT_TRUE(close(glm::normalize(turn * yaw) * glm::vec3{0.0f, 1.0f, 0.0f}, normal));
    // Searching up under a ceiling: the up axis points along the direction, a half turn first; the base mounts on it.
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    EXPECT_EQ(editor::get_orient_half_turn(yaw, frame, up), Orient_half_turn::on);
    const glm::quat ceiling_turn = editor::get_orient_turn(yaw, frame, Orient_half_turn::on, -up);
    EXPECT_TRUE(close(glm::normalize(ceiling_turn * yaw) * up, -up));
    // A flat floor under an upright piece turns nothing.
    const glm::quat flat = editor::get_orient_turn(yaw, frame, Orient_half_turn::off, up);
    EXPECT_EQ(flat, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
}

TEST(Surface_drag_anchor, a_half_turn_decided_once_keeps_one_roll_across_the_eye_height)
{
    // A camera ray toward a wall facing +X: above eye height it points down, below it up. The half turn of the drag start
    // (above) kept for the drag gives the same turn on both sides; deciding per step would flip the piece.
    const editor::Surface_base_frame frame = editor::get_orient_base_frame(glm::vec3{1.0f});
    const glm::quat start{1.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec3 wall_normal{1.0f, 0.0f, 0.0f};
    const glm::vec3 above = glm::normalize(glm::vec3{-1.0f, -0.2f, 0.0f});
    const glm::vec3 below = glm::normalize(glm::vec3{-1.0f, 0.2f, 0.0f});
    const Orient_half_turn decided = editor::get_orient_half_turn(start, frame, above);
    EXPECT_NE(editor::get_orient_half_turn(start, frame, above), editor::get_orient_half_turn(start, frame, below));
    const glm::quat turn = editor::get_orient_turn(start, frame, decided, wall_normal);
    EXPECT_TRUE(close(glm::normalize(turn * start) * glm::vec3{0.0f, 1.0f, 0.0f}, wall_normal));
    // The per-step decision below eye height gives another roll about the normal: the flip the drag avoids.
    const glm::quat flipped = editor::get_orient_turn(start, frame, editor::get_orient_half_turn(start, frame, below), wall_normal);
    EXPECT_TRUE(close(glm::normalize(flipped * start) * glm::vec3{0.0f, 1.0f, 0.0f}, wall_normal));
    EXPECT_FALSE(close(glm::normalize(flipped * start) * glm::vec3{0.0f, 0.0f, 1.0f}, glm::normalize(turn * start) * glm::vec3{0.0f, 0.0f, 1.0f}));
}

TEST(Surface_drag_anchor, the_rest_rule_names_round_trip_and_default_to_the_leading_face)
{
    EXPECT_EQ(editor::Surface_options{}.rest, Surface_snap_rest::leading_face);
    for (const Surface_snap_rest rest : {Surface_snap_rest::leading_face, Surface_snap_rest::anchor}) {
        Surface_snap_rest parsed = (rest == Surface_snap_rest::anchor) ? Surface_snap_rest::leading_face : Surface_snap_rest::anchor;
        ASSERT_TRUE(from_string(to_string(rest), parsed));
        EXPECT_EQ(parsed, rest);
    }
    Surface_snap_rest unchanged = Surface_snap_rest::anchor;
    EXPECT_FALSE(from_string("base", unchanged));
    EXPECT_EQ(unchanged, Surface_snap_rest::anchor);
}

TEST(Surface_drag_anchor, the_anchor_rest_moves_the_origin_and_keeps_rotation_and_scale)
{
    const Surface_search_window window{.forward_distance = 1.0f, .backward_distance = 1.0f};
    const editor::Surface_pose  start{
        .position = glm::vec3{2.0f, 1.0f, -1.0f},
        .rotation = glm::angleAxis(glm::radians(20.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
        .scale    = glm::vec3{1.5f, 0.5f, 2.0f}
    };
    // A wall 0.3 ahead along +X: the origin lands on its face, the piece is not turned or scaled.
    const glm::vec3                      east{1.0f, 0.0f, 0.0f};
    const std::vector<Surface_hit_point> wall{Surface_hit_point{.height = -0.3f, .surface = true}};
    const editor::Anchor_rest            rest = editor::get_anchor_rest(start, east, Surface_line{.hits = wall, .span = 0.0f}, window);
    ASSERT_TRUE(rest.found);
    EXPECT_TRUE(close(rest.pose.position, glm::vec3{2.3f, 1.0f, -1.0f}));
    EXPECT_EQ(rest.pose.rotation, start.rotation);
    EXPECT_EQ(rest.pose.scale, start.scale);
    EXPECT_FLOAT_EQ(rest.distance, 0.3f);
    // A start within the tolerance of its rest is the rest itself, bit for bit.
    const std::vector<Surface_hit_point> touching{Surface_hit_point{.height = -0.5f * editor::c_surface_snap_tolerance, .surface = true}};
    const editor::Anchor_rest            rested = editor::get_anchor_rest(start, c_down, Surface_line{.hits = touching, .span = 0.0f}, window);
    ASSERT_TRUE(rested.found);
    EXPECT_EQ(rested.pose.position, start.position);
    EXPECT_EQ(rested.pose.rotation, start.rotation);
    EXPECT_EQ(rested.pose.scale, start.scale);
    EXPECT_EQ(rested.distance, 0.0f);
    // Nothing within the window: not found, the start pose kept.
    const std::vector<Surface_hit_point> far_hits{Surface_hit_point{.height = -3.0f, .surface = true}};
    const editor::Anchor_rest            none = editor::get_anchor_rest(start, c_down, Surface_line{.hits = far_hits, .span = 0.0f}, window);
    EXPECT_FALSE(none.found);
    EXPECT_EQ(none.pose.position, start.position);
}

} // anonymous namespace
