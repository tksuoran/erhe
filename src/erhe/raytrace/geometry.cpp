#include "erhe/raytrace/geometry.hpp"
#include "erhe/raytrace/device.hpp"

#include <gsl/assert>

namespace erhe::raytrace
{

//struct RTCFilterFunctionNArguments
//{
//  int* valid;
//  void* geometryUserPtr;
//  const struct RTCIntersectContext* context;
//  struct RTCRayN* ray;
//  struct RTCHitN* hit;
//  unsigned int N;
//};

//void intersection_filter(const RTCFilterFunctionNArguments* args)
//{
//    if (args == nullptr)
//    {
//        return;
//    }
//    auto* geometry = reinterpret_cast<Geometry*>(args->geometryUserPtr);
//    if (geometry == nullptr)
//    {
//        return;
//    }
//    geometry->intersection_filter(args);
//}

Geometry::Geometry(Device& device)
{
    m_geometry = rtcNewGeometry(device.get_rtc_device(), RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryUserData(m_geometry, this);
    // rtcSetGeometryBuffer()
    // rtcSetSharedGeometryBuffer()
    // rtcSetGeometryVertexAttributeCount()
    // rtcSetGeometryBuildQuality()
    // rtcCommitGeometry()
    // rtcSetGeometryIntersectFilterFunction()
    // rtcSetGeometryOccludedFilterFunction()
    // rtcSetGeometryInstancedScene()
    // rtcSetGeometryTransform()
    // rtcSetGeometryTransformQuaternion()
    // rtcSetGeometryTessellationRate()
    // rtcSetGeometryTopologyCount()
    // rtcSetGeometrySubdivisionMode()
    // rtcSetGeometryVertexAttributeTopology()
    // rtcSetGeometryDisplacementFunction()
    // rtcGetGeometryFirstHalfEdge()
    // rtcGetGeometryNextHalfEdge()
    // rtcGetGeometryPreviousHalfEdge()
    // rtcGetGeometryOppositeHalfEdge()
    // rtcInterpolate()
    // rtcInterpolateN()
    // rtcGetGeometryFace()
}

Geometry::~Geometry()
{
    rtcReleaseGeometry(m_geometry);
}

auto Geometry::get_rtc_geometry() const -> RTCGeometry
{
    return m_geometry;
}

void Geometry::set_id(const unsigned int id)
{
    Expects(id != RTC_INVALID_GEOMETRY_ID);
    Expects(m_id == RTC_INVALID_GEOMETRY_ID);
    m_id = id;
}

void Geometry::reset_id()
{
    m_id = RTC_INVALID_GEOMETRY_ID;
}

auto Geometry::get_id() -> unsigned int
{
    return m_id;
}

// void Geometry::intersection_filter(const RTCFilterFunctionNArguments* args)
// {
//     static_cast<void>(args);
// }
// 
// void Geometry::enable()
// {
// }
// 
// void Geometry::disable()
// {
// 
// }

// void Geometry::set_mask(unsigned int mask)
// {
//     rtcSetGeometryMask(m_geometry, mask);
// }

//void Geometry::set_buffer()
//{
    // auto* buffer = rtcSetNewGeometryBuffer(m_geometry,
    //     enum RTCBufferType type,
    //     unsigned int       slot,
    //     enum RTCFormat     format,
    //     size_t             byteStride,
    //     size_t             itemCount
    // );
    // rtcGetGeometryBufferData()
    // rtcUpdateGeometryBuffer()
//}

//void Geometry::set_intersection_filter()
//{
//    // rtcSetGeometryIntersectFilterFunction()
//}

} // namespace
