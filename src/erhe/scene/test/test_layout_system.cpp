// The Layout node system (doc/erhe/scene.md "Node systems"): a Scene owns one
// Layout_system, which keeps one record per layout node of the scene and is
// driven by the key property Layout.type and by the node entering or leaving
// the scene.

#include "erhe_scene/layout.hpp"
#include "erhe_scene/layout_system.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

namespace {

using erhe::scene::Layout;
using erhe::scene::Layout_data;
using erhe::scene::Layout_type;
using erhe::scene::Xform;

class Test_scene_host : public erhe::scene::Scene_host
{
public:
    Test_scene_host() : scene{"test scene", this} {}

    auto get_host_name   () const -> const char*         override { return "Test_scene_host"; }
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

    void on_mesh_primitives_changed    (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_material_changed      (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_flags_changed         (const std::shared_ptr<erhe::scene::Mesh>&, uint64_t, uint64_t) override {}
    void on_mesh_transform_changed     (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_primitive_data_changed(const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_mesh_display_color_changed (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    erhe::scene::Scene scene;
};

[[nodiscard]] auto make_hosted_node(Test_scene_host& host, const char* name) -> std::shared_ptr<Xform>
{
    std::shared_ptr<Xform> node = std::make_shared<Xform>(name);
    node->set_parent(host.scene.get_root_node());
    return node;
}

} // anonymous namespace

TEST(Layout_system, the_key_property_creates_and_destroys_the_record)
{
    Test_scene_host host;
    const erhe::scene::Layout_system& system = host.scene.get_layout_system();

    const std::shared_ptr<Xform> node = make_hosted_node(host, "node");
    EXPECT_EQ(system.get_records().size(), std::size_t{0});

    node->set_value(Layout::type_property, Layout_type::stack);
    ASSERT_NE(system.find(*node), nullptr);
    EXPECT_EQ(system.find(*node)->type, Layout_type::stack);

    // A non-key value of the group refreshes the record.
    node->set_value(Layout::gap_property, glm::vec3{0.25f});
    ASSERT_NE(system.find(*node), nullptr);
    EXPECT_EQ(system.find(*node)->gap, glm::vec3{0.25f});

    node->clear_value(Layout::type_property);
    EXPECT_EQ(system.find(*node), nullptr);
    EXPECT_EQ(system.get_records().size(), std::size_t{0});
}

TEST(Layout_system, a_node_that_already_carries_a_layout_gets_its_record_on_entry)
{
    Test_scene_host host;
    const erhe::scene::Layout_system& system = host.scene.get_layout_system();

    std::shared_ptr<Xform> node = std::make_shared<Xform>("node");
    node->set_value(Layout::type_property, Layout_type::grid);
    EXPECT_EQ(system.get_records().size(), std::size_t{0});

    node->set_parent(host.scene.get_root_node());
    ASSERT_NE(system.find(*node), nullptr);
    EXPECT_EQ(system.find(*node)->type, Layout_type::grid);

    node->set_parent(std::shared_ptr<erhe::Hierarchy>{});
    EXPECT_EQ(system.find(*node), nullptr);
    EXPECT_EQ(system.get_records().size(), std::size_t{0});
}

TEST(Layout_system, update_arranges_the_children_of_a_stack_layout_node)
{
    Test_scene_host host;

    const std::shared_ptr<Xform> layout_node = make_hosted_node(host, "layout");
    layout_node->set_value(Layout::type_property, Layout_type::stack);
    layout_node->set_value(Layout::volume_min_property, glm::vec3{0.0f, 0.0f, 0.0f});
    layout_node->set_value(Layout::volume_max_property, glm::vec3{10.0f, 1.0f, 1.0f});
    layout_node->set_value(Layout::gap_property, glm::vec3{1.0f, 0.0f, 0.0f});

    // Content-less children have a zero-extent box, so each one lands on the
    // gap boundary ahead of the previous.
    std::shared_ptr<Xform> a = std::make_shared<Xform>("a");
    std::shared_ptr<Xform> b = std::make_shared<Xform>("b");
    a->set_parent(layout_node);
    b->set_parent(layout_node);

    host.scene.update_layouts();
    EXPECT_EQ(a->parent_from_node_transform().get_translation().x, 0.0f);
    EXPECT_EQ(b->parent_from_node_transform().get_translation().x, 1.0f);

    // A node that is no longer a layout node arranges nothing.
    layout_node->clear_value(Layout::type_property);
    b->set_parent_from_node(erhe::scene::Trs_transform{glm::vec3{5.0f, 0.0f, 0.0f}});
    host.scene.update_layouts();
    EXPECT_EQ(b->parent_from_node_transform().get_translation().x, 5.0f);
}
