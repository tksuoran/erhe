#include "erhe/raytrace/device.hpp"
#include "erhe/toolkit/verify.hpp"

#include <embree3/rtcore.h>

namespace erhe::raytrace
{

class Device
    : public IDevice
{
public:
    Device();
    Device(const Device&)            = delete;
    Device(Device&&)                 = delete;
    Device& operator=(const Device&) = delete;
    Device& operator=(Device&&)      = delete;
    ~Device();

    auto get_error() const -> RTCError;

    auto get_rtc_device() const -> RTCDevice;

    void error_callback(RTCError code, const char* str);

private:
    RTCDevice m_device{nullptr};
};


auto IDevice::create() -> IDevice*
{
    return new Device();
}

auto IDevice::create_shared() -> std::shared_ptr<IDevice>
{
    return std::make_shared<Device>();
}

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
