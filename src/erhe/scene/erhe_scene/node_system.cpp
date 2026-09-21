#include "erhe_scene/node_system.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include "erhe_property/property_metadata.hpp"

namespace erhe::scene {

INode_system::~INode_system() noexcept = default;

void node_system_property_changed(
    erhe::property::Dependency_object&           object,
    const erhe::property::Property_changed_args& args
)
{
    Node* const node = dynamic_cast<Node*>(&object);
    if (node == nullptr) {
        return;
    }
    Scene* const scene = node->get_scene();
    if (scene == nullptr) {
        return;
    }
    scene->on_node_values_changed(*node, args.property);
}

} // namespace erhe::scene
