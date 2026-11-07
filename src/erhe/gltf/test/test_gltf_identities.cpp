// Stable identities and transforms (doc/plans/prepared_assets.md R6.1, R6.2;
// doc/editor/scene_serialization.md "Unique IDs" and "Save pipeline"):
// derived uids at the parse, no uid assignment at export, and an import root
// written as no node only on the identity path.

#include "gltf_test_util.hpp"

#include "erhe_gltf/gltf_item_flags.hpp"
#include "erhe_gltf/gltf_uid.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <fmt/format.h>

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using erhe_gltf_test::parse_file;
using erhe_gltf_test::write_temporary_file;

template <typename T>
[[nodiscard]] auto find_by_name(const std::vector<std::shared_ptr<T>>& items, const std::string_view name) -> std::shared_ptr<T>
{
    for (const std::shared_ptr<T>& item : items) {
        if (item && (item->get_name() == name)) {
            return item;
        }
    }
    return {};
}

void make_content(erhe::Item_base& item)
{
    item.enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui);
}

[[nodiscard]] auto same_bits(const glm::mat4& a, const glm::mat4& b) -> bool
{
    return std::memcmp(&a, &b, sizeof(glm::mat4)) == 0;
}

[[nodiscard]] auto make_trs(const glm::vec3 translation, const glm::quat rotation, const glm::vec3 scale) -> erhe::scene::Trs_transform
{
    erhe::scene::Trs_transform transform{};
    transform.set_trs(translation, rotation, scale);
    return transform;
}

// Node 1 is named with the uid node 0 would derive first, so node 0 must take the next one.
[[nodiscard]] auto foreign_gltf_text(const std::string_view file_name) -> std::string
{
    const std::string colliding_name = erhe::gltf::make_derived_gltf_uid(fmt::format("{}|node|0", file_name));
    return fmt::format(
        R"({{"asset":{{"version":"2.0"}},"scene":0,"scenes":[{{"nodes":[0,1,2]}}],)"
        R"("nodes":[{{"name":"a"}},{{"name":"{}"}},{{"name":"c","uid":"CarriedUid000001"}}],)"
        R"("materials":[{{"name":"m0"}},{{"name":"m1"}}]}})",
        colliding_name
    );
}

} // namespace

TEST(Gltf_identities, derived_uid_depends_only_on_its_seed)
{
    const std::string a = erhe::gltf::make_derived_gltf_uid("file.glb|node|0");
    EXPECT_EQ(a.size(), 16u);
    EXPECT_EQ(a, erhe::gltf::make_derived_gltf_uid("file.glb|node|0"));
    EXPECT_NE(a, erhe::gltf::make_derived_gltf_uid("file.glb|node|1"));
    EXPECT_NE(a, erhe::gltf::make_derived_gltf_uid("other.glb|node|0"));
    for (const char c : a) {
        EXPECT_TRUE(((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')));
    }
}

// R6.2: an object without a uid takes one derived from the file name, its
// kind and index, the same in every parse; a carried uid is kept, and a
// derived uid equal to a name of the file is derived again.
TEST(Gltf_identities, parse_derives_the_same_uids_every_time)
{
    const std::string text = foreign_gltf_text("identities.gltf");
    const std::filesystem::path path = write_temporary_file("identities.gltf", text);

    erhe::scene::Scene          scene_1{"parse_1", nullptr};
    const erhe::gltf::Gltf_data data_1 = parse_file(path, scene_1);
    erhe::scene::Scene          scene_2{"parse_2", nullptr};
    const erhe::gltf::Gltf_data data_2 = parse_file(path, scene_2);

    ASSERT_EQ(data_1.nodes.size(), 3u);
    ASSERT_EQ(data_2.nodes.size(), 3u);
    ASSERT_EQ(data_1.materials.size(), 2u);
    std::unordered_set<std::string> uids;
    for (std::size_t i = 0; i < 3; ++i) {
        ASSERT_TRUE(data_1.nodes[i] && data_2.nodes[i]);
        EXPECT_EQ(data_1.nodes[i]->get_gltf_uid(), data_2.nodes[i]->get_gltf_uid()) << i;
        uids.insert(data_1.nodes[i]->get_gltf_uid());
    }
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(data_1.materials[i]->get_gltf_uid(), data_2.materials[i]->get_gltf_uid()) << i;
        uids.insert(data_1.materials[i]->get_gltf_uid());
    }
    EXPECT_EQ(uids.size(), 5u);
    EXPECT_EQ(data_1.nodes[0]->get_gltf_uid(), erhe::gltf::make_derived_gltf_uid("identities.gltf|node|0#1"));
    EXPECT_NE(data_1.nodes[0]->get_gltf_uid(), data_1.nodes[1]->get_name());
    EXPECT_EQ(data_1.nodes[1]->get_gltf_uid(), erhe::gltf::make_derived_gltf_uid("identities.gltf|node|1"));
    EXPECT_EQ(data_1.nodes[2]->get_gltf_uid(), "CarriedUid000001");
    EXPECT_EQ(data_1.materials[0]->get_gltf_uid(), erhe::gltf::make_derived_gltf_uid("identities.gltf|material|0"));

    // The same content under another file name derives other uids.
    const std::filesystem::path other_path = write_temporary_file("identities_other.gltf", text);
    erhe::scene::Scene          scene_3{"parse_3", nullptr};
    const erhe::gltf::Gltf_data data_3 = parse_file(other_path, scene_3);
    ASSERT_EQ(data_3.nodes.size(), 3u);
    EXPECT_NE(data_3.nodes[1]->get_gltf_uid(), data_1.nodes[1]->get_gltf_uid());
}

// R6.2: export writes each item's uid and assigns none; an item keeps its uid.
// A uid two items share is written once as it is and once derived from it,
// the same in every export.
TEST(Gltf_identities, export_writes_item_uids_and_assigns_none)
{
    erhe::scene::Scene scene{"source", nullptr};
    std::vector<std::shared_ptr<erhe::scene::Xform>> nodes;
    for (const char* const name : {"n0", "n1", "n2"}) {
        const std::shared_ptr<erhe::scene::Xform> node = std::make_shared<erhe::scene::Xform>(name);
        make_content(*node);
        node->set_parent(scene.get_root_node());
        nodes.push_back(node);
    }
    nodes[2]->set_gltf_uid(nodes[1]->get_gltf_uid());
    std::vector<std::string> uids_before;
    for (const std::shared_ptr<erhe::scene::Xform>& node : nodes) {
        ASSERT_EQ(node->get_gltf_uid().size(), 16u);
        uids_before.push_back(node->get_gltf_uid());
    }

    const std::string text_1 = erhe::gltf::export_gltf(*scene.get_root_node(), true);
    const std::string text_2 = erhe::gltf::export_gltf(*scene.get_root_node(), true);
    EXPECT_EQ(text_1, text_2);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        EXPECT_EQ(nodes[i]->get_gltf_uid(), uids_before[i]) << i;
    }

    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("export_uids.glb", text_1), loaded);
    const std::shared_ptr<erhe::scene::Node> n0 = find_by_name(data.nodes, "n0");
    const std::shared_ptr<erhe::scene::Node> n1 = find_by_name(data.nodes, "n1");
    const std::shared_ptr<erhe::scene::Node> n2 = find_by_name(data.nodes, "n2");
    ASSERT_TRUE(n0 && n1 && n2);
    EXPECT_EQ(n0->get_gltf_uid(), uids_before[0]);
    EXPECT_EQ(n1->get_gltf_uid(), uids_before[1]);
    EXPECT_EQ(n2->get_gltf_uid(), erhe::gltf::make_derived_gltf_uid(uids_before[1] + "#1"));
}

TEST(Gltf_identities, transparent_import_root_needs_both_flags)
{
    EXPECT_TRUE (erhe::gltf::is_transparent_import_root(erhe::Item_flags::import_root | erhe::Item_flags::world_space));
    EXPECT_FALSE(erhe::gltf::is_transparent_import_root(erhe::Item_flags::import_root));
    EXPECT_FALSE(erhe::gltf::is_transparent_import_root(erhe::Item_flags::world_space));
}

// R6.1: an import root on the identity path is written as no node, a
// transformed one as a node with its transform; either way every world
// transform after the reload is bit-identical, and the children keep their
// local transforms.
TEST(Gltf_identities, import_root_written_as_node_only_when_transformed)
{
    const erhe::scene::Trs_transform root_transform = make_trs(
        glm::vec3{1.25f, -0.5f, 3.0f}, glm::normalize(glm::quat{0.9f, 0.1f, -0.3f, 0.2f}), glm::vec3{2.0f, 2.0f, 2.0f}
    );
    const erhe::scene::Trs_transform child_transform = make_trs(
        glm::vec3{0.1f, 0.2f, -0.7f}, glm::normalize(glm::quat{0.3f, 0.7f, 0.1f, -0.2f}), glm::vec3{0.5f, 1.5f, 1.0f}
    );

    erhe::scene::Scene scene{"source", nullptr};
    const std::shared_ptr<erhe::scene::Xform> import_root = std::make_shared<erhe::scene::Xform>("file.glb");
    import_root->enable_flag_bits(erhe::Item_flags::content | erhe::Item_flags::show_in_ui | erhe::Item_flags::import_root);
    import_root->set_parent(scene.get_root_node());
    const std::shared_ptr<erhe::scene::Xform> child = std::make_shared<erhe::scene::Xform>("child");
    make_content(*child);
    child->set_parent(import_root);
    child->set_parent_from_node(child_transform);
    scene.update_node_transforms();
    ASSERT_TRUE(erhe::gltf::is_transparent_import_root(import_root->get_flag_bits()));

    // Identity: no node for the import root, the child in its place.
    {
        erhe::scene::Scene          loaded{"loaded_identity", nullptr};
        const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("import_root_identity.glb", erhe::gltf::export_gltf(*scene.get_root_node(), true)), loaded);
        loaded.update_node_transforms();
        EXPECT_FALSE(find_by_name(data.nodes, "file.glb"));
        const std::shared_ptr<erhe::scene::Node> loaded_child = find_by_name(data.nodes, "child");
        ASSERT_TRUE(loaded_child);
        EXPECT_TRUE(same_bits(loaded_child->world_from_node(), child->world_from_node()));
        EXPECT_EQ(loaded_child->get_gltf_uid(), child->get_gltf_uid());
    }

    // Transformed: the import root is a node with its transform; the transform edit cleared world_space.
    import_root->set_parent_from_node(root_transform);
    scene.update_node_transforms();
    child->update_world_from_node(); // an unhosted scene propagates nothing
    ASSERT_FALSE(erhe::gltf::is_transparent_import_root(import_root->get_flag_bits()));
    {
        erhe::scene::Scene          loaded{"loaded_transformed", nullptr};
        const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("import_root_transformed.glb", erhe::gltf::export_gltf(*scene.get_root_node(), true)), loaded);
        loaded.update_node_transforms();
        const std::shared_ptr<erhe::scene::Node> loaded_root  = find_by_name(data.nodes, "file.glb");
        const std::shared_ptr<erhe::scene::Node> loaded_child = find_by_name(data.nodes, "child");
        ASSERT_TRUE(loaded_root && loaded_child);
        EXPECT_EQ(loaded_child->get_parent_node(), loaded_root);
        EXPECT_EQ(loaded_root->get_gltf_uid(), import_root->get_gltf_uid());
        EXPECT_TRUE(same_bits(loaded_root->parent_from_node(),  import_root->parent_from_node()));
        EXPECT_TRUE(same_bits(loaded_child->parent_from_node(), child->parent_from_node()));
        EXPECT_TRUE(same_bits(loaded_child->world_from_node(),  child->world_from_node()));
        // import_root is not serialized: the reloaded node is plain content.
        EXPECT_EQ(loaded_root->get_flag_bits() & erhe::Item_flags::import_root, uint64_t{0});
    }

    // The identity set back restores world_space, and with it the unwritten import root.
    import_root->set_parent_from_node(erhe::scene::Trs_transform{});
    EXPECT_TRUE(erhe::gltf::is_transparent_import_root(import_root->get_flag_bits()));
    {
        erhe::scene::Scene          loaded{"loaded_restored", nullptr};
        const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("import_root_restored.glb", erhe::gltf::export_gltf(*scene.get_root_node(), true)), loaded);
        EXPECT_FALSE(find_by_name(data.nodes, "file.glb"));
    }
}

// The parse parents a node before it sets the node's transform; a
// world-preserving parenting there left a skew of inverse(parent world) in
// the child that set_trs() keeps, so the child's local matrix came out a few
// ulps off its TRS under a rotated, scaled parent.
TEST(Gltf_identities, parse_keeps_local_transforms_under_a_transformed_parent)
{
    const float half = glm::radians(35.0f) * 0.5f;
    const erhe::scene::Trs_transform parent_transform = make_trs(
        glm::vec3{0.75f, -0.25f, 1.5f}, glm::quat{std::cos(half), 0.0f, std::sin(half), 0.0f}, glm::vec3{1.5f, 1.5f, 1.5f}
    );
    const erhe::scene::Trs_transform child_transform{glm::vec3{-2.2f, 0.4f, 0.6f}};

    erhe::scene::Scene scene{"source", nullptr};
    const std::shared_ptr<erhe::scene::Xform> parent = std::make_shared<erhe::scene::Xform>("parent");
    make_content(*parent);
    parent->set_parent(scene.get_root_node());
    parent->set_parent_from_node(parent_transform);
    const std::shared_ptr<erhe::scene::Xform> child = std::make_shared<erhe::scene::Xform>("child");
    make_content(*child);
    erhe::scene::set_prim_parent(child, parent);
    child->set_parent_from_node(child_transform);
    child->update_world_from_node();

    erhe::scene::Scene          loaded{"loaded", nullptr};
    const erhe::gltf::Gltf_data data = parse_file(write_temporary_file("transformed_parent.glb", erhe::gltf::export_gltf(*scene.get_root_node(), true)), loaded);
    const std::shared_ptr<erhe::scene::Node> loaded_child = find_by_name(data.nodes, "child");
    ASSERT_TRUE(loaded_child);
    EXPECT_TRUE(same_bits(loaded_child->parent_from_node(), child_transform.get_matrix()));
    EXPECT_TRUE(same_bits(loaded_child->world_from_node(), child->world_from_node()));
}
