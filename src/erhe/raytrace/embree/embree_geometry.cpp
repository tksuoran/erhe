#include "erhe/raytrace/embree/embree_geometry.hpp"
#include "erhe/raytrace/embree/embree_buffer.hpp"
#include "erhe/raytrace/embree/embree_device.hpp"
#include "erhe/raytrace/embree/embree_scene.hpp"
#include "erhe/raytrace/log.hpp"
#include "erhe/log/log_glm.hpp"

namespace erhe::raytrace
{

auto IGeometry::create(const std::string_view debug_label) -> IGeometry*
{
    return new Embree_geometry(debug_label);
}

auto IGeometry::create_shared(const std::string_view debug_label) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Embree_geometry>(debug_label);
}

auto IGeometry::create_unique(const std::string_view debug_label) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Embree_geometry>(debug_label);
}

Embree_geometry::Embree_geometry(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
    m_geometry = rtcNewGeometry(
        Embree_device::get_instance().get_rtc_device(),
        RTC_GEOMETRY_TYPE_TRIANGLE
    );
    log_embree.trace("rtcNewGeometry() = {} {}\n", debug_label, fmt::ptr(m_geometry));
    if (m_geometry != nullptr)
    {
        rtcSetGeometryUserData(m_geometry, this);
    }
}

Embree_geometry::~Embree_geometry()
{
    log_embree.trace("rtcReleaseGeometry({})\n", m_debug_label);
    rtcReleaseGeometry(m_geometry);
}

auto Embree_geometry::get_rtc_geometry() -> RTCGeometry
{
    return m_geometry;
}

void Embree_geometry::commit()
{
    //log_embree.trace("rtcCommitGeometry({})\n", m_debug_label);
    rtcCommitGeometry(m_geometry);
}

void Embree_geometry::enable()
{
    //log_embree.trace("rtcEnableGeometry({})\n", m_debug_label);
    rtcEnableGeometry(m_geometry);
}

void Embree_geometry::disable()
{
    //log_embree.trace("rtcDisableGeometry({})\n", m_debug_label);
    rtcDisableGeometry(m_geometry);
}

void Embree_geometry::set_user_data(void* ptr)
{
    m_user_data = ptr; //rtcSetGeometryUserData(m_geometry, ptr);
}

auto Embree_geometry::get_user_data() -> void*
{
    return m_user_data; //return rtcGetGeometryUserData(m_geometry);
}

void Embree_geometry::set_vertex_attribute_count(
    const unsigned int count
)
{
    //log_embree.trace(
    //    "rtcSetGeometryVertexAttributeCount(geometry = {}, count = {})\n",
    //    m_debug_label,
    //    count
    //);
    rtcSetGeometryVertexAttributeCount(m_geometry, count);
}

void Embree_geometry::set_buffer(
    const Buffer_type  type,
    const unsigned int slot,
    const Format       format,
    const IBuffer*     buffer,
    const size_t       byte_offset,
    const size_t       byte_stride,
    const size_t       item_count
)
{
    //log_embree.trace(
    //    "rtcSetGeometryBuffer(geometry = {}, slot = {})\n",
    //    m_debug_label,
    //    slot
    //);
    rtcSetGeometryBuffer(
        m_geometry,
        static_cast<RTCBufferType>(type),
        slot,
        static_cast<RTCFormat>(format),
        (reinterpret_cast<const Embree_buffer*>(buffer))->get_rtc_buffer(),
        byte_offset,
        byte_stride,
        item_count
    );
}

auto Embree_geometry::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
