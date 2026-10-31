#include "erhe_scene/lod_system.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/scene_host.hpp"

#include "erhe_item/hierarchy.hpp"

#include <memory>

namespace erhe::scene {

Lod_system::Lod_system(Scene& scene)
    : m_scene{scene}
{
}

Lod_system::~Lod_system() noexcept = default;

void Lod_system::refresh_mesh(Node& node)
{
    Mesh* const mesh = dynamic_cast<Mesh*>(&node);
    if (mesh == nullptr) {
        return;
    }
    const Lod_data resolved = resolve_lod(node);
    const std::unordered_map<Node*, Lod_data>::iterator i = m_records.find(&node);
    const Lod_data previous = (i != m_records.end()) ? i->second : Lod_data{};
    if (resolved == previous) {
        return;
    }
    if (resolved == Lod_data{}) {
        m_records.erase(i);
    } else if (i != m_records.end()) {
        i->second = resolved;
    } else {
        m_records.emplace(&node, resolved);
    }
    Scene_host* const host = dynamic_cast<Scene_host*>(m_scene.get_item_host());
    if (host != nullptr) {
        host->on_mesh_lod_changed(std::static_pointer_cast<Mesh>(mesh->shared_from_this()), resolved);
    }
}

namespace {

// Visits every Xformable strictly below `prim`, stepping through the prims
// that have no transform (a Scope), which the values inherit through too.
template <typename Visitor>
void for_each_node_below(erhe::Hierarchy& prim, Visitor&& visitor)
{
    for (const std::shared_ptr<erhe::Hierarchy>& child : prim.get_children()) {
        Node* const child_node = dynamic_cast<Node*>(child.get());
        if (child_node != nullptr) {
            visitor(*child_node);
        }
        for_each_node_below(*child, visitor);
    }
}

} // anonymous namespace

void Lod_system::refresh_subtree(Node& node)
{
    refresh_mesh(node);
    for_each_node_below(node, [this](Node& below) { refresh_mesh(below); });
}

void Lod_system::on_node_registered(Node& node)
{
    // A subtree enters the scene parent first, and each of its nodes is
    // registered on its own, so the node alone is resolved here; its
    // ancestors are already in the hierarchy it resolves against.
    refresh_mesh(node);
}

void Lod_system::on_node_unregistered(Node& node)
{
    // The host forgets the mesh's record when the mesh leaves the scene.
    m_records.erase(&node);
}

void Lod_system::on_values_changed(Node& node, const erhe::property::Dependency_property& property)
{
    if (property.get_owner_type() != Lod::property_owner_type()) {
        return;
    }
    if (&property == &Lod::apply_lod_property.get()) {
        // The key does not inherit, so a change of it reaches this node alone,
        // while it decides whether the values apply to every mesh below.
        refresh_subtree(node);
        return;
    }
    // The values inherit: a change on a group node reaches each node below
    // whose effective value moved through its own notification
    // (doc/erhe/property_system.md R8), so the node alone is resolved here.
    refresh_mesh(node);
}

void Lod_system::on_node_active_changed(Node&)
{
    // Activity does not change the effective values, and an inactive mesh
    // draws nothing at any level.
}

auto Lod_system::find(const Node& node) const -> const Lod_data*
{
    const std::unordered_map<Node*, Lod_data>::const_iterator i = m_records.find(const_cast<Node*>(&node));
    return (i != m_records.end()) ? &i->second : nullptr;
}

auto Lod_system::get_records() const -> const std::unordered_map<Node*, Lod_data>&
{
    return m_records;
}

} // namespace erhe::scene
