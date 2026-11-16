#include "erhe_raytrace/embree/embree_scene.hpp"
#include "erhe_raytrace/embree/embree_device.hpp"
#include "erhe_raytrace/embree/embree_geometry.hpp"
#include "erhe_raytrace/embree/embree_instance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/normal_transform.hpp"
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
    // Ray::filter is an argument filter; Embree invokes those only in scenes with this flag.
    rtcSetSceneFlags(m_scene, RTC_SCENE_FLAG_FILTER_FUNCTION_IN_ARGUMENTS);
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

namespace {

// Standard layout with the Embree context first: the filter callback casts the context back.
class Embree_filter_context
{
public:
    RTCRayQueryContext rtc_context;
    Embree_scene*      scene {nullptr};
    const IHit_filter* filter{nullptr};
};

void s_filter_function(const RTCFilterFunctionNArguments* args)
{
    const Embree_filter_context* const context = reinterpret_cast<const Embree_filter_context*>(args->context);
    for (unsigned int i = 0; i < args->N; ++i) {
        if (args->valid[i] == 0) {
            continue;
        }
        const RTCHit rtc_hit{
            .Ng_x   = RTCHitN_Ng_x  (args->hit, args->N, i),
            .Ng_y   = RTCHitN_Ng_y  (args->hit, args->N, i),
            .Ng_z   = RTCHitN_Ng_z  (args->hit, args->N, i),
            .u      = RTCHitN_u     (args->hit, args->N, i),
            .v      = RTCHitN_v     (args->hit, args->N, i),
            .primID = RTCHitN_primID(args->hit, args->N, i),
            .geomID = RTCHitN_geomID(args->hit, args->N, i),
            .instID = {
                RTCHitN_instID(args->hit, args->N, i, 0)
            }
        };
        if (!context->filter->accept(context->scene->make_hit(rtc_hit, RTCRayN_tfar(args->ray, args->N, i)))) {
            args->valid[i] = 0;
        }
    }
}

} // anonymous namespace

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
    if (ray.filter != nullptr) {
        Embree_filter_context context{
            .rtc_context = {},
            .scene       = this,
            .filter      = ray.filter
        };
        rtcInitRayQueryContext(&context.rtc_context);
        RTCIntersectArguments arguments;
        rtcInitIntersectArguments(&arguments);
        arguments.flags   = static_cast<RTCRayQueryFlags>(arguments.flags | RTC_RAY_QUERY_FLAG_INVOKE_ARGUMENT_FILTER);
        arguments.context = &context.rtc_context;
        arguments.filter  = s_filter_function;
        rtcIntersect1(m_scene, &ray_hit, &arguments);
    } else {
        rtcIntersect1(m_scene, &ray_hit, nullptr);
    }

    if (ray_hit.hit.geomID == RTC_INVALID_GEOMETRY_ID)
    {
        return false;
    }

    ray.t_near = ray_hit.ray.tnear;
    ray.t_far  = ray_hit.ray.tfar;
    hit        = make_hit(ray_hit.hit, ray_hit.ray.tfar);
    return true;
}

auto Embree_scene::make_hit(const RTCHit& rtc_hit, const float t) -> Hit
{
    Hit hit{
        .normal      = glm::vec3{rtc_hit.Ng_x, rtc_hit.Ng_y, rtc_hit.Ng_z},
        .uv          = glm::vec2{rtc_hit.u, rtc_hit.v},
        .triangle_id = rtc_hit.primID,
        .geometry    = nullptr,
        .instance    = nullptr,
        .t           = t
    };

    if (rtc_hit.instID[0] != RTC_INVALID_GEOMETRY_ID)
    {
        const RTCGeometry instance_geometry = rtcGetGeometry(m_scene, rtc_hit.instID[0]);
        if (instance_geometry != nullptr)
        {
            void* user_data                    = rtcGetGeometryUserData(instance_geometry);
            Embree_instance* embree_instance    = static_cast<Embree_instance*>(user_data);
            hit.instance = embree_instance;
            if (embree_instance != nullptr)
            {
                // Embree reports Ng in the instanced geometry's object space; the other backends in the instance's parent space.
                hit.normal = glm::vec3{get_normal_transform(embree_instance->get_transform()) * glm::vec4{hit.normal, 0.0f}};
                Embree_scene* embree_instance_scene = embree_instance->get_embree_scene();
                if (embree_instance_scene != nullptr)
                {
                    hit.geometry = embree_instance_scene->get_geometry_from_id(rtc_hit.geomID);
                }
            }
        }
    }
    else
    {
        hit.geometry = (rtc_hit.geomID != RTC_INVALID_GEOMETRY_ID)
            ? get_geometry_from_id(rtc_hit.geomID)
            : nullptr;
    }
    return hit;
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
