#pragma once

#include "erhe_item/item_host.hpp"

#include <memory>

namespace erhe::scene {

class Camera;
class Light;
class Mesh;
class Mesh_raytrace;
class Node;
class Scene;
class Skin;

class Scene_host : public erhe::Item_host
{
public:
    ~Scene_host() noexcept override;

    [[nodiscard]] virtual auto get_hosted_scene() -> Scene* = 0;
    virtual void register_node    (const std::shared_ptr<Node>&   node)   = 0;
    virtual void unregister_node  (const std::shared_ptr<Node>&   node)   = 0;
    virtual void register_camera  (const std::shared_ptr<Camera>& camera) = 0;
    virtual void unregister_camera(const std::shared_ptr<Camera>& camera) = 0;
    virtual void register_mesh    (const std::shared_ptr<Mesh>&   mesh)   = 0;
    virtual void unregister_mesh  (const std::shared_ptr<Mesh>&   mesh)   = 0;
    virtual void register_skin    (const std::shared_ptr<Skin>&   skin)   = 0;
    virtual void unregister_skin  (const std::shared_ptr<Skin>&   skin)   = 0;
    virtual void register_light   (const std::shared_ptr<Light>&  light)  = 0;
    virtual void unregister_light (const std::shared_ptr<Light>&  light)  = 0;

    // Mesh change notifications (doc/draw_list_renderer_requirements.md R0a,
    // R12, R12a). May be called from worker threads (deferred mesh finalize
    // runs Mesh::update_rt_primitives() on a tf::Executor worker), so
    // implementations must only enqueue and apply the change later on the
    // main thread.
    //
    // on_mesh_primitives_changed: the primitive list changed, or a live
    // primitive's renderable Buffer_mesh was replaced in place
    // (Mesh::update_rt_primitives / Mesh::clear_primitives).
    // on_mesh_material_changed: a primitive's material was reassigned
    // (Mesh::set_primitive_material).
    // on_mesh_flags_changed: the mesh Item_flags word changed.
    virtual void on_mesh_primitives_changed(const std::shared_ptr<Mesh>& mesh) = 0;
    virtual void on_mesh_material_changed  (const std::shared_ptr<Mesh>& mesh) = 0;
    virtual void on_mesh_flags_changed     (const std::shared_ptr<Mesh>& mesh, uint64_t old_flag_bits, uint64_t new_flag_bits) = 0;
};

} // namespace erhe::scene
