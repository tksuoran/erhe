// Playing an animation must move the rendered scene.
//
// Animation_sampler::apply() writes the sampled TRS component straight into the
// target node's parent_from_node, bypassing the Node transform setters.
// Animation::apply() is therefore responsible for updating the target's world
// transform and calling handle_transform_update() - which notifies attachments
// and marks the node dirty for Scene::update_node_transforms(). Since transform
// propagation became dirty-list driven, skipping that notification leaves both
// the animated node and its descendants at their previous pose, so the viewport
// keeps rendering the old frame no matter which renderer draws it.

#include "erhe_scene/animation.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"
#include "erhe_scene/xform.hpp"
#include "erhe_property/dependency_property.hpp"
#include "erhe_scene/xform_op.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>

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
    void on_mesh_display_color_changed (const std::shared_ptr<erhe::scene::Mesh>&) override {}
    void on_light_changed              (const std::shared_ptr<erhe::scene::Light>&) override {}

    erhe::scene::Scene scene;
};

// One translation channel on X: key 0 at t=0, key 10 at t=1.
// Fills in place: Animation's copy constructor is explicit, so it cannot be
// returned by value.
void make_translation_animation(erhe::scene::Animation& animation, const std::shared_ptr<erhe::scene::Node>& target)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(
        std::vector<float>{0.0f, 1.0f},
        std::vector<float>{
            0.0f, 0.0f, 0.0f,
            10.0f, 0.0f, 0.0f
        }
    );
    animation.samplers.push_back(std::move(sampler));

    animation.channels.push_back(
        erhe::scene::make_transform_channel(target, erhe::scene::Animation_path::TRANSLATION, 0)
    );
}

TEST(animation_apply, moves_the_animated_node)
{
    Test_scene_host host;

    auto node = std::make_shared<erhe::scene::Xform>("animated node");
    node->set_parent(host.scene.get_root_node());

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node);

    animation.apply(0.5f);
    host.scene.update_node_transforms();

    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 5.0f);
    EXPECT_FLOAT_EQ(node->world_from_node ()[3][0], 5.0f);
}

TEST(animation_apply, moves_the_children_of_the_animated_node)
{
    Test_scene_host host;

    auto parent = std::make_shared<erhe::scene::Xform>("animated parent");
    auto child  = std::make_shared<erhe::scene::Xform>("child");
    parent->set_parent(host.scene.get_root_node());
    child->set_parent(parent);
    child->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 2.0f, 0.0f}));

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, parent);

    animation.apply(1.0f);
    host.scene.update_node_transforms();

    EXPECT_FLOAT_EQ(child->world_from_node()[3][0], 10.0f);
    EXPECT_FLOAT_EQ(child->world_from_node()[3][1],  2.0f);
}

// Each applied frame must move the node again, not just the first one.
TEST(animation_apply, keeps_moving_the_node_on_later_frames)
{
    Test_scene_host host;

    auto node = std::make_shared<erhe::scene::Xform>("animated node");
    node->set_parent(host.scene.get_root_node());

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node);

    animation.apply(0.25f);
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->world_from_node()[3][0], 2.5f);

    animation.apply(0.75f);
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->world_from_node()[3][0], 7.5f);
}

// Playback writes the animated layer (doc/erhe/property_system.md D5), not the
// authored transform: the prim reads the pose, the transform it authored stays
// readable as the base under it, and nothing a save looks at moves.
TEST(animation_apply, writes_the_animated_layer_and_keeps_the_authored_pose)
{
    using erhe::property::Value_source;
    Test_scene_host host;

    auto node = std::make_shared<erhe::scene::Xform>("animated node");
    node->set_parent(host.scene.get_root_node());
    node->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}));

    erhe::scene::Xform_op_stack stack;
    erhe::scene::Xform_op op{};
    op.type  = erhe::scene::Xform_op_type::translate;
    op.value = glm::dvec3{1.0, 2.0, 3.0};
    stack.ops.push_back(op);
    node->set_xform_op_stack(stack);

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node);

    animation.apply(0.5f);
    host.scene.update_node_transforms();

    // The pose is what the prim and the scene read.
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 5.0f);
    EXPECT_FLOAT_EQ(node->world_from_node ()[3][0], 5.0f);
    EXPECT_FLOAT_EQ(node->get_value(erhe::scene::Xformable::translation_property).x, 5.0f);
    EXPECT_EQ(node->get_value_source(erhe::scene::Xformable::translation_property.get()), Value_source::animated);
    EXPECT_TRUE(node->has_animated_value(erhe::scene::Xformable::translation_property.get()));

    // The authored pose is the base under it, and it is what a save sees.
    EXPECT_FLOAT_EQ(node->get_animation_base_value(erhe::scene::Xformable::translation_property).x, 1.0f);
    EXPECT_FLOAT_EQ(node->authored_parent_from_node_transform().get_translation().x, 1.0f);
    EXPECT_TRUE(node->is_local_transform_animated());
    const std::optional<glm::vec3> local = node->read_local_value(erhe::scene::Xformable::translation_property);
    ASSERT_TRUE(local.has_value());
    EXPECT_FLOAT_EQ(local.value().x, 1.0f);

    std::size_t seen_translations = 0;
    node->for_each_local_value(
        [&seen_translations](const erhe::property::Dependency_property& property, const erhe::property::Property_value& value) {
            if (property.get_name() != "translation") {
                return;
            }
            ++seen_translations;
            EXPECT_FLOAT_EQ(std::get<glm::vec3>(value).x, 1.0f);
        }
    );
    EXPECT_EQ(seen_translations, 1);

    // The authored xformOp stack is the base, so playback does not touch it.
    ASSERT_TRUE(node->has_xform_op_stack());
    ASSERT_EQ(node->get_xform_op_stack()->ops.size(), 1);
    EXPECT_EQ(std::get<glm::dvec3>(node->get_xform_op_stack()->ops.front().value).x, 1.0);
}

// Stopping an animation drops the layer, so the prim holds what it authored.
TEST(animation_apply, clear_applied_restores_the_authored_pose)
{
    using erhe::property::Value_source;
    Test_scene_host host;

    auto node = std::make_shared<erhe::scene::Xform>("animated node");
    node->set_parent(host.scene.get_root_node());
    node->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 2.0f, 3.0f}));

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node);

    animation.apply(1.0f);
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->world_from_node()[3][0], 10.0f);

    animation.clear_applied();
    host.scene.update_node_transforms();

    EXPECT_FALSE(node->is_local_transform_animated());
    EXPECT_EQ(node->get_value_source(erhe::scene::Xformable::translation_property.get()), Value_source::local);
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 1.0f);
    EXPECT_FLOAT_EQ(node->world_from_node ()[3][0], 1.0f);
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][1], 2.0f);
}

// A transform edit made while an animation plays over the prim is an edit of
// the authored pose, not of the playback pose: the next sampled frame still
// plays, and the edit is what stopping shows.
TEST(animation_apply, an_edit_while_playing_writes_the_base)
{
    Test_scene_host host;

    auto node = std::make_shared<erhe::scene::Xform>("animated node");
    node->set_parent(host.scene.get_root_node());

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node);

    animation.apply(0.5f);
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 5.0f);

    node->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{0.0f, 7.0f, 0.0f}));

    // The pose is untouched.
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 5.0f);
    EXPECT_FLOAT_EQ(node->get_animation_base_value(erhe::scene::Xformable::translation_property).y, 7.0f);

    // The next frame still plays.
    animation.apply(1.0f);
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 10.0f);

    // Stopping shows the edit.
    animation.clear_applied();
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 0.0f);
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][1], 7.0f);
}

// Only some components animated (the usual sampled stack): a write of a
// component the animation does not hold must carry the AUTHORED transform into
// the stack - never the pose the animated components are in - and must not
// write the stack's composition back over them.
TEST(animation_apply, an_edit_of_an_unanimated_component_keeps_the_pose_and_the_authored_stack)
{
    Test_scene_host host;

    auto node = std::make_shared<erhe::scene::Xform>("animated node");
    node->set_parent(host.scene.get_root_node());

    erhe::scene::Xform_op_stack stack;
    erhe::scene::Xform_op translate_op{};
    translate_op.type  = erhe::scene::Xform_op_type::translate;
    translate_op.value = glm::dvec3{1.0, 2.0, 3.0};
    stack.ops.push_back(translate_op);
    erhe::scene::Xform_op rotate_op{};
    rotate_op.type  = erhe::scene::Xform_op_type::orient;
    rotate_op.value = glm::dquat{1.0, 0.0, 0.0, 0.0};
    stack.ops.push_back(rotate_op);
    node->set_xform_op_stack(stack);

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node); // translation only

    animation.apply(1.0f);
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->parent_from_node()[3][0], 10.0f);

    // A rotation edit, with the authored translation beside it - what a
    // transform tool writes.
    const glm::quat new_rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3{0.0f, 1.0f, 0.0f});
    erhe::scene::Trs_transform edit;
    edit.set_trs(glm::vec3{1.0f, 2.0f, 3.0f}, new_rotation, glm::vec3{1.0f, 1.0f, 1.0f});
    node->set_parent_from_node(edit);

    // The pose is untouched.
    EXPECT_FLOAT_EQ(node->parent_from_node_transform().get_translation().x, 10.0f);
    // The stack holds the authored translation and the edited rotation.
    ASSERT_TRUE(node->has_xform_op_stack());
    ASSERT_EQ(node->get_xform_op_stack()->ops.size(), 2);
    EXPECT_EQ(std::get<glm::dvec3>(node->get_xform_op_stack()->ops[0].value), glm::dvec3(1.0, 2.0, 3.0));
    const glm::dquat stack_rotation = std::get<glm::dquat>(node->get_xform_op_stack()->ops[1].value);
    EXPECT_NEAR(static_cast<float>(stack_rotation.y), new_rotation.y, 1e-5f);
    EXPECT_NEAR(static_cast<float>(stack_rotation.w), new_rotation.w, 1e-5f);

    const erhe::scene::Trs_transform authored = node->authored_parent_from_node_transform();
    EXPECT_FLOAT_EQ(authored.get_translation().x, 1.0f);
    EXPECT_FLOAT_EQ(authored.get_translation().y, 2.0f);
    EXPECT_NEAR(authored.get_rotation().y, new_rotation.y, 1e-5f);

    animation.clear_applied();
    host.scene.update_node_transforms();
    EXPECT_FLOAT_EQ(node->parent_from_node_transform().get_translation().x, 1.0f);
    EXPECT_FLOAT_EQ(node->parent_from_node_transform().get_translation().z, 3.0f);
    EXPECT_NEAR(node->parent_from_node_transform().get_rotation().y, new_rotation.y, 1e-5f);
}

// --- Channels driving properties other than the transform ---------------
//
// An Animation_channel names a Dependency_property of its target, so a clip
// drives any registered property whose type a sampler can carry, and the
// three transform components are just the common case.

// One float channel on the light's intensity: key 1 at t=0, key 5 at t=1.
void make_intensity_animation(erhe::scene::Animation& animation, const std::shared_ptr<erhe::scene::Light>& target)
{
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(
        std::vector<float>{0.0f, 1.0f},
        std::vector<float>{1.0f, 5.0f}
    );
    animation.samplers.push_back(std::move(sampler));

    erhe::scene::Animation_channel channel{};
    channel.property      = erhe::scene::Light::intensity_property.get_ptr();
    channel.sampler_index = animation.samplers.size() - 1;
    channel.target        = target;
    animation.channels.push_back(channel);
}

TEST(animation_property_channel, a_float_channel_interpolates_and_writes_the_animated_layer)
{
    using erhe::property::Value_source;

    auto light = std::make_shared<erhe::scene::Light>("animated light");
    light->set_intensity(2.0f);

    erhe::scene::Animation animation{"test animation"};
    make_intensity_animation(animation, light);

    animation.apply(0.5f);

    EXPECT_FLOAT_EQ(light->get_value(erhe::scene::Light::intensity_property), 3.0f);
    EXPECT_EQ(light->get_value_source(erhe::scene::Light::intensity_property.get()), Value_source::animated);
    EXPECT_TRUE(light->has_animated_value(erhe::scene::Light::intensity_property.get()));

    // The authored intensity is the base under the pose, and it is what a
    // save sees.
    EXPECT_FLOAT_EQ(light->get_animation_base_value(erhe::scene::Light::intensity_property), 2.0f);

    animation.apply(1.0f);
    EXPECT_FLOAT_EQ(light->get_value(erhe::scene::Light::intensity_property), 5.0f);

    animation.clear_applied();
    EXPECT_FALSE(light->has_animated_value(erhe::scene::Light::intensity_property.get()));
    EXPECT_FLOAT_EQ(light->get_value(erhe::scene::Light::intensity_property), 2.0f);
}

// A boolean has no value between two keys: it holds the previous key whatever
// the sampler's interpolation mode says.
TEST(animation_property_channel, a_bool_channel_holds_the_previous_key)
{
    auto node = std::make_shared<erhe::scene::Xform>("animated node");

    erhe::scene::Animation animation{"test animation"};
    erhe::scene::Animation_sampler sampler{erhe::scene::Animation_interpolation_mode::LINEAR};
    sampler.set(
        std::vector<float>{0.0f, 1.0f, 2.0f},
        std::vector<float>{0.0f, 1.0f, 0.0f}
    );
    animation.samplers.push_back(std::move(sampler));

    erhe::scene::Animation_channel channel{};
    channel.property      = erhe::Item_base::visible_property.get_ptr();
    channel.sampler_index = 0;
    channel.target        = node;
    animation.channels.push_back(channel);

    animation.apply(0.0f);
    EXPECT_FALSE(node->get_value(erhe::Item_base::visible_property));
    animation.apply(0.5f);
    EXPECT_FALSE(node->get_value(erhe::Item_base::visible_property));
    animation.apply(1.0f);
    EXPECT_TRUE(node->get_value(erhe::Item_base::visible_property));
    animation.apply(1.5f);
    EXPECT_TRUE(node->get_value(erhe::Item_base::visible_property));
    animation.apply(2.0f);
    EXPECT_FALSE(node->get_value(erhe::Item_base::visible_property));

    animation.clear_applied();
    EXPECT_FALSE(node->has_animated_value(erhe::Item_base::visible_property.get()));
    EXPECT_TRUE(node->get_value(erhe::Item_base::visible_property));
}

// One clip, a transform channel and a non-transform channel: both play, and
// clear_applied() puts both back on what they authored.
TEST(animation_property_channel, a_mixed_clip_applies_and_clears_both_kinds)
{
    Test_scene_host host;

    auto node  = std::make_shared<erhe::scene::Xform>("animated node");
    auto light = std::make_shared<erhe::scene::Light>("animated light");
    node->set_parent(host.scene.get_root_node());
    node->set_parent_from_node(glm::translate(glm::mat4{1.0f}, glm::vec3{1.0f, 0.0f, 0.0f}));
    light->set_intensity(2.0f);

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node);
    make_intensity_animation(animation, light);

    animation.apply(1.0f);
    host.scene.update_node_transforms();

    EXPECT_FLOAT_EQ(node->world_from_node()[3][0], 10.0f);
    EXPECT_FLOAT_EQ(light->get_value(erhe::scene::Light::intensity_property), 5.0f);
    EXPECT_TRUE(node->is_local_transform_animated());
    EXPECT_TRUE(light->has_animated_value(erhe::scene::Light::intensity_property.get()));

    animation.clear_applied();
    host.scene.update_node_transforms();

    EXPECT_FALSE(node->is_local_transform_animated());
    EXPECT_FALSE(light->has_animated_value(erhe::scene::Light::intensity_property.get()));
    EXPECT_FLOAT_EQ(node->world_from_node()[3][0], 1.0f);
    EXPECT_FLOAT_EQ(light->get_value(erhe::scene::Light::intensity_property), 2.0f);
}

// Animation_path is a classification of the driven property, not a field.
TEST(animation_property_channel, get_animation_path_classifies_the_driven_property)
{
    auto node  = std::make_shared<erhe::scene::Xform>("n");
    auto light = std::make_shared<erhe::scene::Light>("l");

    erhe::scene::Animation animation{"test animation"};
    make_translation_animation(animation, node);
    make_intensity_animation(animation, light);

    EXPECT_EQ(erhe::scene::get_animation_path(animation.channels[0]), erhe::scene::Animation_path::TRANSLATION);
    EXPECT_EQ(erhe::scene::get_animation_path(animation.channels[1]), erhe::scene::Animation_path::INVALID);
    EXPECT_EQ(erhe::scene::get_component_count(animation.channels[0]), 3);
    EXPECT_EQ(erhe::scene::get_component_count(animation.channels[1]), 1);

    EXPECT_EQ(
        erhe::scene::make_transform_channel(node, erhe::scene::Animation_path::ROTATION, 0).property,
        erhe::scene::Xformable::rotation_property.get_ptr()
    );
    EXPECT_EQ(
        erhe::scene::get_animation_path(erhe::scene::make_transform_channel(node, erhe::scene::Animation_path::SCALE, 0)),
        erhe::scene::Animation_path::SCALE
    );
    EXPECT_TRUE(erhe::scene::is_animatable(erhe::scene::Light::intensity_property.get()));
    EXPECT_FALSE(erhe::scene::is_animatable(erhe::Item_base::name_property.get()));
}

} // anonymous namespace


// The keyed time range and the counts as computed properties (D26,
// doc/erhe/property_system.md section 4.16): read-only, always current on read,
// pushed to expression readers by notify_keyframes_changed().
TEST(animation_apply, time_range_and_counts_are_computed_properties)
{
    using erhe::property::Value_source;
    auto animation = std::make_shared<erhe::scene::Animation>("a");
    auto node      = std::make_shared<erhe::scene::Xform>("n");
    EXPECT_EQ(animation->get_value(erhe::scene::Animation::sampler_count_property), 0);
    EXPECT_EQ(animation->get_value(erhe::scene::Animation::channel_count_property), 0);
    EXPECT_TRUE(erhe::scene::Animation::first_time_property.get().is_read_only());

    make_translation_animation(*animation, node);
    EXPECT_EQ(animation->get_value(erhe::scene::Animation::sampler_count_property), 1);
    EXPECT_EQ(animation->get_value(erhe::scene::Animation::channel_count_property), 1);
    EXPECT_FLOAT_EQ(animation->get_value(erhe::scene::Animation::first_time_property), 0.0f);
    EXPECT_FLOAT_EQ(animation->get_value(erhe::scene::Animation::last_time_property), 1.0f);
    EXPECT_EQ(animation->get_value_source(erhe::scene::Animation::last_time_property.get()), Value_source::computed);
    EXPECT_FALSE(animation->set_value(erhe::scene::Animation::last_time_property.get(), erhe::property::Property_value{5.0f}));

    // A keyframe edit is visible on read at once; the push is a no-op
    // while nothing reads the properties.
    animation->samplers[0].timestamps.back() = 3.0f;
    EXPECT_FLOAT_EQ(animation->get_value(erhe::scene::Animation::last_time_property), 3.0f);
    animation->notify_keyframes_changed();
    EXPECT_FLOAT_EQ(animation->get_value(erhe::scene::Animation::last_time_property), 3.0f);
    EXPECT_FALSE(animation->has_local_value(erhe::scene::Animation::last_time_property.get()));
}
