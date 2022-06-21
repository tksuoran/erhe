#include "erhe/raytrace/embree/embree_geometry.hpp"
#include "erhe/raytrace/embree/embree_buffer.hpp"
#include "erhe/raytrace/embree/embree_device.hpp"
#include "erhe/raytrace/embree/embree_scene.hpp"
#include "erhe/raytrace/log.hpp"
#include "erhe/log/log_glm.hpp"

namespace erhe::raytrace
{

auto IGeometry::create(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> IGeometry*
{
    return new Embree_geometry(debug_label, geometry_type);
}

auto IGeometry::create_shared(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Embree_geometry>(debug_label, geometry_type);
}

auto IGeometry::create_unique(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Embree_geometry>(debug_label, geometry_type);
}

Embree_geometry::Embree_geometry(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
)
    : m_debug_label{debug_label}
{
    m_geometry = rtcNewGeometry(
        Embree_device::get_instance().get_rtc_device(),
        static_cast<RTCGeometryType>(geometry_type)
    );
    SPDLOG_LOGGER_TRACE(log_embree, "rtcNewGeometry() = {} {}", debug_label, fmt::ptr(m_geometry));
    if (m_geometry != nullptr)
    {
        rtcSetGeometryUserData(m_geometry, this);
    }

    rtcSetGeometryBuildQuality(m_geometry, RTC_BUILD_QUALITY_LOW);
}

Embree_geometry::~Embree_geometry() noexcept
{
    SPDLOG_LOGGER_TRACE(log_embree, "rtcReleaseGeometry({})", m_debug_label);
    rtcReleaseGeometry(m_geometry);
}

auto Embree_geometry::get_rtc_geometry() -> RTCGeometry
{
    return m_geometry;
}

void Embree_geometry::commit()
{
    SPDLOG_LOGGER_TRACE(log_embree, "rtcCommitGeometry({})", m_debug_label);
    rtcCommitGeometry(m_geometry);
}

void Embree_geometry::enable()
{
    SPDLOG_LOGGER_TRACE(log_embree, "rtcEnableGeometry(geometry = {})", m_debug_label);
    rtcEnableGeometry(m_geometry);
    m_enabled = true;
}

void Embree_geometry::disable()
{
    SPDLOG_LOGGER_TRACE(log_embree, "rtcDisableGeometry(geometry = {})", m_debug_label);
    rtcDisableGeometry(m_geometry);
    m_enabled = true;
}

auto Embree_geometry::is_enabled() const -> bool
{
    return m_enabled;
}

void Embree_geometry::set_mask(const uint32_t mask)
{
    SPDLOG_LOGGER_TRACE(log_embree, "rtcSetGeometryMask(geometry = {}, mask = {:#04x})", m_debug_label, mask);
    rtcSetGeometryMask(m_geometry, mask);
    m_mask = mask;
}

auto Embree_geometry::get_mask() const -> uint32_t
{
    return m_mask;
}

void Embree_geometry::set_user_data(void* ptr)
{
    m_user_data = ptr; //rtcSetGeometryUserData(m_geometry, ptr);
}

auto Embree_geometry::get_user_data() const -> void*
{
    return m_user_data; //return rtcGetGeometryUserData(m_geometry);
}


void Embree_geometry::set_vertex_attribute_count(
    const unsigned int count
)
{
    SPDLOG_LOGGER_TRACE(
        log_embree,
        "rtcSetGeometryVertexAttributeCount(geometry = {}, count = {})",
        m_debug_label,
        count
    );
    rtcSetGeometryVertexAttributeCount(m_geometry, count);
}

void Embree_geometry::set_buffer(
    const Buffer_type  type,
    const unsigned int slot,
    const Format       format,
    IBuffer* const     buffer,
    const std::size_t  byte_offset,
    const std::size_t  byte_stride,
    const std::size_t  item_count
)
{
    SPDLOG_LOGGER_TRACE(
        log_embree,
        "rtcSetGeometryBuffer(geometry = {}, slot = {})",
        m_debug_label,
        slot
    );
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
