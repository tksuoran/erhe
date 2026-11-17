#include "level/surface_base_frame.hpp"
#include "level/surface_snap_search.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// The base face of snap to surface (doc/editor/level_tools.md "Surface placement"): the lines of find_surface_placement()
// over a horizontal floor, built from the same helpers, and the snap search over them.
namespace {

using editor::Bounds_side;
using editor::Surface_base_frame;
using editor::Surface_base_rule;
using editor::Surface_hit_point;
using editor::Surface_line;
using editor::Surface_offset;
using editor::Surface_pose;

const glm::vec3 c_down{0.0f, -1.0f, 0.0f};
constexpr float c_max_distance  = 1.0f;
constexpr float c_min_ray_height = 1.0e-3f; // level_queries.cpp
constexpr float c_bistro_scale  = 0.0146f;

// A Z-up mesh in centimeters: 60 x 40 footprint, 80 tall along local +Z.
const erhe::math::Aabb c_z_up_bounds{.min = glm::vec3{-30.0f, -20.0f, 0.0f}, .max = glm::vec3{30.0f, 20.0f, 80.0f}};
const erhe::math::Aabb c_y_up_bounds{.min = glm::vec3{-0.5f, 0.0f, -0.25f},  .max = glm::vec3{0.5f, 1.2f, 0.25f}};

[[nodiscard]] auto rotation_about(const float degrees, const glm::vec3 axis) -> glm::quat
{
    return glm::angleAxis(glm::radians(degrees), axis);
}

[[nodiscard]] auto get_corners(const erhe::math::Aabb& bounds) -> std::array<glm::vec3, 8>
{
    std::array<glm::vec3, 8> corners{};
    for (std::size_t i = 0; i < 8; ++i) {
        corners[i] = glm::vec3{
            ((i & 1u) != 0) ? bounds.max.x : bounds.min.x,
            ((i & 2u) != 0) ? bounds.max.y : bounds.min.y,
            ((i & 4u) != 0) ? bounds.max.z : bounds.min.z
        };
    }
    return corners;
}

[[nodiscard]] auto world_min_y(const erhe::math::Aabb& bounds, const Surface_pose& pose) -> float
{
    float lowest = std::numeric_limits<float>::max();
    for (const glm::vec3& corner : get_corners(bounds)) {
        lowest = std::min(lowest, editor::get_world_point(pose, corner).y);
    }
    return lowest;
}

class Floor_snap
{
public:
    Surface_base_frame frame;
    Surface_offset     offset;
    float              support_distance{0.0f}; // the support report's distance over the same floor
};

// The snap lines over a floor at floor_y, as find_surface_placement() builds them, and the support report's distance.
[[nodiscard]] auto snap_over_floor(const Surface_base_rule rule, const erhe::math::Aabb& local_bounds, const Surface_pose& pose, const float floor_y) -> Floor_snap
{
    Floor_snap result{};
    result.frame = editor::get_surface_base_frame(rule, pose.rotation, pose.scale, c_down);
    const std::array<glm::vec3, 5> points = editor::get_base_points(editor::get_footprint_bounds(local_bounds, pose.scale, result.frame), result.frame);
    std::array<std::array<Surface_hit_point, 1>, 5> hits{};
    std::array<Surface_line, 5>                     lines{};
    result.support_distance = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < points.size(); ++i) {
        const glm::vec3 base = editor::get_world_point(pose, points[i]);
        const glm::vec3 top  = editor::get_world_point(pose, editor::get_top_point(local_bounds, result.frame, points[i]));
        hits[i][0] = Surface_hit_point{.height = floor_y - base.y, .surface = true, .normal = glm::vec3{0.0f, 1.0f, 0.0f}};
        lines[i]   = Surface_line{.hits = hits[i], .span = std::max(c_min_ray_height, glm::dot(top - base, -c_down))};
        result.support_distance = std::min(result.support_distance, glm::dot(glm::vec3{0.0f, floor_y, 0.0f} - base, c_down));
    }
    result.offset = editor::find_snap_height(lines, c_max_distance);
    return result;
}

[[nodiscard]] auto moved(const Surface_pose& pose, const Surface_offset& offset) -> Surface_pose
{
    Surface_pose result = pose;
    result.position = pose.position - (offset.height * c_down);
    return result;
}

class Expected_face
{
public:
    int         axis;
    Bounds_side side;
};

// Rests on the floor (y 0): answers 0; lifted 0.5 m: drops 0.5 m onto it, and the rest answers 0.
void expect_rests_and_drops(
    const Surface_base_rule            rule,
    const erhe::math::Aabb&            local_bounds,
    const Surface_pose&                start,
    const std::optional<Expected_face> face
)
{
    Surface_pose pose = start;
    pose.position.y -= world_min_y(local_bounds, pose);
    ASSERT_NEAR(world_min_y(local_bounds, pose), 0.0f, 1.0e-5f);

    const Floor_snap resting = snap_over_floor(rule, local_bounds, pose, 0.0f);
    if (face.has_value()) {
        EXPECT_EQ(resting.frame.axis, face.value().axis);
        EXPECT_EQ(resting.frame.side, face.value().side);
    }
    ASSERT_TRUE(resting.offset.found);
    EXPECT_LE(std::abs(resting.offset.height), editor::c_surface_snap_tolerance);
    EXPECT_NEAR(resting.support_distance, 0.0f, 1.0e-5f);

    Surface_pose lifted = pose;
    lifted.position.y += 0.5f;
    const Floor_snap dropping = snap_over_floor(rule, local_bounds, lifted, 0.0f);
    ASSERT_TRUE(dropping.offset.found);
    EXPECT_NEAR(dropping.offset.height, -0.5f, 1.0e-5f);
    EXPECT_NEAR(dropping.support_distance, -dropping.offset.height, 1.0e-6f);
    const Surface_pose dropped = moved(lifted, dropping.offset);
    EXPECT_NEAR(world_min_y(local_bounds, dropped), 0.0f, 1.0e-5f);
    const Floor_snap again = snap_over_floor(rule, local_bounds, dropped, 0.0f);
    ASSERT_TRUE(again.offset.found);
    EXPECT_LE(std::abs(again.offset.height), editor::c_surface_snap_tolerance);
}

[[nodiscard]] auto make_pose(const glm::quat& rotation, const glm::vec3 scale) -> Surface_pose
{
    return Surface_pose{.position = glm::vec3{1.0f, 0.0f, 2.0f}, .rotation = rotation, .scale = scale};
}

// A node's pose as every caller reads it: glm::decompose of the world matrix (Trs_transform), which makes all three scale
// components negative for a mirror and folds the rest into the rotation.
[[nodiscard]] auto decomposed_pose(const glm::quat& rotation, const glm::vec3 scale) -> Surface_pose
{
    const glm::mat4 world = glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 2.0f}) * glm::mat4_cast(rotation) * glm::scale(glm::mat4{1.0f}, scale);
    glm::vec3 out_scale{};
    glm::quat out_rotation{};
    glm::vec3 out_translation{};
    glm::vec3 skew{};
    glm::vec4 perspective{};
    EXPECT_TRUE(glm::decompose(world, out_scale, out_rotation, out_translation, skew, perspective));
    return Surface_pose{.position = out_translation, .rotation = out_rotation, .scale = out_scale};
}

const glm::quat c_z_up_rotation = rotation_about(-90.0f, glm::vec3{1.0f, 0.0f, 0.0f});

TEST(Surface_base_frame, a_z_up_mesh_turned_minus_90_about_x_rests_on_its_min_z_face)
{
    expect_rests_and_drops(Surface_base_rule::from_pose, c_z_up_bounds, make_pose(c_z_up_rotation, glm::vec3{c_bistro_scale}), Expected_face{2, Bounds_side::min});
    expect_rests_and_drops(Surface_base_rule::from_pose, c_z_up_bounds, decomposed_pose(c_z_up_rotation, glm::vec3{c_bistro_scale}), Expected_face{2, Bounds_side::min});
}

TEST(Surface_base_frame, a_z_up_mesh_turned_plus_90_about_x_rests_on_its_max_z_face)
{
    expect_rests_and_drops(
        Surface_base_rule::from_pose, c_z_up_bounds, make_pose(rotation_about(90.0f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{c_bistro_scale}), Expected_face{2, Bounds_side::max}
    );
}

TEST(Surface_base_frame, a_mesh_lying_with_local_x_up_rests_on_its_min_x_face)
{
    expect_rests_and_drops(
        Surface_base_rule::from_pose, c_y_up_bounds, make_pose(rotation_about(90.0f, glm::vec3{0.0f, 0.0f, 1.0f}), glm::vec3{0.5f, 2.0f, 1.0f}), Expected_face{0, Bounds_side::min}
    );
}

// Mirrors along X, Y and Z, read back through glm::decompose; the world box bottom ends on the floor under either rule.
const std::array<glm::vec3, 3> c_mirrors{glm::vec3{-1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, -1.0f, 1.0f}, glm::vec3{1.0f, 1.0f, -1.0f}};

TEST(Surface_base_frame, a_mirrored_z_up_mesh_from_its_decomposed_pose_rests_on_its_world_bottom_face)
{
    for (const glm::vec3& mirror : c_mirrors) {
        expect_rests_and_drops(Surface_base_rule::from_pose, c_z_up_bounds, decomposed_pose(c_z_up_rotation, c_bistro_scale * mirror), std::nullopt);
    }
}

TEST(Surface_base_frame, a_mirrored_kit_piece_from_its_decomposed_pose_rests_on_its_world_bottom_y_face)
{
    const glm::quat yaw = rotation_about(30.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    for (const glm::vec3& mirror : c_mirrors) {
        const Surface_pose pose = decomposed_pose(yaw, mirror);
        // Upright for X and Z mirrors (the authored base below), upside down for the Y mirror (the authored top below).
        const Surface_base_frame frame = editor::get_surface_base_frame(Surface_base_rule::authored_y_up, pose.rotation, pose.scale, c_down);
        EXPECT_EQ(frame.axis, 1);
        EXPECT_LT(editor::get_world_point(pose, editor::get_base_center(c_y_up_bounds, frame)).y, editor::get_world_point(pose, c_y_up_bounds.center()).y);
        expect_rests_and_drops(Surface_base_rule::authored_y_up, c_y_up_bounds, pose, std::nullopt);
        expect_rests_and_drops(Surface_base_rule::from_pose,     c_y_up_bounds, pose, std::nullopt);
    }
    // An X mirror keeps the authored base, local min Y, down: the decomposed scale makes min Y the face the pose turns down.
    const Surface_pose x_mirrored = decomposed_pose(yaw, c_mirrors[0]);
    const Surface_base_frame x_frame = editor::get_surface_base_frame(Surface_base_rule::authored_y_up, x_mirrored.rotation, x_mirrored.scale, c_down);
    EXPECT_NEAR(editor::get_world_point(x_mirrored, editor::get_base_center(c_y_up_bounds, x_frame)).y, editor::get_world_point(x_mirrored, glm::vec3{0.0f, c_y_up_bounds.min.y, 0.0f}).y, 1.0e-5f);
}

// The base points before the base face was chosen: always the local min Y face, the inset on X and Z.
[[nodiscard]] auto y_up_base_points(const erhe::math::Aabb& local_bounds, const glm::vec3 scale) -> std::array<glm::vec3, 5>
{
    const auto local_inset = [](const float extent, const float axis_scale) -> float {
        const float scale_length = std::abs(axis_scale);
        return (scale_length > 0.0f) ? std::min(1.0e-3f / scale_length, 0.5f * extent) : 0.0f;
    };
    const glm::vec3 inset{
        local_inset(local_bounds.max.x - local_bounds.min.x, scale.x),
        0.0f,
        local_inset(local_bounds.max.z - local_bounds.min.z, scale.z)
    };
    const erhe::math::Aabb b{.min = local_bounds.min + inset, .max = local_bounds.max - inset};
    const glm::vec3 center = b.center();
    return std::array<glm::vec3, 5>{
        glm::vec3{center.x, b.min.y, center.z},
        glm::vec3{b.min.x, b.min.y, b.min.z},
        glm::vec3{b.max.x, b.min.y, b.min.z},
        glm::vec3{b.min.x, b.min.y, b.max.z},
        glm::vec3{b.max.x, b.min.y, b.max.z}
    };
}

TEST(Surface_base_frame, upright_and_moderately_tilted_y_up_pieces_keep_the_min_y_base_points_bit_for_bit)
{
    const std::array<glm::quat, 5> rotations{
        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        rotation_about(37.0f, glm::vec3{0.0f, 1.0f, 0.0f}),
        rotation_about(30.0f, glm::vec3{1.0f, 0.0f, 0.0f}),
        rotation_about(-44.0f, glm::vec3{0.0f, 0.0f, 1.0f}),
        rotation_about(20.0f, glm::vec3{0.0f, 1.0f, 0.0f}) * rotation_about(40.0f, glm::vec3{1.0f, 0.0f, 0.0f})
    };
    const std::array<glm::vec3, 4> scales{glm::vec3{1.0f}, glm::vec3{1.5f, 0.5f, 2.0f}, glm::vec3{1.0f, 0.0f, 1.0f}, glm::vec3{-1.0f, 1.0f, 1.0f}};
    for (const glm::quat& rotation : rotations) {
        for (const glm::vec3& scale : scales) {
            for (const Surface_base_rule rule : {Surface_base_rule::authored_y_up, Surface_base_rule::from_pose}) {
                const Surface_base_frame frame = editor::get_surface_base_frame(rule, rotation, scale, c_down);
                EXPECT_EQ(frame.axis, 1);
                EXPECT_EQ(frame.side, Bounds_side::min);
                EXPECT_EQ(frame.up_local, (glm::vec3{0.0f, 1.0f, 0.0f}));
                const std::array<glm::vec3, 5> points   = editor::get_base_points(editor::get_footprint_bounds(c_y_up_bounds, scale, frame), frame);
                const std::array<glm::vec3, 5> expected = y_up_base_points(c_y_up_bounds, scale);
                for (std::size_t i = 0; i < points.size(); ++i) {
                    EXPECT_EQ(points[i], expected[i]);
                    EXPECT_EQ(editor::get_top_point(c_y_up_bounds, frame, points[i]), (glm::vec3{expected[i].x, c_y_up_bounds.max.y, expected[i].z}));
                }
            }
        }
    }
}

TEST(Surface_base_frame, a_mesh_from_its_pose_tilted_past_45_degrees_rests_on_the_face_now_lowest)
{
    const Surface_base_frame frame = editor::get_surface_base_frame(Surface_base_rule::from_pose, rotation_about(60.0f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}, c_down);
    // +60 about X takes local +Z down-ish: the max Z face is the most downward.
    EXPECT_EQ(frame.axis, 2);
    EXPECT_EQ(frame.side, Bounds_side::max);
}

TEST(Surface_base_frame, exact_ties_take_min_y_then_the_fixed_order)
{
    const glm::quat identity{1.0f, 0.0f, 0.0f, 0.0f};
    const Surface_base_frame y_or_z = editor::get_surface_base_frame(Surface_base_rule::from_pose, identity, glm::vec3{1.0f}, glm::normalize(glm::vec3{0.0f, -1.0f, -1.0f}));
    EXPECT_EQ(y_or_z.axis, 1);
    EXPECT_EQ(y_or_z.side, Bounds_side::min);
    const Surface_base_frame x_or_z = editor::get_surface_base_frame(Surface_base_rule::from_pose, identity, glm::vec3{1.0f}, glm::normalize(glm::vec3{-1.0f, 0.0f, -1.0f}));
    EXPECT_EQ(x_or_z.axis, 0);
    EXPECT_EQ(x_or_z.side, Bounds_side::min);
}

TEST(Surface_base_frame, orient_turns_a_z_up_mesh_onto_a_slope_on_its_base_face_and_rests_there)
{
    // A plane through the origin with normal (0, 2, 1) / sqrt(5); the line through the base center meets it along the direction.
    const glm::vec3 normal = glm::normalize(glm::vec3{0.0f, 2.0f, 1.0f});
    const auto line_height = [&normal](const glm::vec3 base) -> float {
        const float plane_y = -((normal.x * base.x) + (normal.z * base.z)) / normal.y;
        return plane_y - base.y;
    };
    const Surface_pose start{
        .position = glm::vec3{0.3f, 1.0f, 0.2f},
        .rotation = rotation_about(25.0f, glm::vec3{0.0f, 1.0f, 0.0f}) * rotation_about(-90.0f, glm::vec3{1.0f, 0.0f, 0.0f}),
        .scale    = glm::vec3{c_bistro_scale}
    };
    const Surface_base_frame frame = editor::get_surface_base_frame(Surface_base_rule::from_pose, start.rotation, start.scale, c_down);
    ASSERT_EQ(frame.axis, 2);
    ASSERT_EQ(frame.side, Bounds_side::min);
    const float        height   = line_height(editor::get_world_point(start, editor::get_base_center(c_z_up_bounds, frame)));
    const Surface_pose oriented = editor::get_oriented_pose(start, c_z_up_bounds, frame, c_down, height, normal);

    const glm::vec3 up = glm::normalize(oriented.rotation * frame.up_local);
    EXPECT_GE(glm::dot(up, normal), 1.0f - 1.0e-6f);
    const glm::vec3 base_center = editor::get_world_point(oriented, editor::get_base_center(c_z_up_bounds, frame));
    EXPECT_NEAR(glm::dot(normal, base_center), 0.0f, 1.0e-5f);
    // Every corner of the base face lies on the slope: the base face, not another face, rests there.
    for (const glm::vec3& point : editor::get_base_points(c_z_up_bounds, frame)) {
        EXPECT_NEAR(glm::dot(normal, editor::get_world_point(oriented, point)), 0.0f, 1.0e-5f);
    }

    // Orienting the rest again: the same base face, no rise, no turn (find_surface_placement() keeps the pose bit for bit).
    const Surface_base_frame again = editor::get_surface_base_frame(Surface_base_rule::from_pose, oriented.rotation, oriented.scale, c_down);
    EXPECT_EQ(again.axis, frame.axis);
    EXPECT_EQ(again.side, frame.side);
    EXPECT_LE(std::abs(line_height(editor::get_world_point(oriented, editor::get_base_center(c_z_up_bounds, again)))), editor::c_surface_snap_tolerance);
    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * again.up_local), normal), 1.0f - 1.0e-6f);
}

TEST(Surface_base_frame, a_kit_piece_keeps_its_min_y_face_at_any_tilt)
{
    for (const float degrees : {60.0f, 89.0f}) {
        const Surface_base_frame frame = editor::get_surface_base_frame(
            Surface_base_rule::authored_y_up, rotation_about(degrees, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}, c_down
        );
        EXPECT_EQ(frame.axis, 1);
        EXPECT_EQ(frame.side, Bounds_side::min);
        EXPECT_EQ(frame.up_local, (glm::vec3{0.0f, 1.0f, 0.0f}));
    }
    // Turned past 90 degrees it is upside down: the max Y face is below.
    const Surface_base_frame over = editor::get_surface_base_frame(
        Surface_base_rule::authored_y_up, rotation_about(135.0f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}, c_down
    );
    EXPECT_EQ(over.axis, 1);
    EXPECT_EQ(over.side, Bounds_side::max);
    // A negative Y scale puts the authored base on top: the max Y face is below, the up axis is still local +Y in the pose.
    const Surface_base_frame flipped = editor::get_surface_base_frame(
        Surface_base_rule::authored_y_up, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, -1.0f, 1.0f}, c_down
    );
    EXPECT_EQ(flipped.axis, 1);
    EXPECT_EQ(flipped.side, Bounds_side::max);
}

TEST(Surface_base_frame, a_kit_piece_oriented_onto_a_60_degree_slope_rests_on_its_min_y_face_and_stays)
{
    // A plane through the origin tilted 60 degrees about X.
    const glm::vec3 normal{0.0f, std::cos(glm::radians(60.0f)), std::sin(glm::radians(60.0f))};
    const auto line_height = [&normal](const glm::vec3 base) -> float {
        const float plane_y = -((normal.x * base.x) + (normal.z * base.z)) / normal.y;
        return plane_y - base.y;
    };
    const Surface_pose start{.position = glm::vec3{0.2f, 3.0f, -0.4f}, .rotation = rotation_about(30.0f, glm::vec3{0.0f, 1.0f, 0.0f}), .scale = glm::vec3{1.0f}};
    const Surface_base_frame frame = editor::get_surface_base_frame(Surface_base_rule::authored_y_up, start.rotation, start.scale, c_down);
    const float        height   = line_height(editor::get_world_point(start, editor::get_base_center(c_y_up_bounds, frame)));
    const Surface_pose oriented = editor::get_oriented_pose(start, c_y_up_bounds, frame, c_down, height, normal);
    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * frame.up_local), normal), 1.0f - 1.0e-6f);
    for (const glm::vec3& point : editor::get_base_points(c_y_up_bounds, frame)) {
        EXPECT_NEAR(glm::dot(normal, editor::get_world_point(oriented, point)), 0.0f, 1.0e-5f);
    }
    // Orienting the rest again keeps the min Y face: no rise, no turn.
    const Surface_base_frame again = editor::get_surface_base_frame(Surface_base_rule::authored_y_up, oriented.rotation, oriented.scale, c_down);
    EXPECT_EQ(again.axis, 1);
    EXPECT_EQ(again.side, Bounds_side::min);
    EXPECT_LE(std::abs(line_height(editor::get_world_point(oriented, editor::get_base_center(c_y_up_bounds, again)))), editor::c_surface_snap_tolerance);
    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * again.up_local), normal), 1.0f - 1.0e-6f);
    // From the pose, the same rest would take a side face as its base.
    const Surface_base_frame from_pose = editor::get_surface_base_frame(Surface_base_rule::from_pose, oriented.rotation, oriented.scale, c_down);
    EXPECT_NE(from_pose.axis, 1);
}

TEST(Surface_base_frame, a_mesh_from_its_pose_oriented_onto_a_60_degree_slope_rests_and_stays)
{
    const glm::vec3 normal{0.0f, std::cos(glm::radians(60.0f)), std::sin(glm::radians(60.0f))};
    const auto line_height = [&normal](const glm::vec3 base) -> float {
        const float plane_y = -((normal.x * base.x) + (normal.z * base.z)) / normal.y;
        return plane_y - base.y;
    };
    class Oriented
    {
    public:
        Surface_base_frame frame;
        float              height;
        Surface_pose       pose;
    };
    // find_surface_placement() with orient over a plane: the line of the face along the direction, then the face that
    // turns least onto the normal, its own line.
    const auto orient = [&](const Surface_pose& start) -> Oriented {
        const Surface_base_frame line_frame = editor::get_surface_base_frame(Surface_base_rule::from_pose, start.rotation, start.scale, c_down);
        const Surface_base_frame frame      = editor::get_orient_frame(Surface_base_rule::from_pose, start.rotation, start.scale, line_frame, normal);
        const float              height     = line_height(editor::get_world_point(start, editor::get_base_center(c_y_up_bounds, frame)));
        return Oriented{.frame = frame, .height = height, .pose = editor::get_oriented_pose(start, c_y_up_bounds, frame, c_down, height, normal)};
    };
    const Surface_pose start{.position = glm::vec3{0.2f, 3.0f, -0.4f}, .rotation = rotation_about(30.0f, glm::vec3{0.0f, 1.0f, 0.0f}), .scale = glm::vec3{1.0f}};
    const Oriented first = orient(start);
    const glm::vec3 up = glm::normalize(first.pose.rotation * first.frame.up_local);
    EXPECT_GE(glm::dot(up, normal), 1.0f - 1.0e-6f);
    for (const glm::vec3& point : editor::get_base_points(c_y_up_bounds, first.frame)) {
        EXPECT_NEAR(glm::dot(normal, editor::get_world_point(first.pose, point)), 0.0f, 1.0e-5f);
    }
    // Twice more from the rest: the same face, no rise, no turn (find_surface_placement() keeps the pose bit for bit).
    Surface_pose pose = first.pose;
    for (int i = 0; i < 2; ++i) {
        const Oriented again = orient(pose);
        EXPECT_EQ(again.frame.axis, first.frame.axis);
        EXPECT_EQ(again.frame.side, first.frame.side);
        EXPECT_LE(std::abs(again.height), editor::c_surface_snap_tolerance);
        EXPECT_GE(glm::dot(glm::normalize(pose.rotation * again.frame.up_local), normal), 1.0f - 1.0e-6f);
        pose = again.pose;
    }
}

} // anonymous namespace
