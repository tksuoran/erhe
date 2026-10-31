#pragma once

#include "erhe_scene/lod.hpp"
#include "erhe_scene/node_system.hpp"

#include <unordered_map>

namespace erhe::scene {

class Scene;
class Xformable; using Node = Xformable;

// The per-scene owner of the `Lod` value group's runtime state
// (doc/erhe/scene.md "Node systems" and "Level of detail control"): the
// effective Lod_data of every mesh of the scene that resolves to a record
// other than the default one, mirrored into the scene host's draw-list objects
// through Scene_host::on_mesh_lod_changed. One of these is owned by each
// `Scene` and added to it, which drives it from the three node-system change
// sites; the host hears a mesh only when its effective record changes, so
// nothing runs per frame and the draw path never walks the hierarchy.
//
// The record is keyed by a raw `Node*` and holds no `shared_ptr`, and
// `on_node_unregistered` erases it, so a scene close releases everything the
// system holds. A host forgets a mesh's record when the mesh leaves the scene,
// and the mesh's re-entry reaches the host again through on_node_registered.
class Lod_system : public INode_system
{
public:
    explicit Lod_system(Scene& scene);
    ~Lod_system() noexcept override;

    // Implements INode_system
    void on_node_registered    (Node& node) override;
    void on_node_unregistered  (Node& node) override;
    void on_values_changed     (Node& node, const erhe::property::Dependency_property& property) override;
    void on_node_active_changed(Node& node) override;

    // The effective record of a mesh node of this scene, or null when it
    // resolves to the default record.
    [[nodiscard]] auto find(const Node& node) const -> const Lod_data*;
    [[nodiscard]] auto get_records() const -> const std::unordered_map<Node*, Lod_data>&;

private:
    // Resolves the record of `node` when it is a mesh and tells the host when
    // it changed.
    void refresh_mesh   (Node& node);
    // refresh_mesh() for `node` and every node below it.
    void refresh_subtree(Node& node);

    Scene&                              m_scene;
    std::unordered_map<Node*, Lod_data> m_records;
};

} // namespace erhe::scene
