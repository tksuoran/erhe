// A Camera and a Light are transformable prims (doc/usd-compatibility-plan.md
// C5): each is a child prim of its parent with its own transform.

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
        scene.add_light_layer(std::make_shared<erhe::scene::Light_layer>("lights", light_layer_id));
    }

    static constexpr erhe::scene::Layer_id light_layer_id{0};

    auto get_host_name   () const -> const char*         override { return "Test_scene_host"; }
    auto get_hosted_scene()       -> erhe::scene::Scene* override { return &scene; }

    void register_node    (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.register_node    (node); }
    void unregister_node  (const std::shared_ptr<erhe::scene::Node>&   node)   override { scene.unregister_node  (node); }
    void register_camera  (const std::shared_ptr<erhe::scene::Camera>& camera) override { scene.register_camera  (camera); }
    void unregister_camera(const std::shared_ptr<erhe::scene::Camera>& camera) override { scene.unregister_camera(camera); }
    void register_mesh    (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void unregister_mesh  (const std::shared_ptr<erhe::scene::Mesh>&)          override {}
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>&)          override {}
    void register_light   (const std::shared_ptr<erhe::scene::Light>&  light)  override { scene.register_light   (light); }
    void unregister_light (const std::shared_ptr<erhe::scene::Light>&  light)  override { scene.unregister_light (light); }
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

[[nodiscard]] auto layer_contains(const erhe::scene::Scene& scene, const std::shared_ptr<erhe::scene::Light>& light) -> bool
{
    for (const std::shared_ptr<erhe::scene::Light_layer>& light_layer : scene.get_light_layers()) {
        const std::vector<std::shared_ptr<erhe::scene::Light>>& lights = light_layer->lights;
        if (std::find(lights.begin(), lights.end(), light) != lights.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(Camera_light_prims, camera_and_light_are_xformable_prims)
{
    auto camera = std::make_shared<erhe::scene::Camera>("camera");
    auto light  = std::make_shared<erhe::scene::Light>("light");
    EXPECT_TRUE(erhe::is<erhe::scene::Node>     (camera.get()));
    EXPECT_TRUE(erhe::is<erhe::scene::Imageable>(camera.get()));
    EXPECT_TRUE(erhe::is<erhe::scene::Node>     (light.get()));
    EXPECT_TRUE(erhe::is<erhe::scene::Imageable>(light.get()));
    EXPECT_EQ  (camera->get_class_type_name(), "Camera");
    EXPECT_EQ  (light ->get_class_type_name(), "Light");
    EXPECT_EQ  (camera->get_node(), camera.get());
    EXPECT_EQ  (light ->get_node(), light.get());
}

TEST(Camera_light_prims, camera_under_an_xform_composes_its_own_transform)
{
    Test_scene_host host;
    auto parent = std::make_shared<erhe::scene::Xform>("parent");
    auto camera = std::make_shared<erhe::scene::Camera>("camera");
    parent->set_parent(host.scene.get_root_node());
    parent->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{5.0f, 0.0f, 0.0f}});
    erhe::scene::set_prim_parent(camera, parent);

    // A camera that carries no transform of its own sits at its parent's place.
    EXPECT_TRUE(approx(camera->parent_from_node_transform().get_translation(), glm::vec3{0.0f}));
    EXPECT_TRUE(approx(camera->world_from_node_transform().get_translation(), glm::vec3{5.0f, 0.0f, 0.0f}));

    camera->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{0.0f, 2.0f, 0.0f}});
    EXPECT_TRUE(approx(camera->world_from_node_transform().get_translation(), glm::vec3{5.0f, 2.0f, 0.0f}));

    // The camera is registered with the scene as the prim it is.
    const std::vector<std::shared_ptr<erhe::scene::Camera>>& cameras = host.scene.get_cameras();
    EXPECT_NE(std::find(cameras.begin(), cameras.end(), camera), cameras.end());
    EXPECT_EQ(erhe::scene::get_camera(parent.get()), camera);
}

TEST(Camera_light_prims, light_under_a_scope_registers_and_follows_the_xform)
{
    Test_scene_host host;
    auto parent = std::make_shared<erhe::scene::Xform>("parent");
    auto scope  = std::make_shared<erhe::Scope>("lights");
    auto light  = std::make_shared<erhe::scene::Light>("light");
    light->layer_id = Test_scene_host::light_layer_id;
    light->set_light_type(erhe::scene::Light_type::point);
    parent->set_parent(host.scene.get_root_node());
    scope->set_parent(parent);
    erhe::scene::set_prim_parent(light, scope);

    // A Scope carries no transform, so the light composes with the nearest
    // Xformable ancestor, and the scene host reaches it through the Scope.
    EXPECT_TRUE(layer_contains(host.scene, light));

    parent->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{0.0f, 3.0f, 0.0f}});
    host.scene.update_node_transforms();
    EXPECT_TRUE(approx(light->world_from_node_transform().get_translation(), glm::vec3{0.0f, 3.0f, 0.0f}));
    EXPECT_TRUE(approx(light->get_light_frame().position, glm::vec3{0.0f, 3.0f, 0.0f}));

    // Detaching the light takes it out of the layer again.
    erhe::scene::set_prim_parent(light, {});
    EXPECT_FALSE(layer_contains(host.scene, light));
}
