#include "erhe_raytrace/embree/embree_scene.hpp"
#include "erhe_raytrace/embree/embree_device.hpp"
#include "erhe_raytrace/embree/embree_geometry.hpp"
#include "erhe_raytrace/embree/embree_instance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_profile/profile.hpp"

namespace erhe::raytrace
{

auto IScene::create(const std::string_view debug_label) -> IScene*
{
    return new Embree_scene(debug_label);
}

auto IScene::create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>
{
    return std::make_shared<Embree_scene>(debug_label);
}

auto IScene::create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>
{
    return std::make_unique<Embree_scene>(debug_label);
}

Embree_scene::Embree_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
    const RTCDevice device = Embree_device::get_instance().get_rtc_device();
    if (device == nullptr)
    {
        m_scene = nullptr;
        return;
    }
    m_scene = rtcNewScene(device);
    SPDLOG_LOGGER_TRACE(log_embree, "rtcNewScene() = {}", debug_label);
    if (m_scene == nullptr)
    {
        log_scene->error("rtcNewScene() failed");
        Embree_device::get_instance().check_device_error();
        return;
    }
    rtcSetSceneBuildQuality(m_scene, RTC_BUILD_QUALITY_LOW);
}

Embree_scene::~Embree_scene() noexcept
{
    SPDLOG_LOGGER_TRACE(log_embree, "rtcReleaseScene({})", m_debug_label);
    rtcReleaseScene(m_scene);
}

void Embree_scene::attach(IGeometry* geometry)
{
    ERHE_PROFILE_FUNCTION();

    Embree_geometry* const embree_geometry = static_cast<Embree_geometry*>(geometry);
    const RTCGeometry hgeometry            = embree_geometry->get_rtc_geometry();
    embree_geometry->geometry_id = rtcAttachGeometry(
        m_scene,
        hgeometry
    );

    SPDLOG_LOGGER_TRACE(
        log_embree,
        "rtcAttachGeometry(scene = {}, geometry = {}) id = {}",
        m_debug_label,
        embree_geometry->debug_label(),
        embree_geometry->geometry_id
    );
}

void Embree_scene::attach(IInstance* instance)
{
    ERHE_PROFILE_FUNCTION();

    Embree_instance* const embree_instance = static_cast<Embree_instance*>(instance);
    const RTCGeometry hgeometry            = embree_instance->get_rtc_geometry();
    embree_instance->geometry_id = rtcAttachGeometry(
        m_scene,
        hgeometry
    );

    SPDLOG_LOGGER_TRACE(
        log_embree,
        "rtcAttachGeometry(scene = {}, instance = {}) id = {}",
        m_debug_label,
        embree_instance->debug_label(),
        embree_instance->geometry_id
    );
}

void Embree_scene::detach(IGeometry* geometry)
{
    Embree_geometry* const embree_geometry = static_cast<Embree_geometry*>(geometry);
    const unsigned int geometry_id         = embree_geometry->geometry_id;

    SPDLOG_LOGGER_TRACE(
        log_embree,
        "rtcDetachGeometry(scene = {}, geometry = {}, id = {})",
        m_debug_label,
        embree_geometry->debug_label(),
        geometry_id
    );
    rtcDetachGeometry(m_scene, geometry_id);
}

void Embree_scene::detach(IInstance* instance)
{
    ERHE_PROFILE_FUNCTION();

    Embree_instance* const embree_instance = static_cast<Embree_instance*>(instance);
    const unsigned int geometry_id         = embree_instance->geometry_id;

    SPDLOG_LOGGER_TRACE(
        log_embree,
        "rtcDetachGeometry(scene = {}, instance = {} id = {})",
        m_debug_label,
        embree_instance->debug_label(),
        geometry_id
    );
    rtcDetachGeometry(m_scene, geometry_id);
}

void Embree_scene::commit()
{
    ERHE_PROFILE_FUNCTION();

    SPDLOG_LOGGER_TRACE(log_embree, "rtcCommitScene({})\n", m_debug_label);
    rtcCommitScene(m_scene);
}

auto Embree_scene::intersect(Ray& ray, Hit& hit) -> bool
{
    ERHE_PROFILE_FUNCTION();

    RTCRayHit ray_hit{
        .ray = {
            .org_x = ray.origin.x,
            .org_y = ray.origin.y,
            .org_z = ray.origin.z,
            .tnear = ray.t_near,
            .dir_x = ray.direction.x,
            .dir_y = ray.direction.y,
            .dir_z = ray.direction.z,
            .time  = ray.time,
            .tfar  = ray.t_far,
            .mask  = ray.mask,
            .id    = ray.id,
            .flags = 0
        },
        .hit = {
            .Ng_x   = 0.0f,
            .Ng_y   = 0.0f,
            .Ng_z   = 0.0f,
            .u      = 0.0f,
            .v      = 0.0f,
            .primID = RTC_INVALID_GEOMETRY_ID,
            .geomID = RTC_INVALID_GEOMETRY_ID,
            .instID = {
                RTC_INVALID_GEOMETRY_ID
            }
        }
    };

    SPDLOG_LOGGER_TRACE(log_embree, "rtcIntersect1({})", m_debug_label);
    rtcIntersect1(m_scene, &ray_hit, nullptr);

    if (ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
    {
        return false;
    }

    ray.t_near       = ray_hit.ray.tnear;
    ray.t_far        = ray_hit.ray.tfar;
    hit.normal       = glm::vec3{ray_hit.hit.Ng_x, ray_hit.hit.Ng_y, ray_hit.hit.Ng_z};
    hit.uv           = glm::vec2{ray_hit.hit.u, ray_hit.hit.v};
    hit.triangle_id  = ray_hit.hit.primID;
    hit.geometry     = nullptr;
    hit.instance     = nullptr;

    if (ray_hit.hit.instID[0] != RTC_INVALID_GEOMETRY_ID)
    {
        const RTCGeometry instance_geometry = rtcGetGeometry(m_scene, ray_hit.hit.instID[0]);
        if (instance_geometry != nullptr)
        {
            void* user_data                    = rtcGetGeometryUserData(instance_geometry);
            Embree_instance* embree_instance    = static_cast<Embree_instance*>(user_data);
            hit.instance = embree_instance;
            if (embree_instance != nullptr)
            {
                Embree_scene* embree_instance_scene = embree_instance->get_embree_scene();
                if (embree_instance_scene != nullptr)
                {
                    hit.geometry = embree_instance_scene->get_geometry_from_id(ray_hit.hit.geomID);
                }
            }
        }
    }
    else
    {
        hit.geometry = (ray_hit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
            ? get_geometry_from_id(ray_hit.hit.geomID)
            : nullptr;
    }

    return true;
}

auto Embree_scene::get_rtc_scene() -> RTCScene
{
    return m_scene;
}

auto Embree_scene::get_geometry_from_id(const unsigned int id) -> Embree_geometry*
{
    if (id == RTC_INVALID_GEOMETRY_ID)
    {
        return nullptr;
    }

    RTCGeometry rtc_geometry = rtcGetGeometry(m_scene, id);
    if (rtc_geometry != nullptr)
    {
        void* user_data                    = rtcGetGeometryUserData(rtc_geometry);
        Embree_geometry* embree_geometry   = static_cast<Embree_geometry*>(user_data);
        return embree_geometry;
    }
    return nullptr;
}

auto Embree_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

}
