#pragma once

#include "erhe/scene/node.hpp"

#include <embree3/rtcore.h>

#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace erhe::raytrace
{

class Device;
class Geometry;

class Scene
    : public erhe::scene::INode_attachment
{
public:
    explicit Scene(const Device& device);
    Scene(const Scene&)            = delete;
    Scene(Scene&&)                 = delete;
    Scene& operator=(const Scene&) = delete;
    Scene& operator=(Scene&&)      = delete;
    ~Scene();

    void attach   (Scene& scene);
    void attach   (Geometry& geometry);
    void detach   (Geometry& geometry_id);
    void commit   ();
    auto intersect(const glm::vec3 origin, const glm::vec3 direction) -> RTCRayHit;

    auto get_hit_position(const RTCRayHit& ray_hit) -> std::optional<glm::vec3>;
    auto get_hit_normal  (const RTCRayHit& ray_hit) -> std::optional<glm::vec3>;

private:
    RTCScene m_scene{nullptr};
    
};

} // namespace erhe::raytrace
