#pragma once

#include "erhe_scene/node_system.hpp"

namespace editor {

class App_message_bus;

// The per-scene node system of the bone values (doc/erhe/scene.md "Node
// systems"; doc/plans/rigging/skeleton_editing.md R1, R3): reports every
// change that can alter a bone's display to the bone visualization as a
// queued Bone_changed_message - a node carrying Item_flags::bone entering or
// leaving the scene, a change of the bone flag (Node::bone_property, which
// Xformable::handle_flag_bits_update routes here for every writer of the
// bit) and a change of Rig.tail. It keeps no per-node record: the
// visualization reconciles each reported node against its current state.
class Rig_system : public erhe::scene::INode_system
{
public:
    explicit Rig_system(App_message_bus* app_message_bus);
    ~Rig_system() noexcept override;

    // Implements INode_system
    void on_node_registered    (erhe::scene::Node& node) override;
    void on_node_unregistered  (erhe::scene::Node& node) override;
    void on_values_changed     (erhe::scene::Node& node, const erhe::property::Dependency_property& property) override;
    void on_node_active_changed(erhe::scene::Node& node) override;

private:
    void report(erhe::scene::Node& node);

    App_message_bus* m_app_message_bus{nullptr};
};

} // namespace editor
