// Skeleton walks behind the selection helpers and the mirror lookup
// (doc/plans/rigging/skeleton_editing.md R10, R12), on a small biped:
//
//   Armature (not a bone)
//     hips
//       spine
//         chest
//           neck
//             head
//           arm_L_1 - arm_L_2 - hand_L
//           arm_R_1 - arm_R_2 - hand_R
//       leg.L - shin.L
//       leg.R - shin.R
//         shin.R Proxy (not a bone)
//   Other (not a bone)
//     arm_L_1 (bone; its own skeleton, with a name the biped also uses)
//     arm_R_1 (bone; its own skeleton)

#include "rig/bone_hierarchy.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using Node = erhe::scene::Node;
using editor::Bone_select_mode;

class Biped
{
public:
    Biped()
    {
        armature = std::make_shared<erhe::scene::Xform>("Armature");
        other    = std::make_shared<erhe::scene::Xform>("Other");
        add("hips",    armature);
        add("spine",   bones["hips"]);
        add("chest",   bones["spine"]);
        add("neck",    bones["chest"]);
        add("head",    bones["neck"]);
        add("arm_L_1", bones["chest"]);
        add("arm_L_2", bones["arm_L_1"]);
        add("hand_L",  bones["arm_L_2"]);
        add("arm_R_1", bones["chest"]);
        add("arm_R_2", bones["arm_R_1"]);
        add("hand_R",  bones["arm_R_2"]);
        add("leg.L",   bones["hips"]);
        add("shin.L",  bones["leg.L"]);
        add("leg.R",   bones["hips"]);
        add("shin.R",  bones["leg.R"]);
        proxy = std::make_shared<erhe::scene::Xform>("shin.R Proxy");
        proxy->set_parent(bones["shin.R"]);

        other_arm_l = make_bone("arm_L_1");
        other_arm_l->set_parent(other);
        other_arm_r = make_bone("arm_R_1");
        other_arm_r->set_parent(other);
    }

    [[nodiscard]] static auto make_bone(const std::string& name) -> std::shared_ptr<Node>
    {
        std::shared_ptr<Node> node = std::make_shared<erhe::scene::Xform>(name);
        node->enable_flag_bits(erhe::Item_flags::bone);
        return node;
    }

    void add(const std::string& name, const std::shared_ptr<Node>& parent)
    {
        std::shared_ptr<Node> bone = make_bone(name);
        bone->set_parent(parent);
        bones[name] = bone;
    }

    [[nodiscard]] auto select(const std::vector<std::string>& targets, const Bone_select_mode mode) -> std::vector<std::string>
    {
        std::vector<std::shared_ptr<Node>> target_nodes;
        for (const std::string& name : targets) {
            target_nodes.push_back(bones.at(name));
        }
        std::vector<std::shared_ptr<Node>> out{bones.at("head")}; // cleared by the call
        editor::collect_bone_selection(target_nodes, mode, out);
        return names(out);
    }

    [[nodiscard]] static auto names(const std::vector<std::shared_ptr<Node>>& nodes) -> std::vector<std::string>
    {
        std::vector<std::string> result;
        for (const std::shared_ptr<Node>& node : nodes) {
            result.push_back(node->get_name());
        }
        return result;
    }

    std::shared_ptr<Node>                        armature;
    std::shared_ptr<Node>                        other;
    std::shared_ptr<Node>                        proxy;
    std::shared_ptr<Node>                        other_arm_l;
    std::shared_ptr<Node>                        other_arm_r;
    std::map<std::string, std::shared_ptr<Node>> bones;
};

using Names = std::vector<std::string>;

TEST(Bone_hierarchy, skeleton_root_is_the_topmost_bone)
{
    Biped biped;
    EXPECT_EQ(editor::get_skeleton_root(biped.bones["hand_L"]), biped.bones["hips"]);
    EXPECT_EQ(editor::get_skeleton_root(biped.bones["hips"]),   biped.bones["hips"]);
    EXPECT_EQ(editor::get_skeleton_root(biped.other_arm_l),     biped.other_arm_l);
    EXPECT_EQ(editor::get_skeleton_root(biped.proxy),           nullptr) << "not a bone";
}

TEST(Bone_hierarchy, select_parent)
{
    Biped biped;
    EXPECT_EQ(biped.select({"hand_L"}, Bone_select_mode::parent), (Names{"arm_L_2"}));
    EXPECT_EQ(biped.select({"hips"}, Bone_select_mode::parent), (Names{})) << "the Armature is not a bone";
    EXPECT_EQ(biped.select({"leg.L", "leg.R", "shin.L"}, Bone_select_mode::parent), (Names{"hips", "leg.L"})) << "deduplicated";
}

TEST(Bone_hierarchy, select_children_immediate_and_all)
{
    Biped biped;
    EXPECT_EQ(biped.select({"chest"}, Bone_select_mode::children), (Names{"neck", "arm_L_1", "arm_R_1"}));
    EXPECT_EQ(biped.select({"shin.R"}, Bone_select_mode::children), (Names{})) << "the proxy is not a bone";
    EXPECT_EQ(
        biped.select({"hips"}, Bone_select_mode::children_recursive),
        (Names{"spine", "chest", "neck", "head", "arm_L_1", "arm_L_2", "hand_L", "arm_R_1", "arm_R_2", "hand_R", "leg.L", "shin.L", "leg.R", "shin.R"})
    );
    EXPECT_EQ(biped.select({"leg.R"}, Bone_select_mode::children_recursive), (Names{"shin.R"}));
}

TEST(Bone_hierarchy, select_chain_stops_at_branches)
{
    Biped biped;
    // Up to the branching chest (excluded: it belongs to the chain above),
    // down to the leaf.
    EXPECT_EQ(biped.select({"arm_L_2"}, Bone_select_mode::chain), (Names{"arm_L_1", "arm_L_2", "hand_L"}));
    // A branching bone ends its chain: hips branches, so spine's chain is
    // spine - chest, and chest (branching) is its last bone.
    EXPECT_EQ(biped.select({"spine"}, Bone_select_mode::chain), (Names{"spine", "chest"}));
    EXPECT_EQ(biped.select({"chest"}, Bone_select_mode::chain), (Names{"spine", "chest"}));
    EXPECT_EQ(biped.select({"head"}, Bone_select_mode::chain), (Names{"neck", "head"}));
    // The skeleton root branches: it is a chain of one.
    EXPECT_EQ(biped.select({"hips"}, Bone_select_mode::chain), (Names{"hips"}));
    // Several targets: the union, in target order.
    EXPECT_EQ(biped.select({"shin.L", "leg.R"}, Bone_select_mode::chain), (Names{"leg.L", "shin.L", "leg.R", "shin.R"}));
}

TEST(Bone_hierarchy, mirror_counterpart_within_the_same_skeleton)
{
    Biped biped;
    EXPECT_EQ(editor::find_mirror_bone(biped.bones["hand_L"]),  biped.bones["hand_R"]);
    EXPECT_EQ(editor::find_mirror_bone(biped.bones["shin.R"]),  biped.bones["shin.L"]);
    // The same-named bone of another skeleton is never the counterpart.
    EXPECT_EQ(editor::find_mirror_bone(biped.bones["arm_L_1"]), biped.bones["arm_R_1"]);
    // Sibling bones under a non-bone node are two skeletons (each is its own
    // skeleton root), so they are not each other's counterparts.
    EXPECT_EQ(editor::find_mirror_bone(biped.other_arm_l),      nullptr);
    // No side, or no counterpart.
    EXPECT_EQ(editor::find_mirror_bone(biped.bones["spine"]),   nullptr);
    biped.bones["leg.R"]->set_name("thigh.R");
    EXPECT_EQ(editor::find_mirror_bone(biped.bones["leg.L"]),   nullptr);

    EXPECT_EQ(biped.select({"hand_L", "spine", "shin.L"}, Bone_select_mode::mirror), (Names{"hand_R", "shin.R"}));
}

} // anonymous namespace
