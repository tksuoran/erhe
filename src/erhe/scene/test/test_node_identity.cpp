// R6.2 (doc/editor/scene_serialization.md "Unique IDs"): every node has its
// glTF uid from its creation, and a clone, a new object, takes its own.

#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

TEST(Node_identity, a_new_node_has_a_uid)
{
    const std::shared_ptr<erhe::scene::Xform> xform = std::make_shared<erhe::scene::Xform>("xform");
    const std::shared_ptr<erhe::scene::Mesh>  mesh  = std::make_shared<erhe::scene::Mesh>("mesh");
    EXPECT_EQ(xform->get_gltf_uid().size(), 16u);
    EXPECT_EQ(mesh->get_gltf_uid().size(), 16u);
    EXPECT_NE(xform->get_gltf_uid(), mesh->get_gltf_uid());
}

TEST(Node_identity, a_clone_takes_a_uid_of_its_own_and_the_source_keeps_its)
{
    const std::shared_ptr<erhe::scene::Mesh> source = std::make_shared<erhe::scene::Mesh>("source");
    source->set_gltf_uid("SourceUid0000001");
    const std::shared_ptr<erhe::scene::Mesh> clone = std::make_shared<erhe::scene::Mesh>(*source, erhe::for_clone{});
    EXPECT_EQ(clone->get_gltf_uid().size(), 16u);
    EXPECT_NE(clone->get_gltf_uid(), source->get_gltf_uid());
    EXPECT_EQ(source->get_gltf_uid(), "SourceUid0000001");
}

TEST(Node_identity, a_uid_survives_reparenting_and_transform_edits)
{
    erhe::scene::Scene scene{"scene", nullptr};
    const std::shared_ptr<erhe::scene::Xform> parent = std::make_shared<erhe::scene::Xform>("parent");
    const std::shared_ptr<erhe::scene::Xform> child  = std::make_shared<erhe::scene::Xform>("child");
    const std::string uid = child->get_gltf_uid();
    parent->set_parent(scene.get_root_node());
    child->set_parent(parent);
    child->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{1.0f, 2.0f, 3.0f}});
    scene.update_node_transforms();
    child->set_parent(scene.get_root_node());
    EXPECT_EQ(child->get_gltf_uid(), uid);
}

// The propagation pass and the parse's update give a node under an identity
// parent the same world bits: a mirrored TRS holds -0 terms that a product
// with the identity would turn into +0.
TEST(Node_identity, world_under_an_identity_parent_is_the_same_on_both_update_paths)
{
    erhe::scene::Scene scene{"scene", nullptr};
    const std::shared_ptr<erhe::scene::Xform> parent = std::make_shared<erhe::scene::Xform>("parent");
    const std::shared_ptr<erhe::scene::Xform> child  = std::make_shared<erhe::scene::Xform>("child");
    erhe::scene::set_prim_parent(parent, scene.get_root_node());
    erhe::scene::set_prim_parent(child, parent);
    erhe::scene::Trs_transform mirrored{};
    mirrored.set_trs(glm::vec3{0.0f, 0.5f, 0.8f}, glm::quat{0.9848077f, 0.0f, 0.17364818f, 0.0f}, glm::vec3{-1.0f, 1.25f, 1.0f});
    child->node_data.transforms.parent_from_node = mirrored;

    child->update_world_from_node();
    const glm::mat4 from_parse = child->world_from_node();
    child->update_transform(erhe::scene::Node_transforms::get_next_serial());
    const glm::mat4 from_propagation = child->world_from_node();
    EXPECT_EQ(std::memcmp(&from_parse, &from_propagation, sizeof(glm::mat4)), 0);
    EXPECT_EQ(std::memcmp(&from_propagation, &mirrored.get_matrix(), sizeof(glm::mat4)), 0);
}
