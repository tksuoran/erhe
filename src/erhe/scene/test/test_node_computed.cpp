// Computed properties on scene items (doc/property-system.md D26): a
// node's world transform components and child count, a mesh's world
// bounds - read from the owner's derived state, never stored, pushed to
// expressions where that state changes.

#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_primitive/primitive.hpp"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;

namespace {

class Test_scene_host : public erhe::scene::Scene_host
{
public:
    Test_scene_host() : scene{"test scene", this} {}

    auto get_host_name   () const -> const char*        override { return "Test_scene_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.register_node  (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.unregister_node(node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>&)        override {}
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void register_light   (const std::shared_ptr<erhe::scene::Light>&)         override {}
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&)         override {}
    void register_layout  (const std::shared_ptr<erhe::scene::Layout>&)        override {}
    void unregister_layout(const std::shared_ptr<erhe::scene::Layout>&)        override {}

    void on_mesh_primitives_changed    (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>&, uint64_t, uint64_t) override {}
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    erhe::scene::Scene scene;
};

auto approx(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f) -> bool
{
    return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3{eps}));
}

auto translated(const glm::vec3& translation) -> erhe::scene::Trs_transform
{
    erhe::scene::Trs_transform t;
    t.set_trs(translation, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f});
    return t;
}

// A primitive with a unit box as its bounding box and no GPU allocations.
auto make_unit_box_primitive() -> std::shared_ptr<erhe::primitive::Primitive>
{
    erhe::primitive::Buffer_mesh buffer_mesh{};
    buffer_mesh.bounding_box.include(glm::vec3{-1.0f});
    buffer_mesh.bounding_box.include(glm::vec3{ 1.0f});
    return std::make_shared<erhe::primitive::Primitive>(std::move(buffer_mesh));
}

} // anonymous namespace

TEST(Node_computed, world_transform_components_follow_the_propagation_pass)
{
    using erhe::scene::Node;
    using erhe::scene::Xform;
    Test_scene_host host;
    auto parent = std::make_shared<Xform>("Parent");
    auto child  = std::make_shared<Xform>("Child");
    auto light  = std::make_shared<erhe::scene::Light>("Lamp");
    parent->set_parent(host.scene.get_root_node());
    child->set_parent(parent);
    child->attach(light);

    EXPECT_TRUE(Node::world_translation_property.get().is_read_only());
    EXPECT_EQ(child->get_value_source(Node::world_translation_property.get()), Value_source::computed);
    EXPECT_FALSE(child->has_local_value(Node::world_translation_property.get()));

    child->set_value(Node::translation_property, glm::vec3{1.0f, 2.0f, 3.0f});
    parent->set_value(Node::scale_property, glm::vec3{2.0f});
    host.scene.update_node_transforms();
    EXPECT_TRUE(approx(child->get_value(Node::world_translation_property), glm::vec3{2.0f, 4.0f, 6.0f}));
    EXPECT_TRUE(approx(child->get_value(Node::world_scale_property), glm::vec3{2.0f}));
    EXPECT_TRUE(approx(parent->get_value(Node::world_translation_property), glm::vec3{0.0f}));

    // A light on the child reads the child's world position through an
    // expression; a parent move reaches it when the pass recomputes the
    // child's world transform.
    ASSERT_TRUE(light->set_expression(erhe::scene::Light::intensity_property, "{Child/world_translation.y}"));
    EXPECT_TRUE(light->get_expression_error(erhe::scene::Light::intensity_property).empty());
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 4.0f);

    parent->set_parent_from_node(translated(glm::vec3{0.0f, 10.0f, 0.0f}));
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 4.0f); // the child is only dirty so far
    host.scene.update_node_transforms();
    EXPECT_TRUE(approx(child->get_value(Node::world_translation_property), glm::vec3{1.0f, 12.0f, 3.0f}));
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 12.0f);

    // A rotation on the parent shows in the child's world rotation.
    erhe::scene::Trs_transform rotated;
    const glm::quat quarter_turn = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    rotated.set_trs(glm::vec3{0.0f}, quarter_turn, glm::vec3{1.0f});
    parent->set_parent_from_node(rotated);
    host.scene.update_node_transforms();
    const glm::quat world_rotation = child->get_value(Node::world_rotation_property);
    EXPECT_NEAR(std::abs(glm::dot(world_rotation, quarter_turn)), 1.0f, 1e-5f);

    // Writes are refused through every entry.
    EXPECT_FALSE(child->set_value(Node::world_translation_property.get(), Property_value{glm::vec3{0.0f}}));
    EXPECT_FALSE(child->set_expression(Node::world_translation_property.get(), "1, 2, 3"));
    EXPECT_FALSE(child->clear_value(Node::world_translation_property.get()));
}

TEST(Node_computed, child_count_follows_the_tree)
{
    using erhe::scene::Node;
    using erhe::scene::Xform;
    Test_scene_host host;
    auto parent = std::make_shared<Xform>("Parent");
    auto a      = std::make_shared<Xform>("A");
    auto b      = std::make_shared<Xform>("B");
    auto light  = std::make_shared<erhe::scene::Light>("Lamp");
    parent->set_parent(host.scene.get_root_node());
    parent->attach(light);
    EXPECT_EQ(parent->get_value(erhe::Hierarchy::child_count_property), 0);
    EXPECT_EQ(parent->get_value_source(erhe::Hierarchy::child_count_property.get()), Value_source::computed);

    ASSERT_TRUE(light->set_expression(erhe::scene::Light::intensity_property, "{Parent/child_count} * 100"));
    EXPECT_TRUE(light->get_expression_error(erhe::scene::Light::intensity_property).empty());
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 0.0f);

    a->set_parent(parent);
    EXPECT_EQ(parent->get_value(erhe::Hierarchy::child_count_property), 1);
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 100.0f);
    b->set_parent(parent);
    EXPECT_EQ(parent->get_value(erhe::Hierarchy::child_count_property), 2);
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 200.0f);
    a->set_parent(host.scene.get_root_node());
    EXPECT_EQ(parent->get_value(erhe::Hierarchy::child_count_property), 1);
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 100.0f);

    // Not a local value: bags and the glTF extras never see it.
    EXPECT_FALSE(parent->read_local_value(erhe::Hierarchy::child_count_property).has_value());
    bool listed = false;
    parent->for_each_local_value(
        [&listed](const Dependency_property& property, const Property_value&) {
            listed = listed || (&property == erhe::Hierarchy::child_count_property.get_ptr());
        }
    );
    EXPECT_FALSE(listed);
}

TEST(Node_computed, mesh_world_bounds_follow_the_node_and_the_primitives)
{
    using erhe::scene::Mesh;
    using erhe::scene::Node;
    using erhe::scene::Xform;
    Test_scene_host host;
    auto node  = std::make_shared<Xform>("Box");
    auto mesh  = std::make_shared<Mesh>("Box mesh");
    auto light = std::make_shared<erhe::scene::Light>("Lamp");
    node->set_parent(host.scene.get_root_node());
    node->attach(mesh);
    node->attach(light);

    // No primitives: an invalid box reads as zero.
    EXPECT_TRUE(approx(mesh->get_value(Mesh::world_bounds_min_property), glm::vec3{0.0f}));
    EXPECT_TRUE(approx(mesh->get_value(Mesh::world_bounds_max_property), glm::vec3{0.0f}));
    EXPECT_EQ(mesh->get_value_source(Mesh::world_bounds_min_property.get()), Value_source::computed);

    ASSERT_TRUE(light->set_expression(erhe::scene::Light::intensity_property, "{Box mesh/world_bounds_max.x}"));
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 0.0f);

    mesh->add_primitive(make_unit_box_primitive(), {});
    EXPECT_TRUE(approx(mesh->get_value(Mesh::world_bounds_min_property), glm::vec3{-1.0f}));
    EXPECT_TRUE(approx(mesh->get_value(Mesh::world_bounds_max_property), glm::vec3{ 1.0f}));
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 1.0f); // pushed by the primitive change

    node->set_parent_from_node(translated(glm::vec3{5.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(approx(mesh->get_value(Mesh::world_bounds_min_property), glm::vec3{4.0f, -1.0f, -1.0f}));
    EXPECT_TRUE(approx(mesh->get_value(Mesh::world_bounds_max_property), glm::vec3{6.0f,  1.0f,  1.0f}));
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 6.0f); // pushed by the node move

    mesh->clear_primitives();
    EXPECT_TRUE(approx(mesh->get_value(Mesh::world_bounds_max_property), glm::vec3{0.0f}));
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 0.0f);
}
