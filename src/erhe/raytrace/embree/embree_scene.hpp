#pragma once

#include "erhe/raytrace/iscene.hpp"

#include <embree3/rtcore.h>

namespace erhe::raytrace
{

class IGeometry;

class Embree_scene
    : public IScene
{
public:
    Embree_scene();
    ~Embree_scene() override;

    // Implements IScene
    void attach(IGeometry* geometry) override;
    void detach(IGeometry* geometry) override;

    void check_device_error();
    void rtc_error_function(
        RTCError    error_code,
        const char* message
    );

    auto get_rtc_device() -> RTCDevice;
    auto get_rtc_scene () -> RTCScene;

private:
    RTCDevice m_device{nullptr};
    RTCScene  m_scene {nullptr};
};

}