#include "erhe/raytrace/embree/embree_geometry.hpp"
#include "erhe/raytrace/embree/embree_scene.hpp"

namespace erhe::raytrace
{

auto IGeometry::create(IScene* scene) -> IGeometry*
{
    return new Embree_geometry(scene);
}

auto IGeometry::create_shared(IScene* scene) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Embree_geometry>(scene);
}

Embree_geometry::Embree_geometry(IScene* scene)
    : m_geometry{
        rtcNewGeometry(
            reinterpret_cast<Embree_scene*>(scene)->get_rtc_device(),
            RTC_GEOMETRY_TYPE_TRIANGLE
        )
    }
{
    //rtcSetGeometryVertexAttributeCount(m_geometry, 1);
    //rtcSetGeometryBuffer(m_geometry, 
    // RTC_GEOMETRY_TYPE_INSTANCE
}

Embree_geometry::~Embree_geometry()
{
    rtcReleaseGeometry(m_geometry);
}

auto Embree_geometry::get_rtc_geometry() -> RTCGeometry
{
    return m_geometry;
}

void Embree_geometry::set_transform(const glm::mat4 transform)
{
    const unsigned int time_step{0};
    rtcSetGeometryTransform(
        m_geometry,
        time_step,
        RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR,
        &transform[0][0]
    );
}

} // namespace erhe::raytrace
