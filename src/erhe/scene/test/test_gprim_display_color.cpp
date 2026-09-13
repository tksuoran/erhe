// `Gprim.display_color` (USD `primvars:displayColor` at constant
// interpolation): the one color of a whole surface, as an entry-store property
// of the geometry level. The value has to reach the mesh's vertex data - that
// is where every renderer reads a mesh's own color from - and the mesh cannot
// build that itself, so a write states the change to the scene host, which
// rebuilds the primitives with it.

#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/gprim.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using erhe::property::Value_source;
using erhe::scene::Gprim;
using erhe::scene::Mesh;
using erhe::scene::Xform;

namespace {

class Recording_scene_host : public erhe::scene::Scene_host
{
public:
    Recording_scene_host()
        : scene{"test scene", this}
    {
        scene.add_mesh_layer(std::make_shared<erhe::scene::Mesh_layer>("content", 0u, erhe::scene::Layer_id{0}));
    }

    auto get_host_name   () const -> const char*         override { return "Recording_scene_host"; }
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
    void on_mesh_display_color_changed (const std::shared_ptr<erhe::scene::Mesh>& mesh) override
    {
        display_color_changed.push_back(mesh.get());
    }
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    erhe::scene::Scene              scene;
    std::vector<erhe::scene::Mesh*> display_color_changed;
};

[[nodiscard]] auto make_hosted_mesh(Recording_scene_host& host, const char* name) -> std::shared_ptr<Mesh>
{
    std::shared_ptr<erhe::scene::Node> node = std::make_shared<Xform>("node");
    std::shared_ptr<Mesh>              mesh = std::make_shared<Mesh>(name);
    mesh->add_primitive(std::make_shared<erhe::primitive::Primitive>(erhe::primitive::Buffer_mesh{}), {});
    erhe::scene::set_mesh_parent(mesh, node);
    node->set_parent(host.scene.get_root_node());
    host.scene.get_mesh_layers().front()->add(mesh);
    return mesh;
}

} // anonymous namespace

TEST(Gprim_display_color, defaults_to_the_usd_fallback_and_becomes_local_when_written)
{
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>("mesh");
    EXPECT_EQ(mesh->get_display_color(), Gprim::default_display_color);
    EXPECT_EQ(mesh->get_value_source(Gprim::display_color_property.get()), Value_source::default_value);

    mesh->set_display_color(glm::vec3{0.936f, 0.0f, 0.0f});
    EXPECT_EQ(mesh->get_display_color(), (glm::vec3{0.936f, 0.0f, 0.0f}));
    EXPECT_EQ(mesh->get_value_source(Gprim::display_color_property.get()), Value_source::local);

    mesh->clear_value(Gprim::display_color_property);
    EXPECT_EQ(mesh->get_display_color(), Gprim::default_display_color);
    EXPECT_EQ(mesh->get_value_source(Gprim::display_color_property.get()), Value_source::default_value);
}

TEST(Gprim_display_color, inherits_from_the_holding_prim)
{
    std::shared_ptr<Xform> parent = std::make_shared<Xform>("parent");
    std::shared_ptr<Mesh>  mesh   = std::make_shared<Mesh>("mesh");
    erhe::scene::set_mesh_parent(mesh, parent);

    parent->set_value(Gprim::display_color_property, glm::vec3{0.0f, 1.0f, 0.0f});
    EXPECT_EQ(mesh->get_display_color(), (glm::vec3{0.0f, 1.0f, 0.0f}));
    EXPECT_EQ(mesh->get_value_source(Gprim::display_color_property.get()), Value_source::inherited);

    mesh->set_display_color(glm::vec3{0.0f, 0.0f, 1.0f}); // a local value wins over the holder's
    EXPECT_EQ(mesh->get_display_color(), (glm::vec3{0.0f, 0.0f, 1.0f}));
}

TEST(Gprim_display_color, a_write_asks_the_host_for_a_rebuild)
{
    Recording_scene_host  host;
    std::shared_ptr<Mesh> mesh = make_hosted_mesh(host, "mesh");

    host.display_color_changed.clear();
    mesh->set_display_color(glm::vec3{0.325f, 0.825f, 0.0f});
    ASSERT_EQ(host.display_color_changed.size(), 1u);
    EXPECT_EQ(host.display_color_changed.front(), mesh.get());

    host.display_color_changed.clear();
    mesh->clear_value(Gprim::display_color_property);
    ASSERT_EQ(host.display_color_changed.size(), 1u);
    EXPECT_EQ(host.display_color_changed.front(), mesh.get());
}

TEST(Gprim_display_color, an_inherited_write_asks_the_host_for_a_rebuild_of_the_mesh_below)
{
    Recording_scene_host  host;
    std::shared_ptr<Mesh> mesh = make_hosted_mesh(host, "mesh");

    host.display_color_changed.clear();
    mesh->get_parent_node()->set_value(Gprim::display_color_property, glm::vec3{1.0f, 0.5f, 0.0f});
    ASSERT_EQ(host.display_color_changed.size(), 1u);
    EXPECT_EQ(host.display_color_changed.front(), mesh.get());
    EXPECT_EQ(mesh->get_display_color(), (glm::vec3{1.0f, 0.5f, 0.0f}));
}
