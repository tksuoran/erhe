#include "level/kit_connector.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using editor::Kit_connector;
using editor::Kit_join_pose;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::Kit_shape_library;

constexpr float c_tolerance = 1.0e-5f;

auto get_library() -> const Kit_shape_library&
{
    static const Kit_shape_library library;
    return library;
}

auto get_shape(const char* id) -> const Kit_shape_definition&
{
    const Kit_shape_definition* const shape = get_library().find(id);
    EXPECT_NE(shape, nullptr) << id;
    return *shape;
}

auto defaults(const char* id) -> Kit_parameter_values
{
    return editor::make_default_values(get_shape(id), 1.0f, 3.0f);
}

auto connectors_of(const Kit_parameter_values& values) -> std::vector<Kit_connector>
{
    std::vector<Kit_connector> connectors;
    editor::get_shape_connectors(values, connectors);
    return connectors;
}

auto connector(const std::vector<Kit_connector>& connectors, const char* name) -> Kit_connector
{
    const Kit_connector* const found = editor::find_connector(connectors, name);
    EXPECT_NE(found, nullptr) << name;
    return (found != nullptr) ? *found : Kit_connector{};
}

void expect_near(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, c_tolerance);
    EXPECT_NEAR(actual.y, expected.y, c_tolerance);
    EXPECT_NEAR(actual.z, expected.z, c_tolerance);
}

// The piece connector carried by the join pose.
auto joined(const Kit_connector& piece_connector, const Kit_join_pose& pose) -> Kit_connector
{
    return editor::transform_connector(piece_connector, pose.position, pose.rotation);
}

TEST(KitConnector, EveryConnectorFrameIsOrthonormalAndNamesAreFixed)
{
    for (const Kit_shape_definition& shape : get_library().get_shapes()) {
        const Kit_parameter_values       first  = editor::make_default_values(shape, 1.0f, 3.0f);
        Kit_parameter_values             second = editor::make_default_values(shape, 2.0f, 4.0f);
        const std::vector<Kit_connector> a      = connectors_of(first);
        const std::vector<Kit_connector> b      = connectors_of(second);
        ASSERT_EQ(a.size(), b.size()) << shape.id;
        for (std::size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(std::string_view{a[i].name}, std::string_view{b[i].name}) << shape.id;
            EXPECT_NEAR(glm::length(a[i].outward), 1.0f, c_tolerance) << shape.id << " " << a[i].name;
            EXPECT_NEAR(glm::length(a[i].up), 1.0f, c_tolerance) << shape.id << " " << a[i].name;
            EXPECT_NEAR(glm::dot(a[i].outward, a[i].up), 0.0f, c_tolerance) << shape.id << " " << a[i].name;
            for (std::size_t j = i + 1; j < a.size(); ++j) {
                EXPECT_NE(std::string_view{a[i].name}, std::string_view{a[j].name}) << shape.id;
            }
        }
    }
}

TEST(KitConnector, ShapesNamedByTheRequirementHaveConnectors)
{
    EXPECT_EQ(editor::connector_names_text(connectors_of(defaults("architecture.wall"))),      "start, end, base, top, front, back");
    EXPECT_EQ(editor::connector_names_text(connectors_of(defaults("architecture.door_wall"))), "start, end, base, top, opening_front, opening_back");
    EXPECT_EQ(editor::connector_names_text(connectors_of(defaults("basic.stairs"))),           "bottom, top, base");
    EXPECT_EQ(editor::connector_names_text(connectors_of(defaults("basic.block"))),            "base, top, front, back, left, right");
    EXPECT_EQ(editor::connector_names_text(connectors_of(defaults("nature.rock"))),            "(none)");
}

TEST(KitConnector, WallEndsFollowLength)
{
    Kit_parameter_values values = defaults("architecture.wall");
    ASSERT_TRUE(values.set_float("length", 2.5f));
    ASSERT_TRUE(values.set_float("height", 2.0f));
    const std::vector<Kit_connector> connectors = connectors_of(values);
    expect_near(connector(connectors, "start").position, glm::vec3{-1.25f, 0.0f, 0.0f});
    expect_near(connector(connectors, "end").position,   glm::vec3{ 1.25f, 0.0f, 0.0f});
    expect_near(connector(connectors, "end").outward,    glm::vec3{ 1.0f, 0.0f, 0.0f});
    expect_near(connector(connectors, "top").position,   glm::vec3{ 0.0f, 2.0f, 0.0f});
}

TEST(KitConnector, StairsTopAndBottomFollowRiseAndRun)
{
    Kit_parameter_values values = defaults("basic.stairs");
    ASSERT_TRUE(values.set_float("rise", 1.2f));
    const float run = editor::get_stairs_layout(values).run;
    ASSERT_GT(run, 0.0f);
    const std::vector<Kit_connector> connectors = connectors_of(values);
    expect_near(connector(connectors, "bottom").position, glm::vec3{0.0f, 0.0f,  0.5f * run});
    expect_near(connector(connectors, "top").position,    glm::vec3{0.0f, 1.2f, -0.5f * run});
    expect_near(connector(connectors, "top").outward,     glm::vec3{0.0f, 0.0f, -1.0f});
}

TEST(KitConnector, BoxSidesFollowTheirParameters)
{
    Kit_parameter_values block = defaults("basic.block");
    ASSERT_TRUE(block.set_float("width", 1.5f));
    ASSERT_TRUE(block.set_float("height", 2.5f));
    ASSERT_TRUE(block.set_float("depth", 0.75f));
    const std::vector<Kit_connector> b = connectors_of(block);
    expect_near(connector(b, "top").position,   glm::vec3{0.0f, 2.5f, 0.0f});
    expect_near(connector(b, "front").position, glm::vec3{0.0f, 0.0f, 0.375f});
    expect_near(connector(b, "back").outward,   glm::vec3{0.0f, 0.0f, -1.0f});
    expect_near(connector(b, "left").position,  glm::vec3{-0.75f, 0.0f, 0.0f});
    expect_near(connector(b, "right").outward,  glm::vec3{1.0f, 0.0f, 0.0f});

    Kit_parameter_values slab = defaults("basic.floor_slab");
    ASSERT_TRUE(slab.set_float("width", 3.0f));
    ASSERT_TRUE(slab.set_float("depth", 5.0f));
    ASSERT_TRUE(slab.set_float("thickness", 0.3f));
    const std::vector<Kit_connector> s = connectors_of(slab);
    expect_near(connector(s, "top").position,   glm::vec3{0.0f, 0.3f, 0.0f});
    expect_near(connector(s, "front").position, glm::vec3{0.0f, 0.3f, 2.5f});
    expect_near(connector(s, "right").position, glm::vec3{1.5f, 0.3f, 0.0f});
    expect_near(connector(s, "left").up,        glm::vec3{0.0f, 1.0f, 0.0f});
}

TEST(KitConnector, ArchDoorWallAndWedgeFollowTheirParameters)
{
    Kit_parameter_values arch = defaults("architecture.arch");
    ASSERT_TRUE(arch.set_float("span", 3.0f));
    ASSERT_TRUE(arch.set_float("rise", 2.0f));
    ASSERT_TRUE(arch.set_float("thickness", 0.4f));
    const std::vector<Kit_connector> a = connectors_of(arch);
    expect_near(connector(a, "start").position, glm::vec3{-1.9f, 0.0f, 0.0f});
    expect_near(connector(a, "end").position,   glm::vec3{ 1.9f, 0.0f, 0.0f});
    expect_near(connector(a, "top").position,   glm::vec3{0.0f, 2.4f, 0.0f});

    Kit_parameter_values door = defaults("architecture.door_wall");
    ASSERT_TRUE(door.set_float("length", 5.0f));
    ASSERT_TRUE(door.set_float("thickness", 0.3f));
    const std::vector<Kit_connector> d = connectors_of(door);
    expect_near(connector(d, "opening_front").position, glm::vec3{0.0f, 0.0f,  0.15f});
    expect_near(connector(d, "opening_back").position,  glm::vec3{0.0f, 0.0f, -0.15f});
    expect_near(connector(d, "opening_back").outward,   glm::vec3{0.0f, 0.0f, -1.0f});
    expect_near(connector(d, "end").position,           glm::vec3{2.5f, 0.0f, 0.0f});

    Kit_parameter_values wedge = defaults("basic.wedge");
    ASSERT_TRUE(wedge.set_float("run", 3.0f));
    ASSERT_TRUE(wedge.set_float("rise", 0.5f));
    const std::vector<Kit_connector> w = connectors_of(wedge);
    expect_near(connector(w, "bottom").position, glm::vec3{0.0f, 0.0f,  1.5f});
    expect_near(connector(w, "top").position,    glm::vec3{0.0f, 0.5f, -1.5f});
    expect_near(connector(w, "base").outward,    glm::vec3{0.0f, -1.0f, 0.0f});
}

TEST(KitConnector, FirstConnectorIsTheAttachingOne)
{
    for (const char* id : {"basic.block", "basic.floor_slab", "basic.cylinder", "architecture.pillar"}) {
        EXPECT_EQ(std::string_view{connectors_of(defaults(id)).front().name}, "base") << id;
    }
    EXPECT_EQ(std::string_view{connectors_of(defaults("architecture.wall")).front().name}, "start");
    EXPECT_EQ(std::string_view{connectors_of(defaults("basic.stairs")).front().name}, "bottom");
}

TEST(KitConnector, JoinMeetsTheTargetConnector)
{
    const Kit_connector piece{.name = "a", .position = glm::vec3{0.3f, -0.2f, 0.7f}, .outward = glm::normalize(glm::vec3{1.0f, 2.0f, -0.5f}), .up = {}};
    Kit_connector piece_frame = piece;
    piece_frame.up = glm::normalize(glm::cross(piece.outward, glm::vec3{0.0f, 0.0f, 1.0f}));
    const glm::quat target_rotation = glm::angleAxis(0.8f, glm::normalize(glm::vec3{0.2f, 1.0f, 0.3f}));
    const Kit_connector target = editor::transform_connector(
        Kit_connector{.name = "b", .position = glm::vec3{1.0f, 0.5f, 0.0f}, .outward = glm::vec3{1.0f, 0.0f, 0.0f}, .up = glm::vec3{0.0f, 1.0f, 0.0f}},
        glm::vec3{4.0f, 1.0f, -2.0f}, target_rotation
    );
    for (int twist = -1; twist <= 4; ++twist) {
        for (const float gap : {0.0f, 0.25f, -0.1f}) {
            const Kit_join_pose pose  = editor::join_connectors(piece_frame, target, twist, gap);
            const Kit_connector moved = joined(piece_frame, pose);
            expect_near(moved.position, target.position + (gap * target.outward));
            expect_near(moved.outward, -target.outward);
            const glm::vec3 side = glm::cross(target.outward, target.up);
            const int       q    = ((twist % 4) + 4) % 4;
            const glm::vec3 up   = (q == 0) ? target.up : (q == 1) ? side : (q == 2) ? -target.up : -side;
            expect_near(moved.up, up);
            EXPECT_NEAR(glm::determinant(glm::mat3_cast(pose.rotation)), 1.0f, c_tolerance);
            const Kit_join_pose again = editor::join_connectors(piece_frame, target, twist, gap);
            EXPECT_EQ(again.position, pose.position);
            EXPECT_EQ(again.rotation, pose.rotation);
        }
    }
}

TEST(KitConnector, WallStartToWallEndIsCollinearAndTouching)
{
    const std::vector<Kit_connector> connectors = connectors_of(defaults("architecture.wall"));
    const Kit_connector target = editor::transform_connector(connector(connectors, "end"), glm::vec3{1.0f, 0.0f, 2.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f});
    const Kit_join_pose pose   = editor::join_connectors(connector(connectors, "start"), target, 0, 0.0f);
    expect_near(pose.position, glm::vec3{5.0f, 0.0f, 2.0f});
    EXPECT_NEAR(std::abs(pose.rotation.w), 1.0f, c_tolerance);
}

TEST(KitConnector, StairsTopToFloorEdge)
{
    const Kit_parameter_values stairs = defaults("basic.stairs");
    const Kit_parameter_values floor  = defaults("basic.floor_slab");
    const float run       = editor::get_stairs_layout(stairs).run;
    const float rise      = stairs.get_float("rise");
    const float thickness = floor.get_float("thickness");
    const float depth     = floor.get_float("depth");
    const Kit_connector target = connector(connectors_of(floor), "front");
    const Kit_join_pose pose   = editor::join_connectors(connector(connectors_of(stairs), "top"), target, 0, 0.0f);
    expect_near(pose.position, glm::vec3{0.0f, thickness - rise, (0.5f * depth) + (0.5f * run)});
    EXPECT_NEAR(std::abs(pose.rotation.w), 1.0f, c_tolerance);
}

TEST(KitConnector, StackingTopToBaseKeepsHeading)
{
    const Kit_connector target = connector(connectors_of(defaults("basic.block")), "top");
    const Kit_join_pose pose   = editor::join_connectors(connector(connectors_of(defaults("architecture.pillar")), "base"), target, 1, 0.0f);
    expect_near(pose.position, target.position);
    // One quarter turn about +Y.
    expect_near(glm::mat3_cast(pose.rotation) * glm::vec3{0.0f, 0.0f, 1.0f}, glm::vec3{1.0f, 0.0f, 0.0f});
}

// D3.13: the scene-free pair search of the connector snap.

using editor::Kit_connector_point;
using editor::Kit_connector_snap_match;
using editor::find_connector_snap;

auto point(const float x, const float y, const float z, const std::uint64_t piece_id, const std::size_t connector_index) -> Kit_connector_point
{
    return Kit_connector_point{.position = glm::vec3{x, y, z}, .piece_id = piece_id, .connector_index = connector_index};
}

TEST(Kit_connector_snap, finds_the_closest_pair)
{
    const std::vector<Kit_connector_point> moved {point(0.0f, 0.0f, 0.0f, 1, 0), point(4.0f, 0.0f, 0.0f, 1, 1)};
    const std::vector<Kit_connector_point> others{point(0.3f, 0.0f, 0.0f, 2, 0), point(4.0f, 0.2f, 0.0f, 3, 0), point(9.0f, 0.0f, 0.0f, 4, 0)};
    const Kit_connector_snap_match match = find_connector_snap(moved, others, 0.5f);
    ASSERT_TRUE(match.found);
    EXPECT_EQ(match.moved, 1u);
    EXPECT_EQ(match.other, 1u);
    EXPECT_NEAR(match.distance, 0.2f, c_tolerance);
}

TEST(Kit_connector_snap, threshold_is_inclusive)
{
    const std::vector<Kit_connector_point> moved {point(0.0f, 0.0f, 0.0f, 1, 0)};
    const std::vector<Kit_connector_point> at    {point(0.5f, 0.0f, 0.0f, 2, 0)};
    const std::vector<Kit_connector_point> beyond{point(0.5f + 1.0e-4f, 0.0f, 0.0f, 2, 0)};
    const Kit_connector_snap_match at_match = find_connector_snap(moved, at, 0.5f);
    EXPECT_TRUE(at_match.found);
    EXPECT_FLOAT_EQ(at_match.distance, 0.5f);
    EXPECT_FALSE(find_connector_snap(moved, beyond, 0.5f).found);
}

TEST(Kit_connector_snap, coincident_connectors_snap_at_zero_distance)
{
    const std::vector<Kit_connector_point> moved {point(1.0f, 2.0f, 3.0f, 1, 0)};
    const std::vector<Kit_connector_point> others{point(1.0f, 2.0f, 3.0f, 2, 4)};
    const Kit_connector_snap_match match = find_connector_snap(moved, others, 0.1f);
    ASSERT_TRUE(match.found);
    EXPECT_EQ(match.distance, 0.0f);
}

TEST(Kit_connector_snap, equal_distances_take_the_smallest_key_in_any_input_order)
{
    // Four pairs at 0.25 m; the key order is (moved piece, moved connector, other piece, other connector).
    std::vector<Kit_connector_point> moved {point(0.0f, 0.0f, 0.0f, 7, 2), point(10.0f, 0.0f, 0.0f, 5, 3)};
    std::vector<Kit_connector_point> others{point(0.25f, 0.0f, 0.0f, 9, 1), point(-0.25f, 0.0f, 0.0f, 8, 0), point(10.0f, 0.25f, 0.0f, 6, 5), point(10.0f, -0.25f, 0.0f, 6, 4)};
    for (int permutation = 0; permutation < 4; ++permutation) {
        std::rotate(others.begin(), others.begin() + 1, others.end());
        if ((permutation % 2) == 1) {
            std::swap(moved[0], moved[1]);
        }
        const Kit_connector_snap_match match = find_connector_snap(moved, others, 1.0f);
        ASSERT_TRUE(match.found);
        EXPECT_EQ(moved[match.moved].piece_id, 5u);
        EXPECT_EQ(moved[match.moved].connector_index, 3u);
        EXPECT_EQ(others[match.other].piece_id, 6u);
        EXPECT_EQ(others[match.other].connector_index, 4u);
    }
}

TEST(Kit_connector_snap, empty_sets_find_nothing)
{
    const std::vector<Kit_connector_point> one{point(0.0f, 0.0f, 0.0f, 1, 0)};
    const std::vector<Kit_connector_point> none{};
    EXPECT_FALSE(find_connector_snap(none, one, 1.0f).found);
    EXPECT_FALSE(find_connector_snap(one, none, 1.0f).found);
    EXPECT_FALSE(find_connector_snap(none, none, 1.0f).found);
}

TEST(Kit_connector_snap, zero_negative_and_non_finite_distances_disable)
{
    const std::vector<Kit_connector_point> moved {point(0.0f, 0.0f, 0.0f, 1, 0)};
    const std::vector<Kit_connector_point> others{point(0.0f, 0.0f, 0.0f, 2, 0)};
    EXPECT_FALSE(find_connector_snap(moved, others, 0.0f).found);
    EXPECT_FALSE(find_connector_snap(moved, others, -1.0f).found);
    EXPECT_FALSE(find_connector_snap(moved, others, std::numeric_limits<float>::quiet_NaN()).found);
    EXPECT_FALSE(find_connector_snap(moved, others, std::numeric_limits<float>::infinity()).found);
}

auto trs(const glm::vec3& t, const glm::quat& r, const glm::vec3& scale) -> glm::mat4
{
    glm::mat4 m = glm::mat4_cast(r) * glm::mat4{glm::vec4{scale.x, 0.0f, 0.0f, 0.0f}, glm::vec4{0.0f, scale.y, 0.0f, 0.0f}, glm::vec4{0.0f, 0.0f, scale.z, 0.0f}, glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
    m[3] = glm::vec4{t, 1.0f};
    return m;
}

void expect_unit_frame(const Kit_connector& frame)
{
    EXPECT_NEAR(glm::length(frame.outward), 1.0f, c_tolerance);
    EXPECT_NEAR(glm::length(frame.up), 1.0f, c_tolerance);
    EXPECT_NEAR(glm::dot(frame.outward, frame.up), 0.0f, c_tolerance);
}

TEST(Kit_connector_transform, uniform_scale)
{
    const Kit_connector end   = connector(connectors_of(defaults("architecture.wall")), "end");
    const glm::quat     yaw   = glm::angleAxis(0.5f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4     world = trs(glm::vec3{1.0f, 2.0f, 3.0f}, yaw, glm::vec3{1.2f});
    const std::optional<Kit_connector> frame = editor::transform_connector(end, world);
    ASSERT_TRUE(frame.has_value());
    expect_near(frame->position, glm::vec3{world * glm::vec4{end.position, 1.0f}});
    expect_near(frame->outward, yaw * end.outward);
    expect_near(frame->up, yaw * end.up);
}

TEST(Kit_connector_transform, non_uniform_scale_and_scaled_parent)
{
    const Kit_connector end    = connector(connectors_of(defaults("architecture.wall")), "end");
    const glm::quat     yaw    = glm::angleAxis(0.5f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::quat     tilt   = glm::angleAxis(0.3f, glm::normalize(glm::vec3{1.0f, 0.0f, 1.0f}));
    const glm::mat4     parent = trs(glm::vec3{-4.0f, 1.0f, 0.5f}, tilt, glm::vec3{2.0f, 0.5f, 1.0f});
    const glm::mat4     local  = trs(glm::vec3{1.0f, 0.0f, 0.0f}, yaw, glm::vec3{1.5f, 1.0f, 0.75f});
    const glm::mat4     world  = parent * local; // sheared: a non-uniform parent over a turned child
    const std::optional<Kit_connector> frame = editor::transform_connector(end, world);
    ASSERT_TRUE(frame.has_value());
    expect_unit_frame(frame.value());
    expect_near(frame->position, glm::vec3{world * glm::vec4{end.position, 1.0f}});
    // The outward axis stays normal to the transformed face: perpendicular to two transformed in-face vectors.
    const glm::vec3 in_face_up   = glm::normalize(glm::mat3{world} * end.up);
    const glm::vec3 in_face_side = glm::normalize(glm::mat3{world} * glm::cross(end.outward, end.up));
    EXPECT_NEAR(glm::dot(frame->outward, in_face_up), 0.0f, c_tolerance);
    EXPECT_NEAR(glm::dot(frame->outward, in_face_side), 0.0f, c_tolerance);
    EXPECT_GT(glm::dot(frame->outward, glm::mat3{world} * end.outward), 0.0f);
    expect_near(frame->up, in_face_up);
}

TEST(Kit_connector_transform, mirrored_matrix_keeps_a_right_handed_frame)
{
    const Kit_connector end   = connector(connectors_of(defaults("architecture.wall")), "end");
    const glm::mat4     world = trs(glm::vec3{0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{-1.0f, 1.0f, 1.0f});
    const std::optional<Kit_connector> frame = editor::transform_connector(end, world);
    ASSERT_TRUE(frame.has_value());
    expect_near(frame->position, glm::vec3{-end.position.x, end.position.y, end.position.z});
    expect_near(frame->outward, glm::vec3{-end.outward.x, end.outward.y, end.outward.z});
    expect_near(frame->up, end.up);
    const glm::mat3 basis{frame->outward, frame->up, glm::cross(frame->outward, frame->up)};
    EXPECT_NEAR(glm::determinant(basis), 1.0f, c_tolerance);
}

TEST(Kit_connector_transform, zero_scale_axis_degenerates)
{
    const std::vector<Kit_connector> connectors = connectors_of(defaults("architecture.wall"));
    const glm::mat4 flat = trs(glm::vec3{0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 1.0f});
    EXPECT_FALSE(editor::transform_connector(connector(connectors, "end"), flat).has_value());
    std::vector<Kit_connector> out{Kit_connector{}};
    EXPECT_FALSE(editor::transform_connectors(connectors, flat, out));
    EXPECT_TRUE(out.empty());
    EXPECT_TRUE(editor::transform_connectors(connectors, glm::mat4{1.0f}, out));
    EXPECT_EQ(out.size(), connectors.size());
}

TEST(Kit_connector_join, scaled_moved_piece_and_scaled_target)
{
    const std::vector<Kit_connector> wall = connectors_of(defaults("architecture.wall"));
    const glm::quat target_yaw = glm::angleAxis(1.1f, glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 target_world = trs(glm::vec3{3.0f, 0.0f, -2.0f}, target_yaw, glm::vec3{0.8f, 1.4f, 0.8f});
    const glm::mat4 moved_world  = trs(glm::vec3{-5.0f, 1.0f, 4.0f}, glm::angleAxis(-0.4f, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{1.3f, 0.9f, 1.3f});
    const std::optional<Kit_connector> target = editor::transform_connector(connector(wall, "end"), target_world);
    ASSERT_TRUE(target.has_value());
    for (int twist = 0; twist < 4; ++twist) {
        const std::optional<Kit_join_pose> motion = editor::join_motion(connector(wall, "start"), moved_world, target.value(), twist, 0.1f);
        ASSERT_TRUE(motion.has_value());
        EXPECT_NEAR(glm::determinant(glm::mat3_cast(motion->rotation)), 1.0f, c_tolerance);
        const glm::mat4 joined_world = editor::apply_motion(motion.value(), moved_world);
        const std::optional<Kit_connector> start = editor::transform_connector(connector(wall, "start"), joined_world);
        ASSERT_TRUE(start.has_value());
        expect_near(start->position, target->position + (0.1f * target->outward));
        expect_near(start->outward, -target->outward);
        for (int column = 0; column < 3; ++column) {
            EXPECT_NEAR(glm::length(glm::vec3{joined_world[column]}), glm::length(glm::vec3{moved_world[column]}), c_tolerance);
        }
    }
    const std::optional<Kit_join_pose> twisted = editor::join_motion(connector(wall, "start"), moved_world, target.value(), 0, 0.0f);
    ASSERT_TRUE(twisted.has_value());
    const std::optional<Kit_connector> up_start = editor::transform_connector(connector(wall, "start"), editor::apply_motion(twisted.value(), moved_world));
    ASSERT_TRUE(up_start.has_value());
    expect_near(up_start->up, target->up);
}

TEST(Kit_connector_join, joined_piece_needs_no_motion)
{
    const std::vector<Kit_connector> wall = connectors_of(defaults("architecture.wall"));
    const glm::mat4 moved_world = trs(glm::vec3{2.0f, 0.0f, 1.0f}, glm::angleAxis(0.7f, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{1.2f});
    const std::optional<Kit_connector> start = editor::transform_connector(connector(wall, "start"), moved_world);
    ASSERT_TRUE(start.has_value());
    const Kit_connector target{.name = "end", .position = start->position, .outward = -start->outward, .up = start->up};
    const std::optional<Kit_join_pose> motion = editor::join_motion(connector(wall, "start"), moved_world, target, 0, 0.0f);
    ASSERT_TRUE(motion.has_value());
    EXPECT_TRUE(editor::is_identity_motion(motion.value()));
    EXPECT_FALSE(editor::is_identity_motion(Kit_join_pose{.position = glm::vec3{0.001f, 0.0f, 0.0f}}));
}

TEST(Kit_connector_join, rotation_matrix)
{
    EXPECT_TRUE(editor::is_rotation_matrix(glm::mat3_cast(glm::angleAxis(0.7f, glm::normalize(glm::vec3{1.0f, 2.0f, 3.0f})))));
    EXPECT_FALSE(editor::is_rotation_matrix(glm::mat3{-1.0f}));
    EXPECT_FALSE(editor::is_rotation_matrix(glm::mat3{1.001f}));
}

TEST(Kit_connector_join, mirrored_piece_keeps_its_stored_scale)
{
    // A wall mirrored by local scale (-1, 1, 1), joined: the motion times its matrix is still a rotation times that scale.
    const std::vector<Kit_connector> wall = connectors_of(defaults("architecture.wall"));
    const glm::vec3 scale{-1.0f, 1.0f, 1.0f};
    const glm::mat4 moved_world  = trs(glm::vec3{2.0f, 0.0f, 1.0f}, glm::angleAxis(0.4f, glm::vec3{0.0f, 1.0f, 0.0f}), scale);
    const glm::mat4 target_world = trs(glm::vec3{-3.0f, 0.0f, 5.0f}, glm::angleAxis(-1.2f, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{1.0f});
    const std::optional<Kit_connector> target = editor::transform_connector(connector(wall, "end"), target_world);
    ASSERT_TRUE(target.has_value());
    const std::optional<Kit_join_pose> motion = editor::join_motion(connector(wall, "start"), moved_world, target.value(), 0, 0.0f);
    ASSERT_TRUE(motion.has_value());
    const glm::mat4 joined_world = editor::apply_motion(motion.value(), moved_world);
    const std::optional<glm::quat> rotation = editor::rotation_for_scale(glm::mat3{joined_world}, scale);
    ASSERT_TRUE(rotation.has_value());
    glm::mat4 rebuilt = trs(glm::vec3{joined_world[3]}, rotation.value(), scale);
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(rebuilt[column][row], joined_world[column][row], c_tolerance);
        }
    }
    const std::optional<Kit_connector> start = editor::transform_connector(connector(wall, "start"), rebuilt);
    ASSERT_TRUE(start.has_value());
    expect_near(start->position, target->position);
    // A non-uniform scale under a turn is no rotation times that scale.
    EXPECT_FALSE(editor::rotation_for_scale(glm::mat3{2.0f}, glm::vec3{1.0f}).has_value());
}

TEST(Kit_connector_join, orthogonal_axes)
{
    const glm::quat turn = glm::angleAxis(0.7f, glm::normalize(glm::vec3{1.0f, 2.0f, 3.0f}));
    EXPECT_TRUE(editor::has_orthogonal_axes(glm::mat3_cast(turn)));
    EXPECT_TRUE(editor::has_orthogonal_axes(glm::mat3{trs(glm::vec3{0.0f}, turn, glm::vec3{2.0f, 0.5f, -1.0f})}));
    // A non-uniform scale after a turn shears the axes.
    const glm::mat3 scale_after_turn = glm::mat3{trs(glm::vec3{0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{2.0f, 1.0f, 1.0f})} * glm::mat3_cast(turn);
    EXPECT_FALSE(editor::has_orthogonal_axes(scale_after_turn));
    glm::mat3 sheared{1.0f};
    sheared[1][0] = 0.1f;
    EXPECT_FALSE(editor::has_orthogonal_axes(sheared));
}

} // anonymous namespace
