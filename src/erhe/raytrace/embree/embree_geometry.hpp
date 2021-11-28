#pragma once

#include "erhe/raytrace/igeometry.hpp"

#include <embree3/rtcore.h>

namespace erhe::raytrace
{

class Embree_geometry
    : public IGeometry
{
public:
    Embree_geometry(IScene* scene);
    ~Embree_geometry() override;

    // Implements IGeometry
    void set_transform(const glm::mat4 transform) override;

    auto get_rtc_geometry() -> RTCGeometry;

    unsigned int geometry_id;

private:
    RTCGeometry  m_geometry{nullptr};
};

}