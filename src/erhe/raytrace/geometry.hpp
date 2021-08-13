#pragma once

#include "erhe/primitive/primitive.hpp"
#include "erhe/scene/node.hpp"

#include <embree3/rtcore.h>

namespace erhe::raytrace
{

class Device;

class Geometry
    : public erhe::scene::INode_attachment
{
public:
    explicit Geometry(Device& device);
    Geometry(const Geometry&)            = delete;
    Geometry(Geometry&&)                 = delete;
    Geometry& operator=(const Geometry&) = delete;
    Geometry& operator=(Geometry&&)      = delete;
    ~Geometry();

    //void enable();
    //void disable();
    //void set_mask(unsigned int mask);
    //void intersection_filter(const RTCFilterFunctionNArguments* args);

    auto get_rtc_geometry() const -> RTCGeometry;
    void set_id          (const unsigned int id);
    auto get_id          () -> unsigned int;
    void reset_id        ();

private:
    RTCGeometry  m_geometry{nullptr};
    unsigned int m_id      {RTC_INVALID_GEOMETRY_ID};
};

} // namespace erhe::raytrace
