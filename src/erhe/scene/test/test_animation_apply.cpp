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
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

    erhe::scene::Animation_channel channel{};
    channel.path           = erhe::scene::Animation_path::TRANSLATION;
    channel.sampler_index  = 0;
    channel.target         = target;
    channel.start_position = 0;
    channel.value_offset   = 0;
    animation.channels.push_back(channel);
}

TEST(animation_apply, moves_the_animated_node)
{
    Test_scene_host host;

    auto node = std::make_shared<erhe::scene::Node>("animated node");
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

    auto parent = std::make_shared<erhe::scene::Node>("animated parent");
    auto child  = std::make_shared<erhe::scene::Node>("child");
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

    auto node = std::make_shared<erhe::scene::Node>("animated node");
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

} // anonymous namespace

