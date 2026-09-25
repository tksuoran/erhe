// Bind (rigid), doc/plans/rigging/skeleton_editing.md R18: the nearest
// head-tail segment rule of the rigid weights and the rest world transforms
// the inverse bind matrices come from (anchored as
// erhe::scene::get_bind_pose_parent_from_node reads them back).

#include "rig/bone_bind.hpp"
#include "scene/rig_properties.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

using Node = erhe::scene::Node;
using erhe::scene::Trs_transform;
using editor::Bone_segment;
using editor::Rig;

[[nodiscard]] auto mat_close(const glm::mat4& a, const glm::mat4& b, const float tolerance = 1.0e-5f) -> bool
{
    for (int column = 0; column < 4; ++column) {
        if (glm::length(a[column] - b[column]) > tolerance) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto make_node(const std::string& name, const std::shared_ptr<Node>& parent, const bool bone) -> std::shared_ptr<Node>
{
    std::shared_ptr<Node> node = std::make_shared<erhe::scene::Xform>(name);
    if (bone) {
        node->enable_flag_bits(erhe::Item_flags::bone);
    }
    if (parent) {
        node->set_parent(parent);
    }
    return node;
}

void set_rest(Node& bone, const Trs_transform& rest)
{
    bone.set_value(Rig::rest_translation_property(), rest.get_translation());
    bone.set_value(Rig::rest_rotation_property(),    rest.get_rotation());
    bone.set_value(Rig::rest_scale_property(),       rest.get_scale());
}

// Interior points measure to the segment, beyond the ends to the end points.
TEST(Bone_bind, point_segment_distance_clamps_to_the_ends)
{
    const Bone_segment segment{.head = glm::vec3{0.0f, 0.0f, 0.0f}, .tail = glm::vec3{0.0f, 2.0f, 0.0f}};
    EXPECT_FLOAT_EQ(editor::get_point_segment_distance_squared(glm::vec3{3.0f, 1.0f, 0.0f}, segment), 9.0f);
    EXPECT_FLOAT_EQ(editor::get_point_segment_distance_squared(glm::vec3{0.0f, 5.0f, 0.0f}, segment), 9.0f);
    EXPECT_FLOAT_EQ(editor::get_point_segment_distance_squared(glm::vec3{0.0f, -1.0f, 1.0f}, segment), 2.0f);
    const Bone_segment point{.head = glm::vec3{1.0f, 1.0f, 1.0f}, .tail = glm::vec3{1.0f, 1.0f, 1.0f}};
    EXPECT_FLOAT_EQ(editor::get_point_segment_distance_squared(glm::vec3{1.0f, 3.0f, 1.0f}, point), 4.0f);
}

// A chain along +Y: each point binds to the segment it is beside; a point
// equally near two segments (the shared joint) binds to the lower index.
TEST(Bone_bind, nearest_segment_of_a_chain)
{
    const std::array<Bone_segment, 3> chain{{
        {.head = glm::vec3{0.0f, 0.0f, 0.0f}, .tail = glm::vec3{0.0f, 1.0f, 0.0f}},
        {.head = glm::vec3{0.0f, 1.0f, 0.0f}, .tail = glm::vec3{0.0f, 2.0f, 0.0f}},
        {.head = glm::vec3{0.0f, 2.0f, 0.0f}, .tail = glm::vec3{0.0f, 3.0f, 0.0f}}
    }};
    EXPECT_EQ(editor::find_nearest_bone_segment(chain, glm::vec3{0.3f, 0.5f, 0.2f}), 0u);
    EXPECT_EQ(editor::find_nearest_bone_segment(chain, glm::vec3{0.3f, 1.5f, 0.2f}), 1u);
    EXPECT_EQ(editor::find_nearest_bone_segment(chain, glm::vec3{0.3f, 2.5f, 0.2f}), 2u);
    EXPECT_EQ(editor::find_nearest_bone_segment(chain, glm::vec3{0.3f, 3.9f, 0.2f}), 2u) << "beyond the last tail";
    EXPECT_EQ(editor::find_nearest_bone_segment(chain, glm::vec3{0.3f, -0.9f, 0.2f}), 0u) << "below the first head";
    EXPECT_EQ(editor::find_nearest_bone_segment(chain, glm::vec3{0.5f, 1.0f, 0.0f}), 0u) << "a tie goes to the lower index";

    // A side branch: a point off the chain's axis binds to the branch it is beside.
    const std::array<Bone_segment, 2> branch{{
        {.head = glm::vec3{0.0f, 0.0f, 0.0f}, .tail = glm::vec3{0.0f, 2.0f, 0.0f}},
        {.head = glm::vec3{0.0f, 1.0f, 0.0f}, .tail = glm::vec3{2.0f, 1.0f, 0.0f}}
    }};
    EXPECT_EQ(editor::find_nearest_bone_segment(branch, glm::vec3{1.5f, 1.2f, 0.0f}), 1u);
    EXPECT_EQ(editor::find_nearest_bone_segment(branch, glm::vec3{0.1f, 1.8f, 0.0f}), 0u);
}

// The rest world transforms compose the joints' Rig.rest_* values down the
// chain, whatever the joints' current pose; the frame above the top joint is
// its parent's current world transform, and a non-joint node between two
// joints contributes its current local transform.
TEST(Bone_bind, rest_world_transforms_compose_the_rest_values)
{
    const std::shared_ptr<Node> base = make_node("base", {}, false);
    base->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f}) * glm::rotate(glm::mat4{1.0f}, 0.3f, glm::vec3{0.0f, 0.0f, 1.0f}));
    const std::shared_ptr<Node> upper  = make_node("upper",  base,  true);
    const std::shared_ptr<Node> spacer = make_node("spacer", upper, false);
    const std::shared_ptr<Node> lower  = make_node("lower",  spacer, true);
    const std::shared_ptr<Node> hand   = make_node("hand",   lower, true);

    const Trs_transform upper_rest{glm::vec3{0.0f, 1.0f, 0.0f}, glm::angleAxis(0.4f, glm::vec3{1.0f, 0.0f, 0.0f})};
    const Trs_transform lower_rest{glm::vec3{0.0f, 2.0f, 0.0f}, glm::angleAxis(-0.2f, glm::vec3{0.0f, 0.0f, 1.0f})};
    const Trs_transform hand_rest {glm::vec3{0.0f, 1.5f, 0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{2.0f}};
    set_rest(*upper, upper_rest);
    set_rest(*lower, lower_rest);
    set_rest(*hand,  hand_rest);
    const glm::mat4 spacer_local = glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 0.5f, 0.25f});
    spacer->set_parent_from_node(spacer_local);

    // The current pose is off rest; the rest world transforms do not read it.
    upper->set_parent_from_node(glm::rotate(glm::mat4{1.0f}, 1.1f, glm::vec3{0.0f, 1.0f, 0.0f}));
    lower->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{3.0f, 0.0f, 0.0f}));

    const glm::mat4 upper_world = base->world_from_node() * upper_rest.get_matrix();
    const glm::mat4 lower_world = upper_world * spacer_local * lower_rest.get_matrix();
    const glm::mat4 hand_world  = lower_world * hand_rest.get_matrix();

    // Any order: the hand before its ancestors.
    const std::vector<std::shared_ptr<Node>> joints{hand, upper, lower};
    const std::vector<glm::mat4> rest_world = editor::get_rest_world_transforms(joints);
    ASSERT_EQ(rest_world.size(), 3u);
    EXPECT_TRUE(mat_close(rest_world[0], hand_world));
    EXPECT_TRUE(mat_close(rest_world[1], upper_world));
    EXPECT_TRUE(mat_close(rest_world[2], lower_world));

    // Without the upper bone among the joints, the lower bone's frame above is
    // the spacer's current world (through the upper bone's current pose): the
    // product of the current locals, as a node outside a scene carries it.
    const std::vector<std::shared_ptr<Node>> partial{lower};
    const std::vector<glm::mat4> partial_world = editor::get_rest_world_transforms(partial);
    ASSERT_EQ(partial_world.size(), 1u);
    const glm::mat4 spacer_world = base->parent_from_node() * upper->parent_from_node() * spacer_local;
    EXPECT_TRUE(mat_close(partial_world[0], spacer_world * lower_rest.get_matrix()));

    // The segments: head at the rest origin, tail at the rest Rig.tail.
    hand->set_value(Rig::tail_property(), glm::vec3{0.0f, 0.5f, 0.0f});
    const std::vector<Bone_segment> segments = editor::get_rest_bone_segments(joints, rest_world);
    ASSERT_EQ(segments.size(), 3u);
    EXPECT_LT(glm::length(segments[0].head - glm::vec3{hand_world[3]}), 1.0e-5f);
    EXPECT_LT(glm::length(segments[0].tail - glm::vec3{hand_world * glm::vec4{0.0f, 0.5f, 0.0f, 1.0f}}), 1.0e-5f);
}

} // anonymous namespace
