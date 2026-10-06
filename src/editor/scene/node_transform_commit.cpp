#include "scene/node_transform_commit.hpp"

#include "app_message_bus.hpp"
#include "scene/scene_root.hpp"

#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

auto node_affects_indirect_lighting(const erhe::scene::Node& node) -> bool
{
    bool affects = false;
    node.for_each_const<erhe::scene::Mesh>(
        [&affects](const erhe::scene::Mesh& mesh) -> bool {
            if (mesh.layer_id == Mesh_layer_id::content) {
                affects = true;
                return false; // stop
            }
            return true;
        }
    );
    if (affects) {
        return true;
    }
    node.for_each_const<erhe::scene::Light>(
        [&affects](const erhe::scene::Light&) -> bool {
            affects = true;
            return false;
        }
    );
    return affects;
}

void announce_committed_node_transform(App_message_bus& app_message_bus, erhe::scene::Node& node)
{
    app_message_bus.node_touched.send_message(Node_touched_message{.node = &node});
    if (node_affects_indirect_lighting(node)) {
        app_message_bus.scene_lighting_changed.queue_message(Scene_lighting_changed_message{});
    }
}

} // namespace editor
