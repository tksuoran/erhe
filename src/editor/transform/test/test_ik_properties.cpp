// Per-bone IK settings as attached erhe::property properties of the bone
// node (doc/plans/rigging/ik_settings.md section 1): the values live on the
// node, none of them inherits, a Style can supply a shared limit set, and
// Ik.rest_rotation's per-object default is the node's bind-pose local
// rotation.

#include "scene/ik_properties.hpp"

#include "erhe_property/dependency_property.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_scene/transform.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <memory>

using namespace erhe::property;
using editor::Ik;
using editor::Ik_settings_data;
using erhe::scene::Node;

namespace {

// A scene needs a host for its nodes to report get_scene(), which is what the
// bind-pose lookup walks to reach the skins (the erhe_scene tests' pattern).
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
    void register_skin    (const std::shared_ptr<erhe::scene::Skin>& skin)     override { scene.register_skin  (skin); }
    void unregister_skin  (const std::shared_ptr<erhe::scene::Skin>& skin)     override { scene.unregister_skin(skin); }
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

[[nodiscard]] auto make_bone(const std::string_view name) -> std::shared_ptr<erhe::scene::Xform>
{
    auto node = std::make_shared<erhe::scene::Xform>(name);
    node->enable_flag_bits(erhe::Item_flags::bone);
    return node;
}

[[nodiscard]] auto quat_close(const glm::quat& a, const glm::quat& b) -> bool
{
    return std::abs(std::abs(glm::dot(a, b)) - 1.0f) < 1.0e-4f;
}

} // anonymous namespace

TEST(Ik_properties, defaults_match_the_data_defaults)
{
    const std::shared_ptr<erhe::scene::Xform> bone = make_bone("bone");

    EXPECT_EQ(editor::read_ik_settings(*bone), Ik_settings_data{});
    EXPECT_FALSE(bone->get_value(Ik::limit_x_property));
    EXPECT_EQ(bone->get_value(Ik::limit_min_property), glm::vec3{-glm::pi<float>()});
    EXPECT_EQ(bone->get_value_source(Ik::limit_max_property), Value_source::default_value);
    EXPECT_EQ(editor::get_ik_pole_target(*bone), nullptr);
    EXPECT_FALSE(editor::has_local_ik_value(*bone));
}

TEST(Ik_properties, every_property_is_attached_to_node_and_does_not_inherit)
{
    // P2: owner type Ik (qualified Ik.lock_x ..), holder type Node.
    // P3: none of them inherits.
    const Property_registry& registry = Property_registry::get();
    for (const Dependency_property* const property : Ik::all_properties()) {
        EXPECT_TRUE(property->is_attached()) << property->get_name();
        EXPECT_TRUE(property->applies_to(Node::property_owner_type())) << property->get_name();
        EXPECT_FALSE(property->get_metadata(Node::property_owner_type()).inherits) << property->get_name();
        EXPECT_EQ(registry.qualified_name(*property), std::string{"Ik."} + std::string{property->get_name()});
    }
    EXPECT_EQ(registry.find_for_object(Node::property_owner_type(), "Ik.limit_x"), Ik::limit_x_property.get_ptr());
}

TEST(Ik_properties, setters_write_the_node_and_limits_are_coerced)
{
    const std::shared_ptr<erhe::scene::Xform> bone = make_bone("bone");

    bone->set_value(Ik::lock_y_property, true);
    bone->set_value(Ik::limit_z_property, true);
    bone->set_value(Ik::limit_min_property, glm::vec3{-4.0f, -0.5f, 1.0f});
    bone->set_value(Ik::limit_max_property, glm::vec3{0.5f, 4.0f, -1.0f});
    bone->set_value(Ik::stiffness_property, glm::vec3{2.0f, 0.5f, -1.0f});

    const Ik_settings_data data = editor::read_ik_settings(*bone);
    EXPECT_TRUE (data.lock[1]);
    EXPECT_TRUE (data.limit[2]);
    EXPECT_EQ   (data.limit_min, (glm::vec3{-glm::pi<float>(), -0.5f, 0.0f}));
    EXPECT_EQ   (data.limit_max, (glm::vec3{0.5f, glm::pi<float>(), 0.0f}));
    EXPECT_EQ   (data.stiffness, (glm::vec3{0.99f, 0.5f, 0.0f}));
    EXPECT_TRUE (editor::has_local_ik_value(*bone));

    bone->clear_value(Ik::lock_y_property);
    EXPECT_FALSE(editor::read_ik_settings(*bone).lock[1]);
}

TEST(Ik_properties, a_parent_bones_value_does_not_reach_the_child)
{
    // P3: a limit set shared by several bones is a Style, never inheritance.
    const std::shared_ptr<erhe::scene::Xform> parent = make_bone("parent");
    const std::shared_ptr<Node> child  = make_bone("child");
    child->set_parent(parent);

    parent->set_value(Ik::limit_x_property, true);
    EXPECT_TRUE (parent->get_value(Ik::limit_x_property));
    EXPECT_FALSE(child->get_value(Ik::limit_x_property));
    EXPECT_EQ   (child->get_value_source(Ik::limit_x_property), Value_source::default_value);
}

TEST(Ik_properties, a_style_supplies_a_shared_limit_set)
{
    const std::shared_ptr<erhe::scene::Xform> style = std::make_shared<erhe::scene::Xform>("limits");
    style->set_value(Ik::limit_x_property,   true);
    style->set_value(Ik::limit_min_property, glm::vec3{-1.0f, -2.0f, -3.0f});

    const std::shared_ptr<erhe::scene::Xform> bone = make_bone("bone");
    ASSERT_TRUE(bone->set_style(style));

    EXPECT_EQ  (bone->get_value_source(Ik::limit_x_property), Value_source::style);
    EXPECT_TRUE(editor::read_ik_settings(*bone).limit[0]);
    EXPECT_EQ  (editor::read_ik_settings(*bone).limit_min, (glm::vec3{-1.0f, -2.0f, -3.0f}));

    // A local value still wins over the style.
    bone->set_value(Ik::limit_x_property, false);
    EXPECT_FALSE(editor::read_ik_settings(*bone).limit[0]);
}

TEST(Ik_properties, rest_rotation_default_is_identity_without_a_skin)
{
    // P5: no skin lists the node and its parent, so the default is identity.
    const std::shared_ptr<erhe::scene::Xform> parent = make_bone("parent");
    const std::shared_ptr<Node> bone   = make_bone("bone");
    bone->set_parent(parent);
    bone->set_parent_from_node(glm::rotate(glm::mat4{1.0f}, 0.5f, glm::vec3{0.0f, 0.0f, 1.0f}));

    EXPECT_EQ(bone->get_value_source(Ik::rest_rotation_property), Value_source::default_value);
    EXPECT_TRUE(quat_close(bone->get_value(Ik::rest_rotation_property), glm::quat{1.0f, 0.0f, 0.0f, 0.0f}));
}

TEST(Ik_properties, rest_rotation_default_is_the_bind_pose_with_a_skin)
{
    Test_scene_host             host;
    erhe::scene::Scene&         scene  = host.scene;
    const std::shared_ptr<Node> root   = scene.get_root_node();
    const std::shared_ptr<erhe::scene::Xform> parent = make_bone("parent");
    const std::shared_ptr<Node> bone   = make_bone("bone");
    parent->set_parent(root);
    bone->set_parent(parent);

    // Bind pose: the parent at the identity, the bone rotated 0.5 rad about
    // Z. The skin carries no inverse bind matrices, so Skin_data's identity
    // fallback makes world_from_bind the joint's own world matrix and the
    // bind-pose local rotation the bone's local rotation.
    const glm::mat4 bind_parent{1.0f};
    const glm::mat4 bind_bone = glm::rotate(glm::mat4{1.0f}, 0.5f, glm::vec3{0.0f, 0.0f, 1.0f});
    parent->set_parent_from_node(bind_parent);
    bone->set_parent_from_node(bind_bone);

    auto skin = std::make_shared<erhe::scene::Skin>("skin");
    skin->skin_data.joints = {parent, bone};
    scene.register_skin(skin);

    const glm::quat expected = glm::quat_cast(glm::mat3{bind_bone});
    EXPECT_EQ  (bone->get_value_source(Ik::rest_rotation_property), Value_source::default_value);
    EXPECT_TRUE(quat_close(bone->get_value(Ik::rest_rotation_property), expected));
    EXPECT_TRUE(quat_close(editor::read_ik_settings(*bone).rest_rotation, expected));

    // A local value overrides the computed default (P5).
    const glm::quat authored = glm::angleAxis(1.0f, glm::vec3{1.0f, 0.0f, 0.0f});
    bone->set_value(Ik::rest_rotation_property, authored);
    EXPECT_EQ  (bone->get_value_source(Ik::rest_rotation_property), Value_source::local);
    EXPECT_TRUE(quat_close(bone->get_value(Ik::rest_rotation_property), authored));

    // The root bone of the skin has no joint parent: identity.
    EXPECT_TRUE(quat_close(parent->get_value(Ik::rest_rotation_property), glm::quat{1.0f, 0.0f, 0.0f, 0.0f}));

    scene.unregister_skin(skin);
}

TEST(Ik_properties, pole_target_is_a_weak_reference)
{
    const std::shared_ptr<erhe::scene::Xform> bone = make_bone("bone");
    std::shared_ptr<erhe::scene::Xform>       pole = std::make_shared<erhe::scene::Xform>("pole");

    EXPECT_EQ(Ik::pole_target_property.get().get_type(), Property_type::weak_object);

    editor::set_ik_pole_target(*bone, pole);
    EXPECT_EQ(editor::get_ik_pole_target(*bone), pole);
    EXPECT_EQ(pole.use_count(), 1); // the property holds no ownership

    pole.reset();
    EXPECT_EQ(editor::get_ik_pole_target(*bone), nullptr);
}

TEST(Ik_properties, pole_target_refuses_a_non_node_item)
{
    const std::shared_ptr<Node>                bone      = make_bone("bone");
    const std::shared_ptr<erhe::scene::Skin>   not_a_node = std::make_shared<erhe::scene::Skin>("skin");

    const bool accepted = bone->set_value(
        Ik::pole_target_property.get(),
        Weak_object_reference{not_a_node}
    );
    EXPECT_FALSE(accepted);
    EXPECT_EQ(editor::get_ik_pole_target(*bone), nullptr);
}

TEST(Ik_properties, a_clone_keeps_the_values_and_the_pole)
{
    // P9: entry-stored values copy with the node (D10).
    const std::shared_ptr<erhe::scene::Xform> bone = make_bone("bone");
    const std::shared_ptr<erhe::scene::Xform> pole = std::make_shared<erhe::scene::Xform>("pole");
    bone->set_value(Ik::lock_x_property, true);
    bone->set_value(Ik::limit_max_property, glm::vec3{1.0f});
    bone->set_value(Ik::pole_angle_property, 0.25f);
    editor::set_ik_pole_target(*bone, pole);

    const auto clone = std::static_pointer_cast<erhe::scene::Xform>(bone->clone());
    ASSERT_TRUE(clone);

    const Ik_settings_data data = editor::read_ik_settings(*clone);
    EXPECT_TRUE(data.lock[0]);
    EXPECT_EQ  (data.limit_max, glm::vec3{1.0f});
    EXPECT_EQ  (data.pole_angle, 0.25f);
    EXPECT_EQ  (clone->get_value_source(Ik::lock_x_property), Value_source::local);
    EXPECT_EQ  (clone->get_value_source(Ik::limit_min_property), Value_source::default_value);
    EXPECT_EQ  (editor::get_ik_pole_target(*clone), pole);
}
