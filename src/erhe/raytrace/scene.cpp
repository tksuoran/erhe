#include "erhe/raytrace/scene.hpp"
#include "erhe/raytrace/device.hpp"
#include "erhe/raytrace/geometry.hpp"

namespace erhe::raytrace
{

Scene::Scene(const Device& device)
{
    const char* default_config = nullptr;
    m_scene = rtcNewScene(device.get_rtc_device());
    //rtcSetDeviceMemoryMonitorFunction()
}

Scene::~Scene()
{
    rtcReleaseScene(m_scene);
    m_scene = nullptr;
}

void Scene::attach(Geometry& geometry)
{
    auto geometry_id = rtcAttachGeometry(m_scene, geometry.get_rtc_geometry());
    geometry.set_id(geometry_id);
}

void Scene::detach(Geometry& geometry)
{
    auto geometry_id = geometry.get_id();
    if (geometry_id != RTC_INVALID_GEOMETRY_ID)
    {
        rtcDetachGeometry(m_scene, geometry_id);
        geometry.reset_id();
    }
}

void Scene::commit()
{
    //rtcSetSceneFlags()
    //rtcSetSceneBuildQuality()
    rtcCommitScene(m_scene);

    //rtcGetSceneBounds()
    //rtcGetSceneLinearBounds()
}

// Avoid store-to-load forwarding issues with single rays

// We recommend to use a single SSE store to set up the
// org and tnear components, and a single SSE store to
// set up the dir and time components of a single ray (RTCRay type).
//
// Storing these values using scalar stores causes a store-to-load
// forwarding penalty because Embree is reading these components
// using SSE loads later on.
auto Scene::intersect(const glm::vec3 origin, const glm::vec3 direction) -> RTCRayHit
{
    RTCIntersectContext context;
    rtcInitIntersectContext(&context);
    RTCRayHit ray_hit;
    ray_hit.ray.org_x = origin.x;
    ray_hit.ray.org_y = origin.y;
    ray_hit.ray.org_z = origin.z;
    ray_hit.ray.tnear = 0.0f;
    ray_hit.ray.dir_x = direction.x;
    ray_hit.ray.dir_y = direction.y;
    ray_hit.ray.dir_z = direction.z;
    ray_hit.ray.time  = 0.0f;
    ray_hit.ray.tfar  = 4096.0f;
    ray_hit.ray.mask  = ~0u;
    ray_hit.ray.id    = 0u;
    ray_hit.ray.flags = 0u;
    ray_hit.hit.geomID    = RTC_INVALID_GEOMETRY_ID;
    ray_hit.hit.primID    = RTC_INVALID_GEOMETRY_ID;
    ray_hit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
    rtcIntersect1(m_scene, &context, &ray_hit);
    return ray_hit;
}

auto Scene::get_hit_position(const RTCRayHit& ray_hit) -> std::optional<glm::vec3>
{
    if (ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
    {
        return {};
    }
    //auto geometry = rtcGetGeometry(m_scene, ray_hit.hit.geomID);
    //rtcInterpolate0(geometry, ray_hit.hit.primID, ray_hit.hit.u, ray_hit.hit.v, RTC_BUFFER_TYPE_VERTEX,
    return {};

}

auto Scene::get_hit_normal(const RTCRayHit& ray_hit) -> std::optional<glm::vec3>
{
    if (ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
    {
        return {};
    }
    //rtcIntersect4()
    //rtcIntersect8()
    //rtcIntersect16()
    //rtcIntersect1M()
    //rtcIntersect1Mp()
    //rtcIntersectNM()
    //rtcIntersectNp()
    return {};
}


} // namespace
