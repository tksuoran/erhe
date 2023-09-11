#pragma once

#include "erhe_item/item_host.hpp"

#include <memory>

namespace erhe::scene
{

class Camera;
class Light;
class Mesh;
class Mesh_raytrace;
class Node;
class Scene;
class Skin;

class Scene_host
    : public erhe::Item_host
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
};

} // namespace erhe::scene
