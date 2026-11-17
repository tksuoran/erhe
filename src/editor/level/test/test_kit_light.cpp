#include "level/kit_light.hpp"
#include "level/kit_shape.hpp"
#include "level/kit_shapes.hpp"
#include "kit_shape_test_support.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

using editor::Kit_light_frame;
using editor::Kit_light_match;
using editor::Kit_light_type;
using editor::Kit_parameter_values;
using editor::Kit_shape_definition;
using editor::c_no_light_node;

constexpr std::array<const char*, 5> c_arm_names{"arm_0", "arm_1", "arm_2", "arm_3", "arm_4"};

// A test-only shape stating its light frames from its values: one lamp per arm at the arm's end, under the height.
auto make_lamp_post(const int max_arms) -> Kit_shape_definition
{
    Kit_shape_definition shape;
    shape.id           = "test.lamp_post";
    shape.kit          = "test";
    shape.display_name = "Lamp Post";
    shape.parameters   = {
        editor::make_length_parameter("height", "Lamp height", 3.0f, editor::Kit_default_scale::meters, 1.0f, 8.0f),
        editor::make_count_parameter("arms", "Arm count", 2, 0, max_arms),
        editor::make_seed_parameter()
    };
    shape.get_light_frames = [](const Kit_parameter_values& values, std::vector<Kit_light_frame>& out_frames) {
        const float height = values.get_float("height");
        const int   arms   = values.get_int("arms");
        for (int i = 0; i < arms; ++i) {
            const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(arms);
            out_frames.push_back(
                Kit_light_frame{
                    .name      = c_arm_names[static_cast<std::size_t>(i)],
                    .position  = glm::vec3{0.6f * std::cos(angle), height, 0.6f * std::sin(angle)},
                    .direction = glm::vec3{0.0f, -1.0f, 0.0f}
                }
            );
        }
    };
    return shape;
}

auto make_values(const Kit_shape_definition& shape, const float height, const int arms) -> Kit_parameter_values
{
    Kit_parameter_values values = editor::make_default_values(shape, 1.0f, 3.0f);
    EXPECT_TRUE(values.set_float("height", height));
    EXPECT_TRUE(values.set_int("arms", arms));
    return values;
}

auto frame_names(const std::vector<Kit_light_frame>& frames) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for (const Kit_light_frame& frame : frames) {
        names.emplace_back(frame.name);
    }
    return names;
}

TEST(Kit_light, frames_follow_the_values)
{
    const Kit_shape_definition shape = make_lamp_post(4);
    std::vector<Kit_light_frame> frames;
    editor::get_shape_light_frames(make_values(shape, 3.0f, 2), frames);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frame_names(frames), (std::vector<std::string>{"arm_0", "arm_1"}));
    EXPECT_FLOAT_EQ(frames[0].position.y, 3.0f);
    EXPECT_TRUE(editor::check_light_frames(frames).empty());

    editor::get_shape_light_frames(make_values(shape, 5.5f, 4), frames);
    ASSERT_EQ(frames.size(), 4u);
    for (const Kit_light_frame& frame : frames) {
        EXPECT_FLOAT_EQ(frame.position.y, 5.5f);
    }
    // The starting values of P2: point, warm white, 8 m.
    EXPECT_EQ(frames[0].start.type, Kit_light_type::point);
    EXPECT_FLOAT_EQ(frames[0].start.range, 8.0f);
    EXPECT_GT(frames[0].start.color.r, frames[0].start.color.b);

    editor::get_shape_light_frames(make_values(shape, 3.0f, 0), frames);
    EXPECT_TRUE(frames.empty());
}

TEST(Kit_light, a_shape_without_light_frames_states_none)
{
    const Kit_shape_definition& block = editor::kit_test::get_shape("basic.block");
    std::vector<Kit_light_frame> frames{Kit_light_frame{.name = "stale"}};
    editor::get_shape_light_frames(editor::kit_test::defaults(block), frames);
    EXPECT_TRUE(frames.empty());
}

TEST(Kit_light, at_most_four_frames)
{
    const Kit_shape_definition shape = make_lamp_post(5);
    std::vector<Kit_light_frame> frames;
    editor::get_shape_light_frames(make_values(shape, 3.0f, 4), frames);
    EXPECT_TRUE(editor::check_light_frames(frames).empty());
    editor::get_shape_light_frames(make_values(shape, 3.0f, 5), frames);
    ASSERT_EQ(frames.size(), 5u);
    EXPECT_EQ(editor::check_light_frames(frames), "5 light frames, at most 4");
}

TEST(Kit_light, check_refuses_bad_frames)
{
    const Kit_light_frame good{.name = "lamp", .position = glm::vec3{0.0f, 1.0f, 0.0f}};
    EXPECT_TRUE(editor::check_light_frames(std::vector<Kit_light_frame>{good}).empty());

    Kit_light_frame unnamed = good;
    unnamed.name = "";
    EXPECT_FALSE(editor::check_light_frames(std::vector<Kit_light_frame>{unnamed}).empty());
    EXPECT_FALSE(editor::check_light_frames(std::vector<Kit_light_frame>{good, good}).empty()); // a repeated name

    Kit_light_frame long_direction = good;
    long_direction.direction = glm::vec3{0.0f, -2.0f, 0.0f};
    EXPECT_FALSE(editor::check_light_frames(std::vector<Kit_light_frame>{long_direction}).empty());

    Kit_light_frame no_range = good;
    no_range.start.range = 0.0f;
    EXPECT_FALSE(editor::check_light_frames(std::vector<Kit_light_frame>{no_range}).empty());

    Kit_light_frame spot = good;
    spot.start.type             = Kit_light_type::spot;
    spot.start.inner_spot_angle = 0.4f;
    spot.start.outer_spot_angle = 0.8f;
    EXPECT_TRUE(editor::check_light_frames(std::vector<Kit_light_frame>{spot}).empty());
    spot.start.inner_spot_angle = 1.0f; // inner beyond outer
    EXPECT_FALSE(editor::check_light_frames(std::vector<Kit_light_frame>{spot}).empty());
}

// Every library shape states at most four valid light frames at its defaults and at a seed-derived second set.
TEST(Kit_light, every_library_shape_states_valid_light_frames)
{
    std::vector<Kit_light_frame> frames;
    for (const Kit_shape_definition& shape : editor::kit_test::get_library().get_shapes()) {
        SCOPED_TRACE(shape.id);
        editor::get_shape_light_frames(editor::kit_test::defaults(shape), frames);
        EXPECT_EQ(editor::check_light_frames(frames), "");
        editor::get_shape_light_frames(editor::kit_test::all_derived(shape, 4242), frames);
        EXPECT_EQ(editor::check_light_frames(frames), "");
    }
}

TEST(Kit_light, rotation_shines_along_the_direction)
{
    const std::array<glm::vec3, 5> directions{
        glm::vec3{0.0f, -1.0f, 0.0f},
        glm::vec3{0.0f, 1.0f, 0.0f},
        glm::vec3{0.0f, 0.0f, -1.0f},
        glm::normalize(glm::vec3{1.0f, -1.0f, 0.5f}),
        glm::vec3{1.0f, 0.0f, 0.0f}
    };
    for (const glm::vec3& direction : directions) {
        const glm::quat rotation = editor::get_light_frame_rotation(direction);
        const glm::vec3 shine    = rotation * glm::vec3{0.0f, 0.0f, -1.0f};
        EXPECT_NEAR(glm::length(shine - direction), 0.0f, 1.0e-5f);
        EXPECT_NEAR(glm::length(rotation), 1.0f, 1.0e-5f);
    }
    // A horizontal direction keeps the node upright.
    const glm::quat level = editor::get_light_frame_rotation(glm::vec3{1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(glm::length((level * glm::vec3{0.0f, 1.0f, 0.0f}) - glm::vec3{0.0f, 1.0f, 0.0f}), 0.0f, 1.0e-5f);
    // Shining along -Z is the identity.
    const glm::quat forward = editor::get_light_frame_rotation(glm::vec3{0.0f, 0.0f, -1.0f});
    EXPECT_NEAR(std::abs(forward.w), 1.0f, 1.0e-6f);
}

TEST(Kit_light, a_node_is_at_its_frame_within_the_tolerance)
{
    const Kit_light_frame frame{.name = "lamp", .position = glm::vec3{0.5f, 2.0f, 0.0f}, .direction = glm::vec3{0.0f, -1.0f, 0.0f}};
    const glm::quat rotation = editor::get_light_frame_rotation(frame.direction);
    EXPECT_TRUE (editor::is_at_light_frame(frame.position, rotation, glm::vec3{1.0f}, frame));
    EXPECT_TRUE (editor::is_at_light_frame(frame.position, -rotation, glm::vec3{1.0f}, frame)); // the other quaternion of the rotation
    EXPECT_FALSE(editor::is_at_light_frame(frame.position + glm::vec3{0.0f, 0.01f, 0.0f}, rotation, glm::vec3{1.0f}, frame));
    EXPECT_FALSE(editor::is_at_light_frame(frame.position, editor::get_light_frame_rotation(glm::vec3{1.0f, 0.0f, 0.0f}), glm::vec3{1.0f}, frame));
    EXPECT_FALSE(editor::is_at_light_frame(frame.position, rotation, glm::vec3{2.0f}, frame));
}

// A frame with a direction off the axes: the node's local rotation shines along it and the check accepts it.
TEST(Kit_light, a_tilted_frame_is_held_by_its_local_rotation)
{
    const Kit_light_frame frame{.name = "tilted", .position = glm::vec3{0.3f, 1.1f, -0.2f}, .direction = glm::normalize(glm::vec3{0.4f, -0.8f, 0.3f})};
    EXPECT_EQ(editor::check_light_frames(std::vector<Kit_light_frame>{frame}), "");
    const glm::quat rotation = editor::get_light_frame_rotation(frame.direction);
    EXPECT_NEAR(glm::length((rotation * glm::vec3{0.0f, 0.0f, -1.0f}) - frame.direction), 0.0f, 1.0e-5f);
    EXPECT_TRUE(editor::is_at_light_frame(frame.position, rotation, glm::vec3{1.0f}, frame));
}

TEST(Kit_light, nodes_match_frames_by_name)
{
    const Kit_shape_definition shape = make_lamp_post(4);
    std::vector<Kit_light_frame> frames;
    editor::get_shape_light_frames(make_values(shape, 3.0f, 3), frames);
    Kit_light_match match;

    // Every node finds its frame whatever the child order.
    editor::match_light_frames(frames, std::vector<std::string>{"arm_2", "arm_0", "arm_1"}, match);
    EXPECT_EQ(match.frame_nodes, (std::vector<std::size_t>{1, 2, 0}));
    EXPECT_TRUE(match.removed_nodes.empty());
    EXPECT_TRUE(match.released_nodes.empty());

    // Fewer arms: the nodes of the frames that went are removed; more arms: the new frames need nodes.
    editor::get_shape_light_frames(make_values(shape, 3.0f, 1), frames);
    editor::match_light_frames(frames, std::vector<std::string>{"arm_0", "arm_1", "arm_2"}, match);
    EXPECT_EQ(match.frame_nodes, (std::vector<std::size_t>{0}));
    EXPECT_EQ(match.removed_nodes, (std::vector<std::size_t>{1, 2}));
    editor::get_shape_light_frames(make_values(shape, 3.0f, 4), frames);
    editor::match_light_frames(frames, std::vector<std::string>{"arm_1"}, match);
    EXPECT_EQ(match.frame_nodes, (std::vector<std::size_t>{c_no_light_node, 0, c_no_light_node, c_no_light_node}));
    EXPECT_TRUE(match.removed_nodes.empty());

    // A second node of a stated frame (a user's copy of a kit light) is released, kept as a plain light; a node of a
    // frame the values do not state is removed.
    editor::match_light_frames(frames, std::vector<std::string>{"arm_3", "arm_3", "lamp", "arm_1", "arm_1"}, match);
    EXPECT_EQ(match.frame_nodes, (std::vector<std::size_t>{c_no_light_node, 3, c_no_light_node, 0}));
    EXPECT_EQ(match.removed_nodes, (std::vector<std::size_t>{2}));
    EXPECT_EQ(match.released_nodes, (std::vector<std::size_t>{1, 4}));

    // No frames: every node goes.
    editor::match_light_frames(std::vector<Kit_light_frame>{}, std::vector<std::string>{"arm_0"}, match);
    EXPECT_TRUE(match.frame_nodes.empty());
    EXPECT_EQ(match.removed_nodes, (std::vector<std::size_t>{0}));
    EXPECT_TRUE(match.released_nodes.empty());
}

// Replace matches the old shape's nodes against the new shape's frame names: a lamp post's arm_0 node is kept by a
// shape that has an arm_0 frame, and its other arms go.
TEST(Kit_light, replace_keeps_the_nodes_whose_names_the_new_shape_has)
{
    const Kit_shape_definition post = make_lamp_post(4);
    Kit_shape_definition lantern = make_lamp_post(1);
    lantern.id = "test.lantern";
    std::vector<Kit_light_frame> post_frames;
    editor::get_shape_light_frames(make_values(post, 3.0f, 3), post_frames);
    std::vector<Kit_light_frame> lantern_frames;
    editor::get_shape_light_frames(make_values(lantern, 1.0f, 1), lantern_frames);
    Kit_light_match match;
    editor::match_light_frames(lantern_frames, frame_names(post_frames), match);
    EXPECT_EQ(match.frame_nodes, (std::vector<std::size_t>{0}));
    EXPECT_EQ(match.removed_nodes, (std::vector<std::size_t>{1, 2}));
}

} // anonymous namespace
