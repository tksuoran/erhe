#include "erhe/raytrace/embree/embree_instance.hpp"
#include "erhe/raytrace/embree/embree_device.hpp"
#include "erhe/raytrace/embree/embree_scene.hpp"
#include "erhe/raytrace/log.hpp"
#include "erhe/log/log_glm.hpp"

namespace erhe::raytrace
{

auto IInstance::create(const std::string_view debug_label) -> IInstance*
{
    return new Embree_instance(debug_label);
}

auto IInstance::create_shared(const std::string_view debug_label) -> std::shared_ptr<IInstance>
{
    return std::make_shared<Embree_instance>(debug_label);
}

auto IInstance::create_unique(const std::string_view debug_label) -> std::unique_ptr<IInstance>
{
    return std::make_unique<Embree_instance>(debug_label);
}

Embree_instance::Embree_instance(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
    m_geometry = rtcNewGeometry(
        Embree_device::get_instance().get_rtc_device(),
        RTC_GEOMETRY_TYPE_INSTANCE
    );
    //log_embree.trace("rtcNewGeometry(RTC_GEOMETRY_TYPE_INSTANCE) = {}\n", debug_label);
    if (m_geometry != nullptr)
    {
        rtcSetGeometryUserData(m_geometry, this);
    }
}

Embree_instance::~Embree_instance()
{
    //log_embree.trace("rtcReleaseGeometry({})\n", m_debug_label);
    rtcReleaseGeometry(m_geometry);
}

auto Embree_instance::get_rtc_geometry() -> RTCGeometry
{
    return m_geometry;
}

void Embree_instance::set_transform(const glm::mat4 transform)
{
    const unsigned int time_step{0};
    //log_embree.info(
    //    "rtcSetGeometryTransform(\n"
    //    "    geometry = {},\n"
    //    "    transform =\n"
    //    "        {}\n"
    //    "        {}\n"
    //    "        {}\n"
    //    "        {}\n"
    //    ")\n",
    //    m_debug_label,
    //    transform[0],
    //    transform[1],
    //    transform[2],
    //    transform[3]
    //);
    rtcSetGeometryTransform(
        m_geometry,
        time_step,
        RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR,
        &transform[0][0]
    );
}

void Embree_instance::set_scene(IScene* scene)
{
    m_scene = static_cast<Embree_scene*>(scene);
    //log_embree.info(
    //    "rtcSetGeometryInstancedScene(instance = {}, scene = {})\n",
    //    m_debug_label,
    //    (m_scene != nullptr)
    //        ? m_scene->debug_label()
    //        : "()"
    //);
    rtcSetGeometryInstancedScene(
        m_geometry,
        (m_scene != nullptr)
            ? m_scene->get_rtc_scene()
            : nullptr
    );
}

auto Embree_instance::get_embree_scene() const -> Embree_scene*
{
    return m_scene;
}

void Embree_instance::commit()
{
    //log_embree.trace("rtcCommitGeometry(hgeometry = {})\n", m_debug_label);
    rtcCommitGeometry(m_geometry);
}

void Embree_instance::set_user_data(void* ptr)
{
    m_user_data = ptr;
}

auto Embree_instance::get_user_data() -> void*
{
    return m_user_data;
}

auto Embree_instance::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
