#include "scene/physics_edits.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "scene/node_joint.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

namespace editor {

void rebuild_joints_using_settings(App_context& context, const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings)
{
    if (context.app_scenes == nullptr) {
        return;
    }
    for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
        scene_root->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
            for (const std::shared_ptr<erhe::scene::Node_attachment>& attachment : node->get_attachments()) {
                const std::shared_ptr<Node_joint> node_joint = std::dynamic_pointer_cast<Node_joint>(attachment);
                if (node_joint && (node_joint->get_settings() == settings)) {
                    node_joint->rebuild();
                }
            }
            return true;
        });
    }
}

}
