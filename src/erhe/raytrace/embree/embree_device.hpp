#pragma once

#include "erhe/raytrace/iscene.hpp"

#include <embree3/rtcore.h>

#include <string>

namespace erhe::raytrace
{

class IGeometry;

class Embree_device
{
private:
    Embree_device();  // rtcNewDevice

public:
    ~Embree_device(); // rtcReleaseDevice

    // rtcRetainDevice()

    // rtcGetDeviceProperty()
    // rtcGetDeviceError()
    // rtcSetDeviceErrorFunction()
    // rtcSetDeviceMemoryMonitorFunction()

    void check_device_error();

    void rtc_error_function(
        RTCError    error_code,
        const char* message
    );

    [[nodiscard]] auto get_rtc_device() -> RTCDevice;

    static [[nodiscard]] auto get_instance() -> Embree_device&;

private:
    RTCDevice m_device{nullptr};
};

}