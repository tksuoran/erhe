#include "erhe/raytrace/device.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::raytrace
{

void error_function(void* userPtr, RTCError code, const char* str)
{
    auto* device = reinterpret_cast<Device*>(userPtr);
    if (device == nullptr)
    {
        return;
    }
    device->error_callback(code, str);
}

Device::Device()
{
    const char* default_config = nullptr;
    m_device = rtcNewDevice(default_config);

    rtcSetDeviceErrorFunction(m_device, error_function, this);
    //rtcSetDeviceMemoryMonitorFunction()
}

Device::~Device()
{
    rtcReleaseDevice(m_device);
    m_device = nullptr;
}

auto Device::get_rtc_device() const -> RTCDevice
{
    return m_device;
}

auto Device::get_error() const -> RTCError
{
    VERIFY(m_device != nullptr);
    return rtcGetDeviceError(m_device);
}

void Device::error_callback(RTCError code, const char* str)
{
}

}
