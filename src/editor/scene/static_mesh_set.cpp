#include "scene/static_mesh_set.hpp"

#include "content_library/content_library.hpp"
#include "scene/node_physics.hpp"

#include "erhe_physics/irigid_body.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/node.hpp"

#include <memory>
#include <mutex>
#include <optional>

namespace editor {

auto is_moved_by_physics(const erhe::scene::Node& node) -> bool
{
    const erhe::scene::Node* current = &node;
    std::shared_ptr<erhe::scene::Node> parent;
    while (current != nullptr) {
        const std::optional<Node_physics_data> physics = read_node_physics(*current);
        if (
            physics.has_value() &&
            (physics->motion_mode != erhe::physics::Motion_mode::e_none) &&
            (physics->motion_mode != erhe::physics::Motion_mode::e_static)
        ) {
            return true;
        }
        parent  = current->get_parent_node();
        current = parent.get();
    }
    return false;
}

void collect_animated_nodes(Content_library& content_library, std::unordered_set<const erhe::scene::Node*>& out_nodes)
{
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{content_library.mutex};
    for (const std::shared_ptr<erhe::scene::Animation>& animation : content_library.get_all<erhe::scene::Animation>()) {
        for (const erhe::scene::Animation_channel& channel : animation->channels) {
            const std::shared_ptr<erhe::scene::Xformable> target = erhe::scene::get_target_node(channel);
            if (target) {
                out_nodes.insert(target.get());
            }
        }
    }
}

auto is_animated(const erhe::scene::Node& node, const std::unordered_set<const erhe::scene::Node*>& animated_nodes) -> bool
{
    if (animated_nodes.empty()) {
        return false;
    }
    const erhe::scene::Node* current = &node;
    std::shared_ptr<erhe::scene::Node> parent;
    while (current != nullptr) {
        if (animated_nodes.contains(current)) {
            return true;
        }
        parent  = current->get_parent_node();
        current = parent.get();
    }
    return false;
}

}
