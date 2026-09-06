// A prim outside Xformable - a Scope - in a scene tree
// (doc/usd-compatibility-plan.md C5): it has no transform, so a transform
// composes with the nearest Xformable ancestor and passes through it; it
// carries the scene host to the prims below it; and it clones with its
// subtree.

#include "erhe_item/scope.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <memory>

namespace {

class Test_scene_host : public erhe::scene::Scene_host
{
public:
    Test_scene_host() : scene{"test scene", this} {}

    auto get_host_name   () const -> const char*         override { return "Test_scene_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node) override { scene.register_node  (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node) override { scene.unregister_node(node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>&)      override {}
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>&)      override {}
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&)        override {}
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&)        override {}
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&)        override {}
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&)        override {}
    void register_light   (const std::shared_ptr<erhe::scene::Light>&)       override {}
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&)       override {}
    void register_layout  (const std::shared_ptr<erhe::scene::Layout>&)      override {}
    void unregister_layout(const std::shared_ptr<erhe::scene::Layout>&)      override {}

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

auto is_registered(erhe::scene::Scene& scene, const erhe::scene::Node* wanted) -> bool
{
    bool found = false;
    scene.for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        if (node.get() == wanted) {
            found = true;
            return false;
        }
        return true;
    });
    return found;
}

} // anonymous namespace

TEST(Scope_prims, transform_composes_through_a_scope)
{
    Test_scene_host host;
    const std::shared_ptr<erhe::scene::Node>  root  = host.scene.get_root_node();
    const std::shared_ptr<erhe::scene::Xform> a     = std::make_shared<erhe::scene::Xform>("A");
    const std::shared_ptr<erhe::Scope>        s     = std::make_shared<erhe::Scope>("S");
    const std::shared_ptr<erhe::scene::Xform> b     = std::make_shared<erhe::scene::Xform>("B");

    a->set_parent(root);
    s->set_parent(a);
    b->set_parent(s);

    a->set_parent_from_node(translated(glm::vec3{1.0f, 2.0f, 3.0f}));
    b->set_parent_from_node(translated(glm::vec3{0.0f, 0.0f, 5.0f}));

    host.scene.update_node_transforms();
    EXPECT_TRUE(approx(b->world_from_node_transform().get_translation(), glm::vec3{1.0f, 2.0f, 8.0f}));

    // Moving A afterwards moves B, so the propagation pass recurses through
    // the scope that has no transform of its own.
    a->set_parent_from_node(translated(glm::vec3{10.0f, 20.0f, 30.0f}));
    host.scene.update_node_transforms();
    EXPECT_TRUE(approx(b->world_from_node_transform().get_translation(), glm::vec3{10.0f, 20.0f, 35.0f}));
}

TEST(Scope_prims, parent_node_is_the_nearest_xformable_ancestor)
{
    const std::shared_ptr<erhe::scene::Xform> a = std::make_shared<erhe::scene::Xform>("A");
    const std::shared_ptr<erhe::Scope>        s = std::make_shared<erhe::Scope>("S");
    const std::shared_ptr<erhe::scene::Xform> b = std::make_shared<erhe::scene::Xform>("B");

    s->set_parent(a);
    b->set_parent(s);

    EXPECT_EQ(b->get_parent().lock().get(), s.get());
    EXPECT_EQ(b->get_parent_node().get(),   a.get());
    EXPECT_EQ(a->get_parent_node().get(),   nullptr);
}

TEST(Scope_prims, a_scope_carries_the_scene_host_to_its_subtree)
{
    Test_scene_host host;
    const std::shared_ptr<erhe::scene::Node>  root = host.scene.get_root_node();
    const std::shared_ptr<erhe::Scope>        s    = std::make_shared<erhe::Scope>("S");
    const std::shared_ptr<erhe::scene::Xform> b    = std::make_shared<erhe::scene::Xform>("B");

    b->set_parent(s);
    EXPECT_EQ(s->get_item_host(), nullptr);
    EXPECT_EQ(b->get_item_host(), nullptr);
    EXPECT_FALSE(is_registered(host.scene, b.get()));

    s->set_parent(root);
    EXPECT_EQ(s->get_item_host(), &host);
    EXPECT_EQ(b->get_item_host(), &host);
    EXPECT_TRUE(is_registered(host.scene, b.get()));

    s->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    EXPECT_EQ(s->get_item_host(), nullptr);
    EXPECT_EQ(b->get_item_host(), nullptr);
    EXPECT_FALSE(is_registered(host.scene, b.get()));
}

TEST(Scope_prims, cloning_a_scope_clones_its_subtree)
{
    const std::shared_ptr<erhe::Scope>        s = std::make_shared<erhe::Scope>("S");
    const std::shared_ptr<erhe::scene::Xform> b = std::make_shared<erhe::scene::Xform>("B");
    b->set_parent(s);

    const std::shared_ptr<erhe::Item_base> clone_base = s->clone();
    const std::shared_ptr<erhe::Scope>     clone      = std::dynamic_pointer_cast<erhe::Scope>(clone_base);
    ASSERT_TRUE(clone);
    clone->adopt_orphan_children();

    ASSERT_EQ(clone->get_children().size(), std::size_t{1});
    const std::shared_ptr<erhe::Hierarchy>& clone_child = clone->get_children().front();
    EXPECT_NE(clone_child.get(), b.get());
    EXPECT_TRUE(erhe::is<erhe::scene::Xform>(clone_child.get()));
    EXPECT_EQ(clone_child->get_name(), "B");
    EXPECT_EQ(clone_child->get_parent().lock().get(), clone.get());
}
