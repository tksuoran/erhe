// Expressions on scene items (doc/property-system.md D22): reference
// paths resolve through Item_base (".." = parent, a name = an item of the
// hosting scene), and every transform writer of a node reaches its
// dependents through Node::handle_transform_update.

#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

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
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override { ++light_changed_count; }

    erhe::scene::Scene scene;
    int                light_changed_count{0};
};

auto approx(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f) -> bool
{
    return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3{eps}));
}

} // anonymous namespace

TEST(Node_expressions, node_follows_another_node_by_name_through_every_transform_writer)
{
    Test_scene_host host;
    auto driver   = std::make_shared<erhe::scene::Xform>("Driver");
    auto follower = std::make_shared<erhe::scene::Xform>("Follower");
    driver->set_parent(host.scene.get_root_node());
    follower->set_parent(host.scene.get_root_node());

    ASSERT_TRUE(follower->set_expression(erhe::scene::Node::translation_property, "{Driver/translation} + 1"));
    EXPECT_TRUE(follower->get_expression_error(erhe::scene::Node::translation_property).empty());
    EXPECT_EQ(follower->get_value_source(erhe::scene::Node::translation_property), Value_source::expression);
    EXPECT_TRUE(approx(follower->get_value(erhe::scene::Node::translation_property), glm::vec3{1.0f}));
    EXPECT_TRUE(approx(glm::vec3{follower->position_in_world()}, glm::vec3{1.0f}));

    // Property write on the driver
    driver->set_value(erhe::scene::Node::translation_property, glm::vec3{1.0f, 2.0f, 3.0f});
    EXPECT_TRUE(approx(glm::vec3{follower->position_in_world()}, glm::vec3{2.0f, 3.0f, 4.0f}));

    // Direct Trs_transform write on the driver (transform tool, physics,
    // animation): funnels through handle_transform_update.
    erhe::scene::Trs_transform t;
    t.set_trs(glm::vec3{10.0f, 0.0f, 0.0f}, glm::quat{1.0f, 0.0f, 0.0f, 0.0f}, glm::vec3{1.0f});
    driver->set_parent_from_node(t);
    EXPECT_TRUE(approx(glm::vec3{follower->position_in_world()}, glm::vec3{11.0f, 1.0f, 1.0f}));

    // Bridged local state reads the bridge; a value write replaces the
    // expression and the driver no longer reaches the follower.
    EXPECT_TRUE(approx(follower->read_local_value(erhe::scene::Node::translation_property).value(), glm::vec3{11.0f, 1.0f, 1.0f}));
    follower->set_value(erhe::scene::Node::translation_property, glm::vec3{0.0f});
    EXPECT_FALSE(follower->get_expression(erhe::scene::Node::translation_property).has_value());
    driver->set_value(erhe::scene::Node::translation_property, glm::vec3{5.0f});
    EXPECT_TRUE(approx(glm::vec3{follower->position_in_world()}, glm::vec3{0.0f}));
}

TEST(Node_expressions, parent_path_and_attachment_targets)
{
    Test_scene_host host;
    auto parent = std::make_shared<erhe::scene::Xform>("Parent");
    auto child  = std::make_shared<erhe::scene::Xform>("Child");
    auto light  = std::make_shared<erhe::scene::Light>("Lamp");
    parent->set_parent(host.scene.get_root_node());
    child->set_parent(parent);
    child->attach(light);
    parent->set_value(erhe::scene::Node::scale_property, glm::vec3{2.0f, 3.0f, 4.0f});

    // ".." is the inheritance parent (the parent node); an attachment
    // resolves its node's host like the node does.
    ASSERT_TRUE(child->set_expression(erhe::scene::Node::scale_property, "{../scale}"));
    EXPECT_TRUE(approx(child->get_value(erhe::scene::Node::scale_property), glm::vec3{2.0f, 3.0f, 4.0f}));

    ASSERT_TRUE(light->set_expression(erhe::scene::Light::intensity_property, "{Parent/scale.y} * 10"));
    EXPECT_TRUE(light->get_expression_error(erhe::scene::Light::intensity_property).empty());
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 30.0f);
    const int changed_before = host.light_changed_count;
    parent->set_value(erhe::scene::Node::scale_property, glm::vec3{2.0f, 5.0f, 4.0f});
    EXPECT_EQ(light->get_value(erhe::scene::Light::intensity_property), 50.0f);
    EXPECT_EQ(host.light_changed_count, changed_before + 1); // the D19 callback sees an expression result as a write
    EXPECT_TRUE(approx(child->get_value(erhe::scene::Node::scale_property), glm::vec3{2.0f, 5.0f, 4.0f}));

    // An unknown name stays unresolved with an error, and resolves once the
    // item exists.
    ASSERT_TRUE(light->set_expression(erhe::scene::Light::range_property, "{Later/translation.x}"));
    EXPECT_FALSE(light->get_expression_error(erhe::scene::Light::range_property).empty());
    auto later = std::make_shared<erhe::scene::Xform>("Later");
    later->set_value(erhe::scene::Node::translation_property, glm::vec3{7.0f, 0.0f, 0.0f});
    later->set_parent(host.scene.get_root_node());
    EXPECT_EQ(light->get_value(erhe::scene::Light::range_property), 7.0f);
    EXPECT_TRUE(light->get_expression_error(erhe::scene::Light::range_property).empty());
}
