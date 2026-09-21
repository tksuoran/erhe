#pragma once

namespace erhe::property {
    class Dependency_object;
    class Dependency_property;
    class Property_changed_args;
}

namespace erhe::scene {

class Xformable; using Node = Xformable;

// Per-scene owner of the runtime state a node value group implies
// (doc/erhe/scene.md "Node systems", D2 of
// doc/plans/node_attachments_to_properties.md): a physics body, a card proxy
// mesh, a layout solve registration. One system per group per scene, added to
// the scene with Scene::add_node_system. The system keeps its per-node record
// in a container keyed by Node*, holds no shared_ptr to a node, and erases the
// record in on_node_unregistered, so a scene close releases everything the
// system holds.
//
// A system is added and removed while no notification is being delivered: a
// callback may change values of the same scene, which reaches the systems
// again on the same thread, and the list is walked in place.
class INode_system
{
public:
    virtual ~INode_system() noexcept;

    // The node entered (left) the scene this system belongs to. The system
    // tests the group's key property and creates (erases) its record.
    virtual void on_node_registered  (Node& node) = 0;
    virtual void on_node_unregistered(Node& node) = 0;

    // One value of the group changed on a node of this scene, routed by
    // node_system_property_changed. A change of the key property creates or
    // destroys the record.
    virtual void on_values_changed(Node& node, const erhe::property::Dependency_property& property) = 0;

    // The derived erhe::Item_flags::active bit of the node changed: the node
    // and its subtree entered or left rendering, picking and simulation.
    virtual void on_node_active_changed(Node& node) = 0;
};

// The Property_metadata::property_changed callback every value of a node
// value group is registered with: finds the node's scene and calls
// on_values_changed on each of its systems. An object that is not a node (a
// Style holds every class's values, D30) and a node outside a scene reach no
// system.
void node_system_property_changed(
    erhe::property::Dependency_object&         object,
    const erhe::property::Property_changed_args& args
);

} // namespace erhe::scene
