#include "scene/physics_edits.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

void reapply_physics_material(App_context& context, const std::shared_ptr<erhe::physics::Physics_material>& physics_material)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
            const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
            if (node_physics && (node_physics->get_physics_material() == physics_material)) {
                node_physics->set_physics_material(physics_material);
            }
        }
    }
}

void reapply_collision_filter(App_context& context, const std::shared_ptr<erhe::physics::Collision_filter>& collision_filter)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
            const std::shared_ptr<Node_physics> node_physics = erhe::scene::get_attachment<Node_physics>(node.get());
            if (node_physics && (node_physics->get_collision_filter() == collision_filter)) {
                node_physics->set_collision_filter(collision_filter);
            }
        }
    }
}

void rebuild_joints_using_settings(App_context& context, const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        for (const std::shared_ptr<erhe::scene::Node>& node : scene_root->get_scene().get_flat_nodes()) {
            for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
                const std::shared_ptr<Node_joint> node_joint = std::dynamic_pointer_cast<Node_joint>(attachment);
                if (node_joint && (node_joint->get_settings() == settings)) {
                    node_joint->rebuild();
                }
            }
        }
    }
}

}
