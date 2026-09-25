#include "rig/rig_system.hpp"

#include "app_message_bus.hpp"
#include "scene/rig_properties.hpp"

#include "erhe_scene/node.hpp"
#include "erhe_scene/skin.hpp"

namespace editor {

Rig_system::Rig_system(App_message_bus* app_message_bus)
    : m_app_message_bus{app_message_bus}
{
}

Rig_system::~Rig_system() noexcept = default;

void Rig_system::report(erhe::scene::Node& node)
{
    if (m_app_message_bus == nullptr) {
        return;
    }
    const std::shared_ptr<erhe::scene::Node> parent = node.get_parent_node();
    m_app_message_bus->bone_changed.queue_message(
        Bone_changed_message{
            .node   = node.weak_from_this(),
            .parent = parent ? parent->weak_from_this() : std::weak_ptr<erhe::Item_base>{}
        }
    );
}

void Rig_system::on_node_registered(erhe::scene::Node& node)
{
    if (erhe::scene::is_bone(&node)) {
        report(node);
    }
}

void Rig_system::on_node_unregistered(erhe::scene::Node& node)
{
    if (erhe::scene::is_bone(&node)) {
        report(node);
    }
}

void Rig_system::on_values_changed(erhe::scene::Node& node, const erhe::property::Dependency_property& property)
{
    if ((&property == &erhe::scene::Node::bone_property.get()) || (&property == &Rig::tail_property().get())) {
        report(node);
    }
}

void Rig_system::on_node_active_changed(erhe::scene::Node&)
{
    // The proxies are children of their bone and follow its active bit.
}

} // namespace editor
