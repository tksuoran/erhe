#include "erhe/raytrace/embree/embree_scene.hpp"
#include "erhe/raytrace/embree/embree_geometry.hpp"
#include "erhe/raytrace/log.hpp"

namespace erhe::raytrace
{

namespace {

auto c_str(::RTCError error_code) -> const char*
{
    switch (error_code)
    {
        case RTC_ERROR_NONE             : return "RTC_ERROR_NONE";
        case RTC_ERROR_UNKNOWN          : return "RTC_ERROR_UNKNOWN";
        case RTC_ERROR_INVALID_ARGUMENT : return "RTC_ERROR_INVALID_ARGUMENT";
        case RTC_ERROR_INVALID_OPERATION: return "RTC_ERROR_INVALID_OPERATION";
        case RTC_ERROR_OUT_OF_MEMORY    : return "RTC_ERROR_OUT_OF_MEMORY";
        case RTC_ERROR_UNSUPPORTED_CPU  : return "RTC_ERROR_UNSUPPORTED_CPU";
        case RTC_ERROR_CANCELLED        : return "RTC_ERROR_CANCELLED";
        default                         : return "?";
    }
}

void s_rtc_error_function(
    void*       user_ptr,
    RTCError    code,
    const char* str
)
{
    Embree_scene* embree_scene = reinterpret_cast<Embree_scene*>(user_ptr);
    embree_scene->rtc_error_function(code, str);
}

}

auto IScene::create() -> IScene*
{
    return new Embree_scene();
}

auto IScene::create_shared() -> std::shared_ptr<IScene>
{
    return std::make_shared<Embree_scene>();
}

auto IScene::create_unique() -> std::unique_ptr<IScene>
{
    return std::make_unique<Embree_scene>();
}

Embree_scene::Embree_scene()
    : m_device{rtcNewDevice(nullptr)}
    , m_scene {
        (m_device != nullptr)
            ? rtcNewScene(m_device)
            : nullptr
    }
{
    if (m_device == nullptr)
    {
        log_scene.error("rtcNewDevice() failed\n");
        check_device_error();
        return;
    }

    rtcSetDeviceErrorFunction(m_device, s_rtc_error_function, this);
}

Embree_scene::~Embree_scene()
{
    rtcReleaseDevice(m_device);
}

void Embree_scene::check_device_error()
{
    const auto error_code = rtcGetDeviceError(m_device);
    if (error_code != RTC_ERROR_NONE)
    {
        log_scene.error("error: {}\n", c_str(error_code));
    }
}

void Embree_scene::rtc_error_function(
    RTCError    error_code,
    const char* message
)
{
    log_scene.error("error: {} - {}\n", c_str(error_code), message);
}

void Embree_scene::attach(IGeometry* geometry)
{
    auto* embree_geometry = reinterpret_cast<Embree_geometry*>(geometry);
    embree_geometry->geometry_id = rtcAttachGeometry(
        m_scene,
        embree_geometry->get_rtc_geometry()
    );
}

void Embree_scene::detach(IGeometry* geometry)
{
    auto* embree_geometry = reinterpret_cast<Embree_geometry*>(geometry);
    rtcDetachGeometry(m_scene, embree_geometry->geometry_id);
}

auto Embree_scene::get_rtc_device() -> RTCDevice
{
    return m_device;
}

auto Embree_scene::get_rtc_scene() -> RTCScene
{
    return m_scene;
}

}