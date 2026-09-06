// A prim inherits visible / shadow_cast / lightmapped from its parent prim
// (D23 in doc/property-system.md), and a node's attachments inherit from
// their node. A Mesh is a child prim (doc/usd-compatibility-plan.md C5), so
// its inheritance parent is the prim it is parented to.

#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using erhe::scene::Mesh;
using erhe::scene::Node;
using erhe::scene::Xform;

namespace {

class Counting_mesh : public Mesh
{
public:
    explicit Counting_mesh(const std::string_view name) : Mesh{name} {}

    int visible_updates{0};

    void handle_flag_bits_update(const uint64_t old_flag_bits, const uint64_t new_flag_bits) override
    {
        Mesh::handle_flag_bits_update(old_flag_bits, new_flag_bits);
        if (((old_flag_bits ^ new_flag_bits) & erhe::Item_flags::visible) != 0u) {
            ++visible_updates;
        }
    }
};

} // namespace

TEST(Attachment_inheritance, mesh_inherits_from_its_node)
{
    auto node = std::make_shared<Xform>("node");
    auto mesh = std::make_shared<Counting_mesh>("mesh");
    erhe::scene::set_mesh_parent(mesh, node);
    EXPECT_EQ(mesh->get_inheritance_parent(), node.get());
    EXPECT_TRUE(mesh->is_visible());

    node->hide();
    EXPECT_FALSE(mesh->is_visible());
    EXPECT_EQ(mesh->get_value_source(erhe::Item_base::visible_property.get()), Value_source::inherited);
    EXPECT_EQ(mesh->visible_updates, 1);

    node->show();
    EXPECT_TRUE(mesh->is_visible());
    EXPECT_EQ(mesh->visible_updates, 2);
}

TEST(Attachment_inheritance, mesh_follows_an_ancestor_hide)
{
    auto root  = std::make_shared<Xform>("root");
    auto child = std::make_shared<Xform>("child");
    auto mesh  = std::make_shared<Counting_mesh>("mesh");
    child->set_parent(root);
    erhe::scene::set_mesh_parent(mesh, child);

    root->hide();
    EXPECT_FALSE(child->is_visible());
    EXPECT_FALSE(mesh->is_visible());
    EXPECT_EQ(mesh->visible_updates, 1);
}

TEST(Attachment_inheritance, local_true_on_mesh_survives_node_hide)
{
    auto node = std::make_shared<Xform>("node");
    auto mesh = std::make_shared<Counting_mesh>("mesh");
    erhe::scene::set_mesh_parent(mesh, node);
    mesh->show();
    node->hide();
    EXPECT_FALSE(node->is_visible());
    EXPECT_TRUE (mesh->is_visible());
    EXPECT_EQ   (mesh->visible_updates, 0);
}

TEST(Attachment_inheritance, moving_between_nodes_notifies_once_with_old_value)
{
    auto hidden = std::make_shared<Xform>("hidden");
    auto shown  = std::make_shared<Xform>("shown");
    auto mesh   = std::make_shared<Counting_mesh>("mesh");
    hidden->hide();

    struct Seen
    {
        bool         old_value;
        bool         new_value;
        Value_source old_source;
        Value_source new_source;
    };
    bool seen{false};
    Seen seen_args{};
    Observer_token token = mesh->add_observer(
        erhe::Item_base::visible_property.get(),
        [&](Dependency_object&, const Property_changed_args& args) {
            seen      = true;
            seen_args = Seen{get_as<bool>(args.old_value), get_as<bool>(args.new_value), args.old_source, args.new_source};
        }
    );

    erhe::scene::set_mesh_parent(mesh, hidden);
    EXPECT_FALSE(mesh->is_visible());
    ASSERT_TRUE(seen);
    EXPECT_EQ(seen_args.old_value, true);
    EXPECT_EQ(seen_args.new_value, false);
    EXPECT_EQ(seen_args.old_source, Value_source::default_value);
    EXPECT_EQ(seen_args.new_source, Value_source::inherited);
    EXPECT_EQ(mesh->visible_updates, 1);

    seen = false;
    erhe::scene::set_mesh_parent(mesh, shown);
    EXPECT_EQ(mesh->get_parent_node().get(), shown.get());
    EXPECT_TRUE(mesh->is_visible());
    ASSERT_TRUE(seen);
    EXPECT_EQ(seen_args.old_value, false);
    EXPECT_EQ(seen_args.new_value, true);
    EXPECT_EQ(mesh->visible_updates, 2);

    seen = false;
    erhe::scene::set_mesh_parent(mesh, {});
    EXPECT_TRUE(mesh->is_visible());
    EXPECT_FALSE(seen); // default true, unchanged
}

TEST(Attachment_inheritance, shadow_cast_on_group_reaches_meshes_without_local_value)
{
    auto group    = std::make_shared<Xform>("group");
    auto node_a   = std::make_shared<Xform>("a");
    auto node_b   = std::make_shared<Xform>("b");
    auto mesh_a   = std::make_shared<Mesh>("mesh a");
    auto mesh_b   = std::make_shared<Mesh>("mesh b");
    node_a->set_parent(group);
    node_b->set_parent(group);
    erhe::scene::set_mesh_parent(mesh_a, node_a);
    erhe::scene::set_mesh_parent(mesh_b, node_b);
    mesh_b->set_value(Mesh::shadow_cast_property, false);

    group->set_value(Mesh::shadow_cast_property, true);
    EXPECT_NE(mesh_a->get_flag_bits() & erhe::Item_flags::shadow_cast, 0u);
    EXPECT_EQ(mesh_b->get_flag_bits() & erhe::Item_flags::shadow_cast, 0u);

    group->clear_value(Mesh::shadow_cast_property);
    EXPECT_EQ(mesh_a->get_flag_bits() & erhe::Item_flags::shadow_cast, 0u);
}
