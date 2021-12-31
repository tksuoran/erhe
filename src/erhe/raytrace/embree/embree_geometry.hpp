#pragma once

#include "erhe/raytrace/igeometry.hpp"

#include <embree3/rtcore.h>

#include <string>

namespace erhe::raytrace
{

class Embree_geometry
    : public IGeometry
{
public:
    explicit Embree_geometry(const std::string_view debug_label); // rtcNewGeometry()
    ~Embree_geometry() override; // rtcReleaseGeometry()

    // rtcRetainGeometry()

    // Implements IGeometry
    void commit () override; // rtcCommitGeometry()
    void enable () override; // rtcEnableGeometry()
    void disable() override; // rtcDisableGeometry()
    void set_user_data(void* ptr) override;
    [[nodiscard]] auto get_user_data() -> void* override;
    [[nodiscard]] auto debug_label() const -> std::string_view override;

    // rtcSetGeometryTransformQuaternion()
    // rtcGetGeometryTransform()
    // rtcSetGeometryTimeStepCount()
    // rtcSetGeometryTimeRange()
    void set_vertex_attribute_count(const unsigned int count) override; // rtcSetGeometryVertexAttributeCount()

    // rtcSetGeometryMask()
    // rtcSetGeometryBuildQuality()

    void set_buffer( // rtcSetGeometryBuffer()
        const Buffer_type  type,
        const unsigned int slot,
        const Format       format,
        const IBuffer*     buffer,
        const size_t       byte_offset,
        const size_t       byte_stride,
        const size_t       item_count
    ) override;

    // rtcSetSharedGeometryBuffer()
    // rtcSetNewGeometryBuffer()
    // rtcGetGeometryBufferData()
    // rtcUpdateGeometryBuffer()
    // rtcSetGeometryIntersectFilterFunction()
    // rtcSetGeometryOccludedFilterFunction()
    // rtcFilterIntersection()
    // rtcFilterOcclusion()
    // rtcSetGeometryUserData()
    // rtcGetGeometryUserData()
    // rtcSetGeometryUserPrimitiveCount()
    // rtcSetGeometryBoundsFunction()
    // rtcSetGeometryIntersectFunction()
    // rtcSetGeometryOccludedFunction()
    // rtcSetGeometryPointQueryFunction()
    // rtcSetGeometryInstancedScene()

    // Subdivision
    //
    // rtcSetGeometryTessellationRate()
    // rtcSetGeometryTopologyCount()
    // rtcSetGeometrySubdivisionMode()
    // rtcSetGeometryVertexAttributeTopology()
    // rtcSetGeometryDisplacementFunction()

    // Half edge
    //
    // rtcGetGeometryFirstHalfEdge()
    // rtcGetGeometryFace()
    // rtcGetGeometryNextHalfEdge()
    // rtcGetGeometryPreviousHalfEdge()
    // rtcGetGeometryOppositeHalfEdge()

    // rtcInterpolate()
    // rtcInterpolateN()

    auto get_rtc_geometry() -> RTCGeometry;

    // TODO This limits to one scene.
    unsigned int geometry_id{0};

private:
    RTCGeometry m_geometry {nullptr};
    void*       m_user_data{nullptr};
    std::string m_debug_label;
};

}
