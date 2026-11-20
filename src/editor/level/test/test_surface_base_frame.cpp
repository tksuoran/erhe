#include "level/surface_base_frame.hpp"
#include "level/surface_snap_search.hpp"

#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>

// The base face of snap to surface (doc/editor/level_tools.md "Surface placement"): the lines of find_surface_placement()
// over a horizontal floor, built from the same helpers, and the snap search over them.
namespace {

using editor::Bounds_side;
using editor::Surface_base_frame;
using editor::Surface_hit_point;
using editor::Surface_line;
using editor::Surface_offset;
using editor::Surface_pose;

const glm::vec3 c_down{0.0f, -1.0f, 0.0f};
constexpr float c_max_distance  = 1.0f;
constexpr float c_min_ray_height = 1.0e-3f; // level_queries.cpp

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
[[nodiscard]] auto snap_over_floor(const erhe::math::Aabb& local_bounds, const Surface_pose& pose, const float floor_y) -> Floor_snap
{
    Floor_snap result{};
    result.frame = editor::get_surface_base_frame(pose.rotation, pose.scale, c_down);
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
    result.offset = editor::find_snap_height(lines, editor::Surface_search_window{.forward_distance = c_max_distance, .backward_distance = c_max_distance});
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
    const erhe::math::Aabb&            local_bounds,
    const Surface_pose&                start,
    const std::optional<Expected_face> face
)
{
    Surface_pose pose = start;
    pose.position.y -= world_min_y(local_bounds, pose);
    ASSERT_NEAR(world_min_y(local_bounds, pose), 0.0f, 1.0e-5f);

    const Floor_snap resting = snap_over_floor(local_bounds, pose, 0.0f);
    if (face.has_value()) {
        EXPECT_EQ(resting.frame.axis, face.value().axis);
        EXPECT_EQ(resting.frame.side, face.value().side);
    }
    ASSERT_TRUE(resting.offset.found);
    EXPECT_LE(std::abs(resting.offset.height), editor::c_surface_snap_tolerance);
    EXPECT_NEAR(resting.support_distance, 0.0f, 1.0e-5f);

    Surface_pose lifted = pose;
    lifted.position.y += 0.5f;
    const Floor_snap dropping = snap_over_floor(local_bounds, lifted, 0.0f);
    ASSERT_TRUE(dropping.offset.found);
    EXPECT_NEAR(dropping.offset.height, -0.5f, 1.0e-5f);
    EXPECT_NEAR(dropping.support_distance, -dropping.offset.height, 1.0e-6f);
    const Surface_pose dropped = moved(lifted, dropping.offset);
    EXPECT_NEAR(world_min_y(local_bounds, dropped), 0.0f, 1.0e-5f);
    const Floor_snap again = snap_over_floor(local_bounds, dropped, 0.0f);
    ASSERT_TRUE(again.offset.found);
    EXPECT_LE(std::abs(again.offset.height), editor::c_surface_snap_tolerance);
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



// Mirrors along X, Y and Z, read back through glm::decompose.
const std::array<glm::vec3, 3> c_mirrors{glm::vec3{-1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, -1.0f, 1.0f}, glm::vec3{1.0f, 1.0f, -1.0f}};

TEST(Surface_base_frame, a_mirrored_piece_from_its_decomposed_pose_rests_on_its_world_bottom_y_face)
{
    const glm::quat yaw = rotation_about(30.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    for (const glm::vec3& mirror : c_mirrors) {
        const Surface_pose pose = decomposed_pose(yaw, mirror);
        // Upright for X and Z mirrors (the authored base below), upside down for the Y mirror (the authored top below).
        const Surface_base_frame frame = editor::get_surface_base_frame(pose.rotation, pose.scale, c_down);
        EXPECT_EQ(frame.axis, 1);
        EXPECT_LT(editor::get_world_point(pose, editor::get_base_center(c_y_up_bounds, frame)).y, editor::get_world_point(pose, c_y_up_bounds.center()).y);
        expect_rests_and_drops(c_y_up_bounds, pose, std::nullopt);
    }
    // An X mirror keeps the authored base, local min Y, down: the decomposed scale makes min Y the face the pose turns down.
    const Surface_pose x_mirrored = decomposed_pose(yaw, c_mirrors[0]);
    const Surface_base_frame x_frame = editor::get_surface_base_frame(x_mirrored.rotation, x_mirrored.scale, c_down);
    EXPECT_NEAR(editor::get_world_point(x_mirrored, editor::get_base_center(c_y_up_bounds, x_frame)).y, editor::get_world_point(x_mirrored, glm::vec3{0.0f, c_y_up_bounds.min.y, 0.0f}).y, 1.0e-5f);
    // Orient's frame is the min Y face, its up axis pointing from it to the max Y face in the world, for every mirror.
    for (const glm::vec3& mirror : c_mirrors) {
        const Surface_pose       pose   = decomposed_pose(yaw, mirror);
        const Surface_base_frame orient = editor::get_orient_base_frame(pose.scale);
        EXPECT_EQ(orient.axis, 1);
        EXPECT_EQ(orient.side, Bounds_side::min);
        const glm::vec3 base_to_top = editor::get_world_point(pose, glm::vec3{0.0f, c_y_up_bounds.max.y, 0.0f}) - editor::get_world_point(pose, glm::vec3{0.0f, c_y_up_bounds.min.y, 0.0f});
        EXPECT_GE(glm::dot(glm::normalize(pose.rotation * orient.up_local), glm::normalize(base_to_top)), 1.0f - 1.0e-5f);
    }
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
            for (const Surface_base_frame& frame : {editor::get_surface_base_frame(rotation, scale, c_down), editor::get_orient_base_frame(scale)}) {
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

TEST(Surface_base_frame, snap_rests_on_the_bounds_face_that_leads_along_the_direction)
{
    // Tilted below 45 degrees the min Y face leads down; past 45 degrees the face the tilt turns down does.
    for (const float degrees : {30.0f, 44.0f}) {
        const Surface_base_frame frame = editor::get_surface_base_frame(rotation_about(degrees, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}, c_down);
        EXPECT_EQ(frame.axis, 1);
        EXPECT_EQ(frame.side, Bounds_side::min);
        EXPECT_EQ(frame.up_local, (glm::vec3{0.0f, 1.0f, 0.0f}));
    }
    // A positive turn about X takes local +Z down.
    const Surface_base_frame steep = editor::get_surface_base_frame(rotation_about(60.0f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}, c_down);
    EXPECT_EQ(steep.axis, 2);
    EXPECT_EQ(steep.side, Bounds_side::max);
    EXPECT_EQ(steep.up_local, (glm::vec3{0.0f, 0.0f, -1.0f}));
    // Turned past 135 degrees it is upside down: snap alone, which does not turn it, rests on the max Y face below.
    const Surface_base_frame over = editor::get_surface_base_frame(rotation_about(150.0f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}, c_down);
    EXPECT_EQ(over.axis, 1);
    EXPECT_EQ(over.side, Bounds_side::max);
    // A negative Y scale puts the authored base on top: the max Y face is below.
    const Surface_base_frame flipped = editor::get_surface_base_frame(glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, -1.0f, 1.0f}, c_down);
    EXPECT_EQ(flipped.axis, 1);
    EXPECT_EQ(flipped.side, Bounds_side::max);
    // Searching up (a ceiling), an upright piece leads with its max Y face: its top rests against the ceiling.
    const Surface_base_frame up = editor::get_surface_base_frame(glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f}, -c_down);
    EXPECT_EQ(up.axis, 1);
    EXPECT_EQ(up.side, Bounds_side::max);
    EXPECT_EQ(up.up_local, (glm::vec3{0.0f, -1.0f, 0.0f}));
    // Searching into a wall, an upright piece rests flush on the side face that leads: -Z into a wall behind it, +X to its right.
    const Surface_base_frame back = editor::get_surface_base_frame(glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f}, glm::vec3{0.0f, 0.0f, -1.0f});
    EXPECT_EQ(back.axis, 2);
    EXPECT_EQ(back.side, Bounds_side::min);
    EXPECT_EQ(back.u, 0);
    EXPECT_EQ(back.v, 1);
    EXPECT_EQ(back.up_local, (glm::vec3{0.0f, 0.0f, 1.0f}));
    const Surface_base_frame right = editor::get_surface_base_frame(glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f});
    EXPECT_EQ(right.axis, 0);
    EXPECT_EQ(right.side, Bounds_side::max);
    // A mirror along X swaps which X face leads.
    const Surface_base_frame mirrored = editor::get_surface_base_frame(glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{-1.0f, 1.0f, 1.0f}, glm::vec3{1.0f, 0.0f, 0.0f});
    EXPECT_EQ(mirrored.axis, 0);
    EXPECT_EQ(mirrored.side, Bounds_side::min);
}

TEST(Surface_base_frame, snap_alone_rests_an_upright_box_flush_against_a_wall)
{
    // A wall plane z = -2 facing +Z; the piece starts with its base center on it, half inside.
    const erhe::math::Aabb   bounds{.min = glm::vec3{-0.5f, 0.0f, -0.5f}, .max = glm::vec3{0.5f, 1.0f, 0.5f}};
    const Surface_pose       pose{.position = glm::vec3{0.0f, 1.0f, -2.0f}};
    const glm::vec3          direction{0.0f, 0.0f, -1.0f};
    const Surface_base_frame frame  = editor::get_surface_base_frame(pose.rotation, pose.scale, direction);
    const std::array<glm::vec3, 5> points = editor::get_base_points(editor::get_footprint_bounds(bounds, pose.scale, frame), frame);
    std::array<std::array<Surface_hit_point, 1>, 5> hits{};
    std::array<Surface_line, 5>                     lines{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        const glm::vec3 base = editor::get_world_point(pose, points[i]);
        const glm::vec3 top  = editor::get_world_point(pose, editor::get_top_point(bounds, frame, points[i]));
        // Heights grow against the direction: the wall face is (base.z - (-2)) along it.
        hits[i][0] = Surface_hit_point{.height = -2.0f - base.z, .surface = true, .normal = glm::vec3{0.0f, 0.0f, 1.0f}};
        lines[i]   = Surface_line{.hits = hits[i], .span = std::max(c_min_ray_height, glm::dot(top - base, -direction))};
    }
    const editor::Surface_offset offset = editor::find_snap_height(lines, editor::Surface_search_window{.forward_distance = 1.0f, .backward_distance = 1.0f});
    ASSERT_TRUE(offset.found);
    const glm::vec3 rest = pose.position - (offset.height * direction);
    EXPECT_NEAR(rest.z, -1.5f, 1.0e-5f); // the -Z face on the wall, the whole piece in front of it
    EXPECT_NEAR(rest.x, 0.0f, 1.0e-6f);
    EXPECT_NEAR(rest.y, 1.0f, 1.0e-6f);
}

TEST(Surface_base_frame, a_piece_oriented_onto_a_60_degree_slope_rests_on_its_min_y_face_and_stays)
{
    // A plane through the origin tilted 60 degrees about X.
    const glm::vec3 normal{0.0f, std::cos(glm::radians(60.0f)), std::sin(glm::radians(60.0f))};
    const auto line_height = [&normal](const glm::vec3 base) -> float {
        const float plane_y = -((normal.x * base.x) + (normal.z * base.z)) / normal.y;
        return plane_y - base.y;
    };
    const Surface_pose start{.position = glm::vec3{0.2f, 3.0f, -0.4f}, .rotation = rotation_about(30.0f, glm::vec3{0.0f, 1.0f, 0.0f}), .scale = glm::vec3{1.0f}};
    const Surface_base_frame frame = editor::get_orient_base_frame(start.scale);
    const float        height   = line_height(editor::get_world_point(start, editor::get_base_center(c_y_up_bounds, frame)));
    const Surface_pose oriented = editor::get_oriented_pose(start, c_y_up_bounds, frame, c_down, height, normal);
    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * frame.up_local), normal), 1.0f - 1.0e-6f);
    for (const glm::vec3& point : editor::get_base_points(c_y_up_bounds, frame)) {
        EXPECT_NEAR(glm::dot(normal, editor::get_world_point(oriented, point)), 0.0f, 1.0e-5f);
    }
    // Orienting the rest again keeps the min Y face: no rise, no turn.
    const Surface_base_frame again = editor::get_orient_base_frame(oriented.scale);
    EXPECT_EQ(again.axis, 1);
    EXPECT_EQ(again.side, Bounds_side::min);
    EXPECT_LE(std::abs(line_height(editor::get_world_point(oriented, editor::get_base_center(c_y_up_bounds, again)))), editor::c_surface_snap_tolerance);
    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * again.up_local), normal), 1.0f - 1.0e-6f);
}

// U51: searching up, orient turns the piece over onto a ceiling, its base against it and its heading (local X) kept.
TEST(Surface_base_frame, orient_up_turns_an_upright_piece_onto_a_ceiling_by_its_base)
{
    const glm::vec3 up_direction{0.0f, 1.0f, 0.0f};
    const glm::vec3 ceiling_normal{0.0f, -1.0f, 0.0f};
    constexpr float ceiling_y = 3.0f;
    const Surface_pose start{.position = glm::vec3{0.5f, 1.0f, -0.25f}, .rotation = rotation_about(30.0f, glm::vec3{0.0f, 1.0f, 0.0f}), .scale = glm::vec3{1.0f}};
    const Surface_base_frame frame       = editor::get_orient_base_frame(start.scale);
    const glm::vec3          base_center = editor::get_world_point(start, editor::get_base_center(c_y_up_bounds, frame));
    // The rise against the direction: negative, the base moves up along it.
    const float              height      = -(ceiling_y - base_center.y);
    const Surface_pose       oriented    = editor::get_oriented_pose(start, c_y_up_bounds, frame, up_direction, height, ceiling_normal);

    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * frame.up_local), ceiling_normal), 1.0f - 1.0e-6f);
    for (const glm::vec3& point : editor::get_base_points(c_y_up_bounds, frame)) {
        EXPECT_NEAR(editor::get_world_point(oriented, point).y, ceiling_y, 1.0e-5f);
    }
    // The piece hangs below the ceiling, its top face 1.2 m under it.
    EXPECT_NEAR(editor::get_world_point(oriented, glm::vec3{0.0f, c_y_up_bounds.max.y, 0.0f}).y, ceiling_y - 1.2f, 1.0e-5f);
    // The base center stays on the vertical line it searched along.
    const glm::vec3 rested_center = editor::get_world_point(oriented, editor::get_base_center(c_y_up_bounds, frame));
    EXPECT_NEAR(rested_center.x, base_center.x, 1.0e-5f);
    EXPECT_NEAR(rested_center.z, base_center.z, 1.0e-5f);
    // The half turn is about the piece's X axis: the heading is kept.
    EXPECT_GE(glm::dot(oriented.rotation * glm::vec3{1.0f, 0.0f, 0.0f}, start.rotation * glm::vec3{1.0f, 0.0f, 0.0f}), 1.0f - 1.0e-5f);
    // The rest is a fixed point: the base on the ceiling, the up axis along its normal.
    const Surface_base_frame again = editor::get_orient_base_frame(oriented.scale);
    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * again.up_local), ceiling_normal), 1.0f - 1.0e-6f);
}

// U51: a piece turned over and oriented down onto a floor stands on its min Y face again.
TEST(Surface_base_frame, orient_turns_an_upside_down_piece_onto_its_min_y_face)
{
    const glm::vec3 floor_normal{0.0f, 1.0f, 0.0f};
    const Surface_pose start{.position = glm::vec3{0.0f, 2.0f, 0.0f}, .rotation = rotation_about(180.0f, glm::vec3{1.0f, 0.0f, 0.0f}), .scale = glm::vec3{1.0f}};
    const Surface_base_frame frame       = editor::get_orient_base_frame(start.scale);
    const glm::vec3          base_center = editor::get_world_point(start, editor::get_base_center(c_y_up_bounds, frame));
    const Surface_pose       oriented    = editor::get_oriented_pose(start, c_y_up_bounds, frame, c_down, -base_center.y, floor_normal);
    EXPECT_GE(glm::dot(glm::normalize(oriented.rotation * frame.up_local), floor_normal), 1.0f - 1.0e-6f);
    for (const glm::vec3& point : editor::get_base_points(c_y_up_bounds, frame)) {
        EXPECT_NEAR(editor::get_world_point(oriented, point).y, 0.0f, 1.0e-5f);
    }
    EXPECT_NEAR(world_min_y(c_y_up_bounds, oriented), 0.0f, 1.0e-5f);
}

// Review of U51: along a wall the turn comes from the start pose and the direction, not from the normal's side of vertical.
TEST(Surface_base_frame, orient_sideways_onto_a_wall_turns_alike_on_either_side_of_vertical)
{
    const glm::vec3    east{1.0f, 0.0f, 0.0f};
    const Surface_pose start{.position = glm::vec3{0.0f, 1.0f, 0.0f}, .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, .scale = glm::vec3{1.0f}};
    const Surface_base_frame frame = editor::get_orient_base_frame(start.scale);
    const Surface_pose above = editor::get_oriented_pose(start, c_y_up_bounds, frame, east, -0.3f, glm::normalize(glm::vec3{-1.0f,  0.001f, 0.0f}));
    const Surface_pose below = editor::get_oriented_pose(start, c_y_up_bounds, frame, east, -0.3f, glm::normalize(glm::vec3{-1.0f, -0.001f, 0.0f}));
    for (const glm::vec3 axis : {glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 1.0f}}) {
        EXPECT_GE(glm::dot(above.rotation * axis, below.rotation * axis), 0.999f);
    }
    // The up axis takes the wall's normal (west) and the base center rests 0.3 m east.
    EXPECT_GE(glm::dot(above.rotation * glm::vec3{0.0f, 1.0f, 0.0f}, glm::normalize(glm::vec3{-1.0f, 0.001f, 0.0f})), 1.0f - 1.0e-5f);
    const glm::vec3 base_center = editor::get_world_point(above, editor::get_base_center(c_y_up_bounds, frame));
    EXPECT_NEAR(base_center.x, 0.3f, 1.0e-5f);
}

// The turned piece stands along the normal: its whole extent must fit, whatever its extent along the direction.
TEST(Surface_base_frame, orient_sideways_checks_the_whole_extent_for_fit)
{
    const erhe::math::Aabb half_meter{.min = glm::vec3{-0.1f, 0.0f, -0.1f}, .max = glm::vec3{0.1f, 0.5f, 0.1f}};
    const glm::vec3        east{1.0f, 0.0f, 0.0f};
    const Surface_pose     start{.position = glm::vec3{0.0f, 1.0f, 0.0f}, .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, .scale = glm::vec3{1.0f}};
    const editor::Orient_search search = editor::get_orient_search(half_meter, start, east, editor::Surface_search_window{.forward_distance = 1.0f, .backward_distance = 0.0f});
    EXPECT_NEAR(search.span, 0.5f, 1.0e-6f);
    EXPECT_EQ(search.window.forward_distance, 1.0f);
    EXPECT_EQ(search.window.backward_distance, 0.0f);
    // The wall 0.2 m east of the base center, a panel 0.4 m off the wall (0.2 m west of the base center).
    const std::array<Surface_hit_point, 2> hits{
        Surface_hit_point{.height = -0.2f, .surface = true,  .normal = glm::vec3{-1.0f, 0.0f, 0.0f}},
        Surface_hit_point{.height =  0.2f, .surface = false, .normal = glm::vec3{ 1.0f, 0.0f, 0.0f}}
    };
    const std::array<Surface_line, 1> blocked{Surface_line{.hits = hits, .span = search.span}};
    EXPECT_FALSE(editor::find_snap_height(blocked, search.window).found);
    // Without the panel the piece mounts on the wall.
    const std::array<Surface_line, 1> free{Surface_line{.hits = std::span<const Surface_hit_point>{hits.data(), 1}, .span = search.span}};
    const Surface_offset mounted = editor::find_snap_height(free, search.window);
    ASSERT_TRUE(mounted.found);
    EXPECT_NEAR(mounted.height, -0.2f, 1.0e-6f);
}

// The window is measured from the Y face leading along the direction: a 2 m piece lying upside down on a floor finds it.
TEST(Surface_base_frame, orient_measures_the_window_from_the_leading_face)
{
    const erhe::math::Aabb two_meters{.min = glm::vec3{-0.2f, 0.0f, -0.2f}, .max = glm::vec3{0.2f, 2.0f, 0.2f}};
    const Surface_pose     start{.position = glm::vec3{0.0f, 2.0f, 0.0f}, .rotation = rotation_about(180.0f, glm::vec3{1.0f, 0.0f, 0.0f}), .scale = glm::vec3{1.0f}};
    const editor::Surface_search_window window{.forward_distance = 1.0f, .backward_distance = 0.0f};
    const editor::Orient_search search = editor::get_orient_search(two_meters, start, c_down, window);
    EXPECT_NEAR(search.base_center.y, 2.0f, 1.0e-5f);
    EXPECT_NEAR(search.window.forward_distance, 3.0f, 1.0e-5f);
    EXPECT_NEAR(search.window.backward_distance, -2.0f, 1.0e-5f);
    const std::array<Surface_hit_point, 1> floor{Surface_hit_point{.height = -2.0f, .surface = true}};
    const std::array<Surface_line, 1> line{Surface_line{.hits = floor, .span = search.span}};
    EXPECT_FALSE(editor::find_snap_height(line, window).found);
    const Surface_offset found = editor::find_snap_height(line, search.window);
    ASSERT_TRUE(found.found);
    EXPECT_NEAR(found.height, -2.0f, 1.0e-5f);
    // An upright piece leads with its -Y face: the window is as given.
    const Surface_pose upright{.position = glm::vec3{0.0f, 0.5f, 0.0f}, .rotation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, .scale = glm::vec3{1.0f}};
    const editor::Orient_search plain = editor::get_orient_search(two_meters, upright, c_down, window);
    EXPECT_EQ(plain.window.forward_distance, 1.0f);
    EXPECT_EQ(plain.window.backward_distance, 0.0f);
}

} // anonymous namespace
