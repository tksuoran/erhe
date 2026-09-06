// Mesh_primitive as a property sub-object of its mesh (doc/property-system.md
// D29): the material is a member-backed object property, a write reaches the
// scene host exactly as set_primitive_material did, and the owner link
// survives every way a primitive list is copied.

#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace erhe::property;
using erhe::scene::Mesh;
using erhe::scene::Mesh_primitive;
using erhe::scene::Node;
using erhe::scene::Xform;
using erhe::primitive::Material;

namespace {

class Counting_scene_host : public erhe::scene::Scene_host
{
public:
    Counting_scene_host()
        : scene{"test scene", this}
    {
        scene.add_mesh_layer(std::make_shared<erhe::scene::Mesh_layer>("content", 0u, erhe::scene::Layer_id{0}));
    }

    auto get_host_name   () const -> const char*        override { return "Counting_scene_host"; }
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
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>& mesh) override { material_changed.push_back(mesh.get()); }
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>&, uint64_t, uint64_t) override {}
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    erhe::scene::Scene              scene;
    std::vector<erhe::scene::Mesh*> material_changed;
};

[[nodiscard]] auto make_primitive() -> std::shared_ptr<erhe::primitive::Primitive>
{
    return std::make_shared<erhe::primitive::Primitive>(erhe::primitive::Buffer_mesh{});
}

[[nodiscard]] auto make_hosted_mesh(Counting_scene_host& host, const std::shared_ptr<Material>& material) -> std::shared_ptr<Mesh>
{
    std::shared_ptr<Node> node = std::make_shared<Xform>("node");
    std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>("mesh");
    mesh->add_primitive(make_primitive(), material);
    mesh->add_primitive(make_primitive(), material);
    erhe::scene::set_mesh_parent(mesh, node);
    node->set_parent(host.scene.get_root_node());
    host.scene.get_mesh_layers().front()->add(mesh);
    return mesh;
}

} // anonymous namespace

TEST(Mesh_primitive_material, set_primitive_material_writes_the_property_and_notifies_once)
{
    Counting_scene_host host;
    std::shared_ptr<Material> a    = std::make_shared<Material>("a");
    std::shared_ptr<Material> b    = std::make_shared<Material>("b");
    std::shared_ptr<Mesh>     mesh = make_hosted_mesh(host, a);

    const Mesh_primitive& primitive = mesh->get_primitives()[1];
    EXPECT_EQ(primitive.get_value(Mesh_primitive::material_property).object.get(), static_cast<erhe::Item_base*>(a.get()));
    EXPECT_EQ(primitive.get_value_source(Mesh_primitive::material_property.get()), Value_source::local);
    EXPECT_EQ(primitive.get_owner(), mesh.get());
    EXPECT_EQ(primitive.get_index(), std::size_t{1});

    host.material_changed.clear();
    mesh->set_primitive_material(1, b);
    EXPECT_EQ(mesh->get_primitives()[1].material.get(), b.get());
    EXPECT_EQ(mesh->get_primitives()[0].material.get(), a.get());
    ASSERT_EQ(host.material_changed.size(), std::size_t{1});
    EXPECT_EQ(host.material_changed.front(), mesh.get());

    mesh->set_primitive_material(1, b); // unchanged: no notification
    EXPECT_EQ(host.material_changed.size(), std::size_t{1});

    mesh->set_primitive_material(7, a); // out of range: nothing
    EXPECT_EQ(host.material_changed.size(), std::size_t{1});
}

TEST(Mesh_primitive_material, sub_object_addressing_agrees_with_the_primitive_list)
{
    Counting_scene_host host;
    std::shared_ptr<Material> a    = std::make_shared<Material>("a");
    std::shared_ptr<Material> b    = std::make_shared<Material>("b");
    std::shared_ptr<Mesh>     mesh = make_hosted_mesh(host, a);

    ASSERT_EQ(mesh->get_property_sub_object_count(), std::size_t{2});
    EXPECT_EQ(mesh->get_property_sub_object(0), &mesh->get_primitives()[0]);
    EXPECT_EQ(mesh->get_property_sub_object(1), &mesh->get_primitives()[1]);
    EXPECT_EQ(mesh->get_property_sub_object(2), nullptr);
    EXPECT_EQ(mesh->get_property_sub_object_label(1), "Primitive 1");
    EXPECT_EQ(mesh->get_property_sub_object_label(2), "");

    // A write through the sub-object is the same funnel.
    host.material_changed.clear();
    Dependency_object* sub_object = mesh->get_property_sub_object(0);
    ASSERT_NE(sub_object, nullptr);
    sub_object->set_value(Mesh_primitive::material_property, Object_reference{b});
    EXPECT_EQ(mesh->get_primitives()[0].material.get(), b.get());
    EXPECT_EQ(host.material_changed.size(), std::size_t{1});

    // The traits reject a pointee that is not a Material.
    std::shared_ptr<Node> node = std::make_shared<Xform>("not a material");
    sub_object->set_value(Mesh_primitive::material_property, Object_reference{node});
    EXPECT_EQ(mesh->get_primitives()[0].material.get(), b.get());
    EXPECT_EQ(host.material_changed.size(), std::size_t{1});
}

TEST(Mesh_primitive_material, copies_re_stamp_the_owner_link)
{
    Counting_scene_host host;
    std::shared_ptr<Material> a      = std::make_shared<Material>("a");
    std::shared_ptr<Material> b      = std::make_shared<Material>("b");
    std::shared_ptr<Mesh>     source = make_hosted_mesh(host, a);

    // A primitive value outside a mesh has no owner.
    std::vector<Mesh_primitive> copied = source->get_primitives();
    EXPECT_EQ(copied[0].get_owner(), nullptr);

    // set_primitives: the copies belong to the receiving mesh.
    std::shared_ptr<Node> node  = std::make_shared<Xform>("other node");
    std::shared_ptr<Mesh> other = std::make_shared<Mesh>("other");
    other->set_primitives(copied);
    erhe::scene::set_mesh_parent(other, node);
    node->set_parent(host.scene.get_root_node());
    host.scene.get_mesh_layers().front()->add(other);
    EXPECT_EQ(other->get_primitives()[1].get_owner(), other.get());
    EXPECT_EQ(other->get_primitives()[1].get_index(), std::size_t{1});

    host.material_changed.clear();
    other->set_primitive_material(1, b);
    ASSERT_EQ(host.material_changed.size(), std::size_t{1});
    EXPECT_EQ(host.material_changed.front(), other.get()); // the copy's mesh, not the source's
    EXPECT_EQ(source->get_primitives()[1].material.get(), a.get());

    // The clone constructor stamps too.
    std::shared_ptr<Mesh> clone = std::make_shared<Mesh>(*source, erhe::for_clone{});
    EXPECT_EQ(clone->get_primitives()[0].get_owner(), clone.get());

    // Growth past capacity reallocates: the owner link follows.
    std::shared_ptr<Mesh> growing = std::make_shared<Mesh>("growing");
    for (int i = 0; i < 40; ++i) {
        growing->add_primitive(make_primitive(), a);
    }
    for (std::size_t i = 0; i < 40; ++i) {
        EXPECT_EQ(growing->get_primitives()[i].get_owner(), growing.get());
        EXPECT_EQ(growing->get_primitives()[i].get_index(), i);
    }
}
