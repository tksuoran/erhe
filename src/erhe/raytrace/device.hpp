#pragma once

#include <embree3/rtcore.h>

namespace erhe::raytrace
{

class Device
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

} // namespace erhe::raytrace
