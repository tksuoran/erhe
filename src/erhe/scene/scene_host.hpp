#pragma once

#include <memory>

namespace erhe::scene
{

class Camera;
class Light;
class Mesh;
class Node;
class Scene;

class Scene_host
{
public:
    virtual ~Scene_host() noexcept;

    [[nodiscard]] virtual auto get_hosted_scene() -> Scene* = 0;
    virtual void register_node    (const std::shared_ptr<Node>&   node)   = 0;
    virtual void unregister_node  (const std::shared_ptr<Node>&   node)   = 0;
    virtual void register_camera  (const std::shared_ptr<Camera>& camera) = 0;
    virtual void unregister_camera(const std::shared_ptr<Camera>& camera) = 0;
    virtual void register_mesh    (const std::shared_ptr<Mesh>&   mesh)   = 0;
    virtual void unregister_mesh  (const std::shared_ptr<Mesh>&   mesh)   = 0;
    virtual void register_light   (const std::shared_ptr<Light>&  light)  = 0;
    virtual void unregister_light (const std::shared_ptr<Light>&  light)  = 0;
};

} // namespace erhe::scene
