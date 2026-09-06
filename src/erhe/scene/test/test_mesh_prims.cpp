// A Mesh is a geometric prim (doc/usd-compatibility-plan.md C5): a child prim
// of its parent with its own transform, and a parent holds any number of them.

#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/xform.hpp"

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

class Test_scene_host : public erhe::scene::Scene_host
{
public:
    Test_scene_host()
        : scene{"test scene", this}
    {
        scene.add_mesh_layer(std::make_shared<erhe::scene::Mesh_layer>("content", 0, content_layer_id));
    }

    static constexpr erhe::scene::Layer_id content_layer_id{0};

    auto get_host_name   () const -> const char*         override { return "Test_scene_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node) override { scene.register_node  (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node) override { scene.unregister_node(node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>&)      override {}
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>&)      override {}
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&   mesh) override { scene.register_mesh  (mesh); }
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&   mesh) override { scene.unregister_mesh(mesh); }
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

[[nodiscard]] auto layer_contains(const erhe::scene::Scene& scene, const std::shared_ptr<erhe::scene::Mesh>& mesh) -> bool
{
    for (const std::shared_ptr<erhe::scene::Mesh_layer>& mesh_layer : scene.get_mesh_layers()) {
        const std::vector<std::shared_ptr<erhe::scene::Mesh>>& meshes = mesh_layer->meshes;
        if (std::find(meshes.begin(), meshes.end(), mesh) != meshes.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(Mesh_prims, mesh_is_an_xformable_prim)
{
    auto mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    EXPECT_TRUE(erhe::is<erhe::scene::Node>     (mesh.get()));
    EXPECT_TRUE(erhe::is<erhe::scene::Gprim>    (mesh.get()));
    EXPECT_TRUE(erhe::is<erhe::scene::Imageable>(mesh.get()));
    EXPECT_EQ  (mesh->get_class_type_name(), "Mesh");
    EXPECT_EQ  (mesh->get_node(), mesh.get());
}

TEST(Mesh_prims, mesh_under_an_xform_composes_its_own_transform)
{
    Test_scene_host host;
    auto parent = std::make_shared<erhe::scene::Xform>("parent");
    auto mesh   = std::make_shared<erhe::scene::Mesh>("mesh");
    parent->set_parent(host.scene.get_root_node());
    parent->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{5.0f, 0.0f, 0.0f}});
    erhe::scene::set_mesh_parent(mesh, parent);

    // A mesh that carries no transform of its own sits at its parent's place.
    EXPECT_TRUE(approx(mesh->parent_from_node_transform().get_translation(), glm::vec3{0.0f}));
    EXPECT_TRUE(approx(mesh->world_from_node_transform().get_translation(), glm::vec3{5.0f, 0.0f, 0.0f}));

    mesh->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{0.0f, 2.0f, 0.0f}});
    host.scene.update_node_transforms();
    EXPECT_TRUE(approx(mesh->world_from_node_transform().get_translation(), glm::vec3{5.0f, 2.0f, 0.0f}));

    parent->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{5.0f, 0.0f, 3.0f}});
    host.scene.update_node_transforms();
    EXPECT_TRUE(approx(mesh->world_from_node_transform().get_translation(), glm::vec3{5.0f, 2.0f, 3.0f}));
}

TEST(Mesh_prims, several_meshes_under_one_xform_all_register)
{
    Test_scene_host host;
    auto parent = std::make_shared<erhe::scene::Xform>("parent");
    auto mesh_a = std::make_shared<erhe::scene::Mesh>("mesh a");
    auto mesh_b = std::make_shared<erhe::scene::Mesh>("mesh b");
    mesh_a->layer_id = Test_scene_host::content_layer_id;
    mesh_b->layer_id = Test_scene_host::content_layer_id;
    parent->set_parent(host.scene.get_root_node());
    erhe::scene::set_mesh_parent(mesh_a, parent);
    erhe::scene::set_mesh_parent(mesh_b, parent);

    EXPECT_TRUE(layer_contains(host.scene, mesh_a));
    EXPECT_TRUE(layer_contains(host.scene, mesh_b));

    // get_mesh answers the one-mesh case; for_each_mesh_child answers all.
    EXPECT_EQ(erhe::scene::get_mesh(parent.get()), mesh_a);
    std::vector<std::shared_ptr<erhe::scene::Mesh>> visited;
    erhe::scene::for_each_mesh_child(*parent.get(), [&visited](const std::shared_ptr<erhe::scene::Mesh>& mesh) { visited.push_back(mesh); });
    ASSERT_EQ(visited.size(), std::size_t{2});
    EXPECT_EQ(visited[0], mesh_a);
    EXPECT_EQ(visited[1], mesh_b);

    // Detaching the parent takes both meshes out of the scene.
    parent->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    EXPECT_FALSE(layer_contains(host.scene, mesh_a));
    EXPECT_FALSE(layer_contains(host.scene, mesh_b));
}

TEST(Mesh_prims, get_mesh_answers_the_mesh_itself)
{
    auto mesh = std::make_shared<erhe::scene::Mesh>("mesh");
    EXPECT_EQ(erhe::scene::get_mesh(mesh.get()), mesh);
    EXPECT_EQ(erhe::scene::get_mesh(std::static_pointer_cast<erhe::Item_base>(mesh)), mesh);
}
