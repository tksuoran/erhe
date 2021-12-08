#include "erhe/raytrace/embree/embree_scene.hpp"
#include "erhe/raytrace/embree/embree_device.hpp"
#include "erhe/raytrace/embree/embree_geometry.hpp"
#include "erhe/raytrace/embree/embree_instance.hpp"
#include "erhe/raytrace/log.hpp"

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
    const auto device = Embree_device::get_instance().get_rtc_device();
    if (device == nullptr)
    {
        m_scene = nullptr;
    }
    m_scene = rtcNewScene(device);
    log_embree.trace(
        "rtcNewScene() = {}\n",
        debug_label
    );
    if (m_scene == nullptr)
    {
        log_scene.error("rtcNewDevice() failed\n");
        Embree_device::get_instance().check_device_error();
        return;
    }
}

Embree_scene::~Embree_scene()
{
    log_embree.trace("rtcReleaseScene(hscene = {})\n", m_debug_label);
    rtcReleaseScene(m_scene);
}

namespace {

constexpr size_t s_grow = 256;

}

void Embree_scene::attach(IGeometry* geometry)
{
    auto* const embree_geometry = reinterpret_cast<Embree_geometry*>(geometry);
    const auto hgeometry        = embree_geometry->get_rtc_geometry();
    embree_geometry->geometry_id = rtcAttachGeometry(
        m_scene,
        hgeometry
    );
    //if (embree_geometry->geometry_id >= m_geometry_map.size())
    //{
    //    const auto old_size{embree_geometry->geometry_id};
    //    const auto new_size{old_size + s_grow};
    //    m_geometry_map.resize(new_size);
    //    for (size_t i = old_size; i < new_size - 1; ++i)
    //    {
    //        m_geometry_map[i] = nullptr;
    //    }
    //}
    //m_geometry_map[embree_geometry->geometry_id] = geometry;
    //log_embree.info(
    //    "rtcAttachGeometry(scene = {}, geometry = {}) id = {}\n",
    //    m_debug_label,
    //    embree_geometry->debug_label(),
    //    embree_geometry->geometry_id
    //);
}

void Embree_scene::attach(IInstance* instance)
{
    auto* const embree_instance = reinterpret_cast<Embree_instance*>(instance);
    const auto  hgeometry       = embree_instance->get_rtc_geometry();
    embree_instance->geometry_id = rtcAttachGeometry(
        m_scene,
        hgeometry
    );
    //if (embree_instance->geometry_id >= m_instance_map.size())
    //{
    //    const auto old_size{embree_instance->geometry_id};
    //    const auto new_size{old_size + s_grow};
    //    m_instance_map.resize(new_size);
    //    for (size_t i = old_size; i < new_size - 1; ++i)
    //    {
    //        m_instance_map[i] = nullptr;
    //    }
    //}
    //m_instance_map[embree_instance->geometry_id] = instance;
    //log_embree.info(
    //    "rtcAttachGeometry(scene = {}, instance = {}) id = {}\n",
    //    m_debug_label,
    //    embree_instance->debug_label(),
    //    embree_instance->geometry_id
    //);
}

void Embree_scene::detach(IGeometry* geometry)
{
    auto* const embree_geometry = reinterpret_cast<Embree_geometry*>(geometry);
    const auto  geometry_id     = embree_geometry->geometry_id;
    //if (geometry_id < m_geometry_map.size())
    //{
    //    m_geometry_map[geometry_id] = nullptr;
    //}
    //log_embree.trace(
    //    "rtcDetachGeometry(scene = {}, geometry = {}, id = {})\n",
    //    m_debug_label,
    //    embree_geometry->debug_label(),
    //    geometry_id
    //);
    rtcDetachGeometry(m_scene, geometry_id);
}

void Embree_scene::detach(IInstance* instance)
{
    auto* const embree_instance = reinterpret_cast<Embree_instance*>(instance);
    const auto  geometry_id     = embree_instance->geometry_id;
    //if (geometry_id < m_instance_map.size())
    //{
    //    m_instance_map[geometry_id] = nullptr;
    //}
    //log_embree.trace(
    //    "rtcDetachGeometry(scene = {}, geometry_id = {} {})\n",
    //    m_debug_label,
    //    embree_instance->debug_label(),
    //    geometry_id
    //);
    rtcDetachGeometry(m_scene, geometry_id);
}

void Embree_scene::commit()
{
    log_embree.trace("rtcCommitScene({})\n", m_debug_label);
    rtcCommitScene(m_scene);
}

void Embree_scene::intersect(Ray& ray, Hit& hit)
{
    RTCIntersectContext context;
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
            .id    = ray.id
        },
        .hit = {
            .Ng_x   = 0,
            .Ng_y   = 0,
            .Ng_z   = 0,
            .u      = 0,
            .v      = 0,
            .primID = 0,
            .geomID = RTC_INVALID_GEOMETRY_ID,
            .instID = {
                RTC_INVALID_GEOMETRY_ID
            }
        }
    };

    //log_embree.trace("rtcInitIntersectContext()\n");
    rtcInitIntersectContext(&context);
    //  log_embree.trace("rtcIntersect1({})\n", m_debug_label);
    rtcIntersect1(
        m_scene,
        &context,
        &ray_hit
    ); 
    ray.t_near       = ray_hit.ray.tnear;
    ray.t_far        = ray_hit.ray.tfar;
    hit.normal       = glm::vec3{ray_hit.hit.Ng_x, ray_hit.hit.Ng_y, ray_hit.hit.Ng_z};
    hit.uv           = glm::vec2{ray_hit.hit.u, ray_hit.hit.v};
    hit.primitive_id = ray_hit.hit.primID;
    hit.geometry     = nullptr;
    hit.instance     = nullptr;

    if (ray_hit.hit.instID[0] != RTC_INVALID_GEOMETRY_ID)
    {
        const auto instance_geometry = rtcGetGeometry(m_scene, ray_hit.hit.instID[0]);
        if (instance_geometry != nullptr)
        {
            void* user_data       = rtcGetGeometryUserData(instance_geometry);
            auto* embree_instance = reinterpret_cast<Embree_instance*>(user_data);
            hit.instance = embree_instance;
            if (embree_instance != nullptr)
            {
                auto* embree_instance_scene = embree_instance->get_embree_scene();
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

    auto rtc_geometry = rtcGetGeometry(m_scene, id);
    if (rtc_geometry != nullptr)
    {
        void* user_data       = rtcGetGeometryUserData(rtc_geometry);
        auto* embree_geometry = reinterpret_cast<Embree_geometry*>(user_data);
        return embree_geometry;
    }
    return nullptr;
}

auto Embree_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

}