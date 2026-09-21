// Node systems (doc/erhe/scene.md "Node systems"): one system per node value
// group per scene owns the runtime state the group implies, and is driven
// from three change sites - a value of the group changing, the node entering
// or leaving the scene, and the derived Item_flags::active bit flipping.

#include "erhe_item/item.hpp"
#include "erhe_property/attached_group.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_property/property_metadata.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/node_system.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace {

using erhe::property::Property;
using erhe::property::Property_metadata;
using erhe::property::Property_ui;
using erhe::scene::INode_system;
using erhe::scene::Node;
using erhe::scene::Xform;

// A test value group on Node: the key says whether the node carries the
// feature, the strength is one of the group's other values.
[[nodiscard]] auto feature_owner_type() -> erhe::property::Owner_type
{
    static const erhe::property::Owner_type id = erhe::property::allocate_owner_type(erhe::property::root_owner_type, "Test_feature");
    return id;
}

const Property<bool> feature_enabled_property = Property<bool>::register_attached(
    "enabled", feature_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = false,
        .property_changed = erhe::scene::node_system_property_changed,
        .inherits         = false,
        .ui               = Property_ui{.group = "Test feature", .label = "Enabled"}
    }
);
const Property<float> feature_strength_property = Property<float>::register_attached(
    "strength", feature_owner_type(), erhe::scene::Node::property_owner_type(),
    Property_metadata{
        .default_value    = 1.0f,
        .property_changed = erhe::scene::node_system_property_changed,
        .ui               = Property_ui{
            .group        = "Test feature",
            .label        = "Strength",
            .visible_when = erhe::property::attached_group_visible_when(feature_enabled_property.get())
        }
    }
);

// The system's per-node runtime record: what a physics body or a card proxy
// mesh stands for in the real groups.
class Feature_record
{
public:
    float strength{1.0f};
    int   active_changes{0};
};

class Test_feature_system : public INode_system
{
public:
    void on_node_registered(Node& node) override
    {
        refresh(node);
    }
    void on_node_unregistered(Node& node) override
    {
        m_records.erase(&node);
    }
    void on_values_changed(Node& node, const erhe::property::Dependency_property& property) override
    {
        if (&property == feature_enabled_property.get_ptr()) {
            refresh(node);
            return;
        }
        const auto i = m_records.find(&node);
        if (i != m_records.end()) {
            i->second.strength = node.get_value(feature_strength_property);
        }
    }
    void on_node_active_changed(Node& node) override
    {
        const auto i = m_records.find(&node);
        if (i != m_records.end()) {
            ++i->second.active_changes;
        }
    }

    [[nodiscard]] auto get_record_count() const -> std::size_t
    {
        return m_records.size();
    }
    [[nodiscard]] auto find_record(const Node& node) const -> const Feature_record*
    {
        const auto i = m_records.find(&node);
        return (i != m_records.end()) ? &i->second : nullptr;
    }

private:
    void refresh(Node& node)
    {
        if (erhe::property::carries_attached_group(node, feature_enabled_property.get())) {
            Feature_record& record = m_records[&node];
            record.strength = node.get_value(feature_strength_property);
        } else {
            m_records.erase(&node);
        }
    }

    std::unordered_map<const Node*, Feature_record> m_records;
};

class Test_scene_host : public erhe::scene::Scene_host
{
public:
    Test_scene_host()
        : scene{"test scene", this}
    {
    }

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

TEST(Node_systems, a_system_is_added_and_removed)
{
    Test_scene_host     host;
    Test_feature_system system;

    // A scene starts with its own Layout_system already added.
    const std::size_t own_systems = host.scene.get_node_system_count();
    host.scene.add_node_system(system);
    EXPECT_EQ(host.scene.get_node_system_count(), own_systems + 1);
    host.scene.remove_node_system(system);
    EXPECT_EQ(host.scene.get_node_system_count(), own_systems);
}

TEST(Node_systems, the_key_property_creates_and_destroys_the_record)
{
    Test_scene_host     host;
    Test_feature_system system;
    host.scene.add_node_system(system);

    const std::shared_ptr<Xform> node = make_hosted_node(host, "node");
    EXPECT_EQ(system.get_record_count(), std::size_t{0});

    node->set_value(feature_enabled_property, true);
    ASSERT_NE(system.find_record(*node), nullptr);
    EXPECT_EQ(system.get_record_count(), std::size_t{1});

    node->set_value(feature_strength_property, 0.5f);
    ASSERT_NE(system.find_record(*node), nullptr);
    EXPECT_EQ(system.find_record(*node)->strength, 0.5f);

    node->clear_value(feature_enabled_property);
    EXPECT_EQ(system.find_record(*node), nullptr);
    EXPECT_EQ(system.get_record_count(), std::size_t{0});

    host.scene.remove_node_system(system);
}

TEST(Node_systems, a_node_already_carrying_the_feature_gets_its_record_when_it_enters)
{
    Test_scene_host     host;
    Test_feature_system system;
    host.scene.add_node_system(system);

    std::shared_ptr<Xform> node = std::make_shared<Xform>("node");
    node->set_value(feature_enabled_property, true);
    node->set_value(feature_strength_property, 0.25f);
    EXPECT_EQ(system.get_record_count(), std::size_t{0}); // not in the scene yet

    node->set_parent(host.scene.get_root_node());
    ASSERT_NE(system.find_record(*node), nullptr);
    EXPECT_EQ(system.find_record(*node)->strength, 0.25f);

    node->set_parent(static_cast<erhe::Hierarchy*>(nullptr));
    EXPECT_EQ(system.get_record_count(), std::size_t{0});

    host.scene.remove_node_system(system);
}

TEST(Node_systems, an_active_bit_flip_reaches_the_record)
{
    Test_scene_host     host;
    Test_feature_system system;
    host.scene.add_node_system(system);

    const std::shared_ptr<Xform> node = make_hosted_node(host, "node");
    node->set_value(feature_enabled_property, true);
    ASSERT_NE(system.find_record(*node), nullptr);
    EXPECT_EQ(system.find_record(*node)->active_changes, 0);

    node->set_value(erhe::Item_base::active_property, false);
    EXPECT_FALSE(node->is_active());
    EXPECT_EQ(system.find_record(*node)->active_changes, 1);

    node->set_value(erhe::Item_base::active_property, true);
    EXPECT_TRUE(node->is_active());
    EXPECT_EQ(system.find_record(*node)->active_changes, 2);

    host.scene.remove_node_system(system);
}

TEST(Node_systems, a_node_outside_a_scene_reaches_no_system)
{
    Test_scene_host     host;
    Test_feature_system system;
    host.scene.add_node_system(system);

    const std::shared_ptr<Xform> node = std::make_shared<Xform>("unhosted");
    node->set_value(feature_enabled_property, true);
    EXPECT_EQ(system.get_record_count(), std::size_t{0});

    host.scene.remove_node_system(system);
}
