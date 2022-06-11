#include "erhe/raytrace/embree/embree_device.hpp"
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
    Embree_device* embree_device = reinterpret_cast<Embree_device*>(user_ptr);
    embree_device->rtc_error_function(code, str);
}

}

Embree_device::Embree_device()
{
    log_embree->trace("rtcNewDevice(nullptr)");
    m_device = rtcNewDevice("verbose=0");
    if (m_device == nullptr)
    {
        log_scene->error("rtcNewDevice() failed");
        check_device_error();
        return;
    }

    //log_embree.trace("rtcSetDeviceErrorFunction()");
    rtcSetDeviceErrorFunction(m_device, s_rtc_error_function, this);
}

Embree_device::~Embree_device()
{
    //log_embree.trace("rtcReleaseDevice()");
    rtcReleaseDevice(m_device);
}

void Embree_device::check_device_error()
{
    const auto error_code = rtcGetDeviceError(m_device);
    //log_embree.trace("rtcGetDeviceError() = {}\n", error_code);
    if (error_code != RTC_ERROR_NONE)
    {
        log_scene->error("error: {}", c_str(error_code));
    }
}

void Embree_device::rtc_error_function(
    RTCError    error_code,
    const char* message
)
{
    log_scene->error("error: {} - {}", c_str(error_code), message);
}

auto Embree_device::get_rtc_device() -> RTCDevice
{
    return m_device;
}

auto Embree_device::get_instance() -> Embree_device&
{
    static Embree_device singleton;
    return singleton;
}

}