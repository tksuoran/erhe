#include "level/kit_connector.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shape_library.hpp"
#include "level/kit_shapes.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
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

TEST(Kit_connector_switches, default_enables_every_connector)
{
    const editor::Kit_connector_switches switches{};
    EXPECT_TRUE(switches.is_enabled("top"));
    EXPECT_TRUE(switches.is_enabled("left"));
    EXPECT_FALSE(switches.is_disabled("top"));
}

TEST(Kit_connector_switches, enabled_needs_both_switches)
{
    editor::Kit_connector_switches switches{.connectors_enabled = true, .disabled_connectors = "left,right"};
    EXPECT_FALSE(switches.is_enabled("left"));
    EXPECT_FALSE(switches.is_enabled("right"));
    EXPECT_TRUE (switches.is_enabled("front"));
    EXPECT_TRUE (switches.is_enabled("lef")); // whole names only
    switches.connectors_enabled = false;
    EXPECT_FALSE(switches.is_enabled("front"));
    EXPECT_TRUE (switches.is_disabled("left"));
    EXPECT_FALSE(switches.is_disabled("front"));
}

TEST(Kit_connector_switches, list_reads_leniently_and_writes_canonically)
{
    const editor::Kit_connector_switches spaced{.connectors_enabled = true, .disabled_connectors = " right , ,left,"};
    EXPECT_TRUE (spaced.is_disabled("left"));
    EXPECT_TRUE (spaced.is_disabled("right"));
    EXPECT_FALSE(spaced.is_disabled(""));
    EXPECT_EQ(editor::parse_connector_names(" right , ,left,"), (std::vector<std::string>{"right", "left"}));
    EXPECT_EQ(editor::make_connector_names_text({"right", "left", "right"}), "left,right");
    EXPECT_EQ(editor::make_connector_names_text({}), "");
}

TEST(Kit_connector_switches, set_state_adds_and_removes_names)
{
    using editor::Kit_connector_state;
    std::string text = editor::set_connector_state("", "right", Kit_connector_state::disabled);
    EXPECT_EQ(text, "right");
    text = editor::set_connector_state(text, "left", Kit_connector_state::disabled);
    EXPECT_EQ(text, "left,right");
    EXPECT_EQ(editor::set_connector_state(text, "left", Kit_connector_state::disabled), "left,right");
    text = editor::set_connector_state(text, "right", Kit_connector_state::enabled);
    EXPECT_EQ(text, "left");
    EXPECT_EQ(editor::set_connector_state(text, "front", Kit_connector_state::enabled), "left");
    EXPECT_EQ(editor::set_connector_state(text, "left", Kit_connector_state::enabled), "");
}

TEST(Kit_connector_switches, remove_disabled_keeps_the_shape_order)
{
    std::vector<Kit_connector> slab = connectors_of(defaults("basic.floor_slab"));
    const std::size_t count = slab.size();
    ASSERT_GE(count, 3u);
    const std::string first_name{slab.front().name};
    const editor::Kit_connector_switches switches{.connectors_enabled = true, .disabled_connectors = "left,right"};
    std::vector<Kit_connector> expected;
    for (const Kit_connector& c : slab) {
        if ((std::string_view{c.name} != "left") && (std::string_view{c.name} != "right")) {
            expected.push_back(c);
        }
    }
    editor::remove_disabled_connectors(switches, slab);
    ASSERT_EQ(slab.size(), expected.size());
    EXPECT_EQ(slab.size(), count - 2);
    for (std::size_t i = 0; i < slab.size(); ++i) {
        EXPECT_STREQ(slab[i].name, expected[i].name);
    }
    editor::remove_disabled_connectors(editor::Kit_connector_switches{.connectors_enabled = false, .disabled_connectors = {}}, slab);
    EXPECT_TRUE(slab.empty());
}

TEST(Kit_connector_switches, write_check_refuses_unknown_names_and_non_canonical_text)
{
    const std::vector<Kit_connector> wall = connectors_of(defaults("architecture.wall"));
    EXPECT_EQ(editor::check_disabled_connectors_text("", wall), "");
    EXPECT_EQ(editor::check_disabled_connectors_text("end,start", wall), "");
    const std::string unknown = editor::check_disabled_connectors_text("end,zz", wall);
    EXPECT_NE(unknown.find("'zz'"), std::string::npos) << unknown;
    EXPECT_NE(unknown.find("start, end, base, top, front, back"), std::string::npos) << unknown;
    const std::string wrong_case = editor::check_disabled_connectors_text("Top", wall);
    EXPECT_NE(wrong_case.find("'Top'"), std::string::npos) << wrong_case;
    const std::string spaced = editor::check_disabled_connectors_text("start, end", wall);
    EXPECT_NE(spaced.find("write 'end,start'"), std::string::npos) << spaced;
    const std::string repeated = editor::check_disabled_connectors_text("end,end", wall);
    EXPECT_NE(repeated.find("write 'end'"), std::string::npos) << repeated;
    const std::vector<Kit_connector> rock = connectors_of(defaults("nature.rock"));
    EXPECT_EQ(editor::check_disabled_connectors_text("", rock), "");
    EXPECT_NE(editor::check_disabled_connectors_text("top", rock), "");
}

// U27: the stay-on-axis projection of the connector snap offset.

using editor::Kit_connector_snap_constraint;
using editor::Kit_connector_snap_constraint_kind;
using editor::constrain_connector_snap_offset;

auto near_vec3(const glm::vec3& a, const glm::vec3& b) -> bool
{
    return (std::abs(a.x - b.x) < c_tolerance) && (std::abs(a.y - b.y) < c_tolerance) && (std::abs(a.z - b.z) < c_tolerance);
}

TEST(Kit_connector_snap, free_constraint_keeps_the_full_offset)
{
    const glm::vec3 offset{0.3f, -0.2f, 0.1f};
    EXPECT_EQ(constrain_connector_snap_offset(offset, Kit_connector_snap_constraint{}), offset);
    const Kit_connector_snap_constraint free_with_direction{.kind = Kit_connector_snap_constraint_kind::free, .direction = glm::vec3{1.0f, 0.0f, 0.0f}};
    EXPECT_EQ(constrain_connector_snap_offset(offset, free_with_direction), offset);
}

TEST(Kit_connector_snap, axis_constraint_keeps_the_component_along_the_axis)
{
    const glm::vec3 offset{0.3f, -0.2f, 0.1f};
    const Kit_connector_snap_constraint x_axis{.kind = Kit_connector_snap_constraint_kind::axis, .direction = glm::vec3{1.0f, 0.0f, 0.0f}};
    EXPECT_TRUE(near_vec3(constrain_connector_snap_offset(offset, x_axis), glm::vec3{0.3f, 0.0f, 0.0f}));
    // A non-unit, turned axis: the result is parallel to it and the remainder perpendicular.
    const glm::vec3 direction{2.0f, 2.0f, 0.0f};
    const Kit_connector_snap_constraint diagonal{.kind = Kit_connector_snap_constraint_kind::axis, .direction = direction};
    const glm::vec3 along = constrain_connector_snap_offset(offset, diagonal);
    EXPECT_TRUE(near_vec3(along, glm::vec3{0.05f, 0.05f, 0.0f}));
    EXPECT_NEAR(glm::dot(offset - along, direction), 0.0f, c_tolerance);
}

TEST(Kit_connector_snap, plane_constraint_drops_the_component_along_the_normal)
{
    const glm::vec3 offset{0.3f, -0.2f, 0.1f};
    const Kit_connector_snap_constraint xz_plane{.kind = Kit_connector_snap_constraint_kind::plane, .direction = glm::vec3{0.0f, 3.0f, 0.0f}};
    EXPECT_TRUE(near_vec3(constrain_connector_snap_offset(offset, xz_plane), glm::vec3{0.3f, 0.0f, 0.1f}));
    // An offset along the normal leaves nothing to apply.
    EXPECT_TRUE(near_vec3(constrain_connector_snap_offset(glm::vec3{0.0f, 0.4f, 0.0f}, xz_plane), glm::vec3{0.0f}));
}

TEST(Kit_connector_snap, degenerate_direction_keeps_the_full_offset)
{
    const glm::vec3 offset{0.3f, -0.2f, 0.1f};
    const Kit_connector_snap_constraint zero_axis{.kind = Kit_connector_snap_constraint_kind::axis, .direction = glm::vec3{0.0f}};
    EXPECT_EQ(constrain_connector_snap_offset(offset, zero_axis), offset);
    const Kit_connector_snap_constraint nan_plane{.kind = Kit_connector_snap_constraint_kind::plane, .direction = glm::vec3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}};
    EXPECT_EQ(constrain_connector_snap_offset(offset, nan_plane), offset);
}

// D3.13 for rotate and scale drags: a moved connector taken into its root's frame at the drag begin follows the root's
// step pose, rotation and scale included, as the node's own world matrix of that pose carries it.
auto about_pivot(const glm::vec3& pivot, const glm::mat4& delta) -> glm::mat4
{
    glm::mat4 to_pivot{1.0f};
    to_pivot[3] = glm::vec4{-pivot, 1.0f};
    glm::mat4 from_pivot{1.0f};
    from_pivot[3] = glm::vec4{pivot, 1.0f};
    return from_pivot * delta * to_pivot;
}

void expect_carried(const glm::mat4& world_from_root_begin, const glm::mat4& root_from_node, const glm::mat4& step_delta)
{
    const glm::mat4 world_from_root_step = step_delta * world_from_root_begin;
    const glm::mat4 world_from_node_step = world_from_root_step * root_from_node;
    for (const Kit_connector& local : connectors_of(defaults("architecture.wall"))) {
        const Kit_connector in_root = editor::connector_in_root(local, root_from_node);
        const std::optional<glm::vec3> position = editor::carry_connector_position(in_root.position, world_from_root_step);
        ASSERT_TRUE(position.has_value()) << local.name;
        expect_near(position.value(), glm::vec3{world_from_node_step * glm::vec4{local.position, 1.0f}});
        // The indicators' frame: in_root carried by the step's root matrix equals the node frame of the step pose.
        const std::optional<Kit_connector> carried = editor::transform_connector(in_root, world_from_root_step);
        const std::optional<Kit_connector> direct  = editor::transform_connector(local, world_from_node_step);
        ASSERT_TRUE(carried.has_value() && direct.has_value()) << local.name;
        expect_unit_frame(carried.value());
        expect_near(carried->position, direct->position);
        expect_near(carried->outward, direct->outward);
        expect_near(carried->up, direct->up);
    }
}

TEST(Kit_connector_drag_carry, rotation_about_the_pivot)
{
    const glm::mat4 root  = trs(glm::vec3{1.0f, 0.5f, -2.0f}, glm::angleAxis(0.4f, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{1.0f});
    const glm::mat4 child = trs(glm::vec3{0.5f, 0.0f, 1.0f}, glm::angleAxis(-0.7f, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{1.0f, 1.5f, 1.0f});
    const glm::quat turn  = glm::angleAxis(1.2f, glm::normalize(glm::vec3{0.2f, 1.0f, -0.1f}));
    expect_carried(root, glm::mat4{1.0f}, about_pivot(glm::vec3{1.0f, 0.5f, -2.0f}, glm::mat4_cast(turn)));
    expect_carried(root, child, about_pivot(glm::vec3{3.0f, 0.0f, 1.0f}, glm::mat4_cast(turn)));
}

TEST(Kit_connector_drag_carry, non_uniform_scale_of_a_turned_root_shears)
{
    const glm::mat4 root  = trs(glm::vec3{-1.0f, 0.0f, 2.0f}, glm::angleAxis(0.6f, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{1.0f, 2.0f, 1.0f});
    const glm::mat4 child = trs(glm::vec3{0.0f, 1.0f, 0.5f}, glm::angleAxis(0.3f, glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f});
    const glm::mat4 scale = glm::scale(glm::mat4{1.0f}, glm::vec3{1.8f, 1.0f, 0.6f}); // world axes over a turned root: a sheared result
    expect_carried(root, glm::mat4{1.0f}, about_pivot(glm::vec3{-1.0f, 0.0f, 2.0f}, scale));
    expect_carried(root, child, about_pivot(glm::vec3{0.0f, 0.0f, 0.0f}, scale));
    expect_carried(root, child, about_pivot(glm::vec3{-1.0f, 0.0f, 2.0f}, glm::scale(glm::mat4{1.0f}, glm::vec3{1.35f})));
}

TEST(Kit_connector_drag_carry, mirrored_scale_keeps_right_handed_frames)
{
    const glm::mat4 root   = trs(glm::vec3{2.0f, 0.0f, 0.0f}, glm::angleAxis(0.5f, glm::vec3{0.0f, 1.0f, 0.0f}), glm::vec3{1.0f});
    const glm::mat4 mirror = about_pivot(glm::vec3{2.0f, 0.0f, 0.0f}, glm::scale(glm::mat4{1.0f}, glm::vec3{-1.0f, 1.0f, 1.0f}));
    expect_carried(root, glm::mat4{1.0f}, mirror);
    const Kit_connector end     = connector(connectors_of(defaults("architecture.wall")), "end");
    const Kit_connector in_root = editor::connector_in_root(end, glm::mat4{1.0f});
    const std::optional<Kit_connector> carried = editor::transform_connector(in_root, mirror * root);
    ASSERT_TRUE(carried.has_value());
    const glm::mat3 basis{carried->outward, carried->up, glm::cross(carried->outward, carried->up)};
    EXPECT_NEAR(glm::determinant(basis), 1.0f, c_tolerance);
}

TEST(Kit_connector_drag_carry, collapsed_axis_carries_nothing)
{
    const glm::mat4 root     = trs(glm::vec3{0.0f, 0.0f, 1.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f});
    const glm::mat4 collapse = about_pivot(glm::vec3{0.0f, 0.0f, 1.0f}, glm::scale(glm::mat4{1.0f}, glm::vec3{0.0f, 1.0f, 1.0f}));
    const Kit_connector end     = connector(connectors_of(defaults("architecture.wall")), "end");
    const Kit_connector in_root = editor::connector_in_root(end, glm::mat4{1.0f});
    EXPECT_FALSE(editor::carry_connector_position(in_root.position, collapse * root).has_value());
    EXPECT_FALSE(editor::transform_connector(in_root, collapse * root).has_value());
    EXPECT_FALSE(editor::is_degenerate_frame(glm::mat3{root}));
    EXPECT_TRUE(editor::is_degenerate_frame(glm::mat3{collapse * root}));
}

// The pair and offset of one rotate step's pose (stateless; the drag sequence without accumulation is checked by
// scripts/level_tools_verify.py --only-connector-snap).
auto rotate_step_snap(const float angle, const glm::vec3& pivot, const glm::mat4& world_from_root_begin, float max_distance)
    -> std::optional<glm::vec3>
{
    const std::vector<Kit_connector> wall = connectors_of(defaults("architecture.wall"));
    const glm::mat4 step = about_pivot(pivot, glm::mat4_cast(glm::angleAxis(angle, glm::vec3{0.0f, 1.0f, 0.0f}))) * world_from_root_begin;
    std::vector<editor::Kit_connector_point> moved;
    std::vector<editor::Kit_connector_point> targets;
    for (std::size_t k = 0; k < wall.size(); ++k) {
        const std::optional<glm::vec3> position = editor::carry_connector_position(editor::connector_in_root(wall[k], glm::mat4{1.0f}).position, step);
        if (position.has_value()) {
            moved.push_back(editor::Kit_connector_point{.position = position.value(), .piece_id = 2, .connector_index = k});
        }
        targets.push_back(editor::Kit_connector_point{.position = wall[k].position, .piece_id = 1, .connector_index = k}); // target wall at the origin
    }
    const editor::Kit_connector_snap_match match = editor::find_connector_snap(moved, targets, max_distance);
    if (!match.found) {
        return std::nullopt;
    }
    return targets[match.other].position - moved[match.moved].position;
}

TEST(Kit_connector_drag_carry, pair_and_offset_of_rotate_step_poses)
{
    const std::vector<Kit_connector> wall = connectors_of(defaults("architecture.wall"));
    const glm::vec3 a_end  = connector(wall, "end").position;
    const float     half   = -connector(wall, "start").position.x;
    // B turned a quarter turn about its origin puts its start 0.3 m beside A's end.
    const glm::vec3 pivot  = a_end + glm::vec3{0.0f, 0.0f, 0.3f - half};
    const glm::mat4 b      = trs(pivot, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f});
    const float     quarter = 0.5f * glm::pi<float>();
    const std::optional<glm::vec3> snapped = rotate_step_snap(quarter, pivot, b, 0.5f);
    ASSERT_TRUE(snapped.has_value());
    expect_near(snapped.value(), glm::vec3{0.0f, 0.0f, -0.3f});
    // Two thirds of the turn leaves the start about 1 m off: no pair; the pose is the unsnapped one.
    EXPECT_FALSE(rotate_step_snap(glm::radians(60.0f), pivot, b, 0.5f).has_value());
    // 85 degrees: the pair and offset of that pose.
    const std::optional<glm::vec3> near_quarter = rotate_step_snap(glm::radians(85.0f), pivot, b, 0.5f);
    ASSERT_TRUE(near_quarter.has_value());
    const glm::quat turn  = glm::angleAxis(glm::radians(85.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::vec3 start = pivot + (turn * connector(wall, "start").position);
    expect_near(near_quarter.value(), a_end - start);
    EXPECT_LE(glm::length(near_quarter.value()), 0.5f);
}

} // anonymous namespace
