// The bone tail of doc/plans/rigging/skeleton_editing.md R3: the computed
// default of Rig.tail (rig/bone_tail.hpp) on bones no skin lists, the skinned
// inference on a skin's joints, and the local value overriding the default.

#include "rig/bone_tail.hpp"
#include "scene/rig_properties.hpp"

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/trs_transform.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using Node = erhe::scene::Node;
using editor::Rig;
using editor::compute_default_bone_tail;
using editor::infer_skinned_bone_tail;

[[nodiscard]] auto close(const glm::vec3& a, const glm::vec3& b, const float tolerance = 1.0e-5f) -> bool
{
    return glm::length(a - b) < tolerance;
}

enum class Node_kind : unsigned int {
    bone  = 0,
    plain = 1
};

[[nodiscard]] auto make_node(const std::string& name, const std::shared_ptr<Node>& parent, const glm::vec3 translation, const Node_kind kind) -> std::shared_ptr<Node>
{
    std::shared_ptr<Node> node = std::make_shared<erhe::scene::Xform>(name);
    if (kind == Node_kind::bone) {
        node->enable_flag_bits(erhe::Item_flags::bone);
    }
    if (parent) {
        node->set_parent(parent);
    }
    node->set_parent_from_node(erhe::scene::Trs_transform{translation});
    return node;
}

// Rule 1: the first bone child's head; a non-bone child (a proxy, a tip node,
// a mesh) does not count.
TEST(Bone_tail, unskinned_bone_points_at_its_first_bone_child)
{
    const std::shared_ptr<Node> root  = make_node("root",  {},   glm::vec3{0.0f},             Node_kind::bone);
    const std::shared_ptr<Node> plain = make_node("plain", root, glm::vec3{5.0f, 0.0f, 0.0f}, Node_kind::plain);
    const std::shared_ptr<Node> a     = make_node("a",     root, glm::vec3{0.0f, 2.0f, 0.0f}, Node_kind::bone);
    const std::shared_ptr<Node> b     = make_node("b",     root, glm::vec3{1.0f, 0.0f, 0.0f}, Node_kind::bone);
    EXPECT_TRUE(close(compute_default_bone_tail(*root), glm::vec3{0.0f, 2.0f, 0.0f}));
    EXPECT_TRUE(close(root->get_value(Rig::tail_property()), glm::vec3{0.0f, 2.0f, 0.0f})) << "Rig.tail defaults to the computed tail";

    // The default follows the child: nothing is cached.
    a->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{0.0f, 3.0f, 0.0f}});
    EXPECT_TRUE(close(root->get_value(Rig::tail_property()), glm::vec3{0.0f, 3.0f, 0.0f}));
}

// Rules 2 and 3: a leaf keeps its parent's bone length along +Y; a leaf with
// no bone parent is one unit long.
TEST(Bone_tail, unskinned_leaf_keeps_the_parent_bone_length)
{
    const std::shared_ptr<Node> holder = make_node("holder", {},     glm::vec3{0.0f},             Node_kind::plain);
    const std::shared_ptr<Node> root   = make_node("root",   holder, glm::vec3{0.0f},             Node_kind::bone);
    const std::shared_ptr<Node> leaf   = make_node("leaf",   root,   glm::vec3{0.0f, 0.5f, 0.0f}, Node_kind::bone);
    EXPECT_TRUE(close(compute_default_bone_tail(*leaf), glm::vec3{0.0f, 0.5f, 0.0f}));

    // An authored parent tail sets the length.
    root->set_value(Rig::tail_property(), glm::vec3{0.0f, 0.0f, 0.25f});
    EXPECT_TRUE(close(compute_default_bone_tail(*leaf), glm::vec3{0.0f, 0.25f, 0.0f}));

    const std::shared_ptr<Node> lone = make_node("lone", holder, glm::vec3{4.0f, 0.0f, 0.0f}, Node_kind::bone);
    EXPECT_TRUE(close(compute_default_bone_tail(*lone), glm::vec3{0.0f, 1.0f, 0.0f})) << "a leaf under a non-bone is one unit along +Y";
}

// A local value overrides the default; clearing it returns to the default.
TEST(Bone_tail, local_tail_overrides_the_default)
{
    const std::shared_ptr<Node> root  = make_node("root",  {},   glm::vec3{0.0f},             Node_kind::bone);
    const std::shared_ptr<Node> child = make_node("child", root, glm::vec3{0.0f, 2.0f, 0.0f}, Node_kind::bone);
    root->set_value(Rig::tail_property(), glm::vec3{1.0f, 1.0f, 0.0f});
    EXPECT_TRUE(close(root->get_value(Rig::tail_property()), glm::vec3{1.0f, 1.0f, 0.0f}));
    root->clear_value(Rig::tail_property());
    EXPECT_TRUE(close(root->get_value(Rig::tail_property()), glm::vec3{0.0f, 2.0f, 0.0f}));
    EXPECT_FALSE(child->get_value(Rig::connected_property())) << "Rig.connected defaults to false";
}

// The skinned inference: agreeing child joints give their translation; a leaf
// joint without skinned bounds points along +Y as long as its own offset.
TEST(Bone_tail, skinned_inference_uses_the_child_joints)
{
    const std::shared_ptr<Node> root  = make_node("root",  {},    glm::vec3{0.0f},             Node_kind::bone);
    const std::shared_ptr<Node> mid   = make_node("mid",   root,  glm::vec3{0.0f, 1.5f, 0.0f}, Node_kind::bone);
    const std::shared_ptr<Node> tip_a = make_node("tip_a", mid,   glm::vec3{0.3f, 0.4f, 0.0f}, Node_kind::bone);
    const std::shared_ptr<Node> tip_b = make_node("tip_b", mid,   glm::vec3{0.3f, 0.4f, 0.0f}, Node_kind::bone);

    erhe::scene::Skin skin{"skin"};
    skin.skin_data.joints = {root, mid, tip_a, tip_b};
    EXPECT_TRUE(close(infer_skinned_bone_tail(skin, 0), glm::vec3{0.0f, 1.5f, 0.0f}));
    EXPECT_TRUE(close(infer_skinned_bone_tail(skin, 1), glm::vec3{0.3f, 0.4f, 0.0f})) << "children agreeing on a location";
    EXPECT_TRUE(close(infer_skinned_bone_tail(skin, 2), glm::vec3{0.0f, 0.5f, 0.0f})) << "a leaf as long as its own offset";
}

} // anonymous namespace
