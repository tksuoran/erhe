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

class IScene
    : public erhe::scene::INode_attachment
{
public:
    virtual void attach          (IScene& scene) = 0;
    virtual void attach          (IGeometry& geometry) = 0;
    virtual void detach          (IGeometry& geometry_id) = 0;
    virtual void commit          ();
    virtual auto intersect       (const glm::vec3 origin, const glm::vec3 direction) -> RTCRayHit;
    virtual auto get_hit_position(const RTCRayHit& ray_hit) -> std::optional<glm::vec3>;
    virtual auto get_hit_normal  (const RTCRayHit& ray_hit) -> std::optional<glm::vec3>;

private:
    RTCScene m_scene{nullptr};    
};

} // namespace erhe::raytrace
