#include "erhe/raytrace/null/null_geometry.hpp"
#include "erhe/raytrace/null/null_scene.hpp"

namespace erhe::raytrace
{

auto IGeometry::create(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> IGeometry*
{
    return new Null_geometry(debug_label, geometry_type);
}

auto IGeometry::create_shared(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Null_geometry>(debug_label, geometry_type);
}

auto IGeometry::create_unique(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Null_geometry>(debug_label, geometry_type);
}

Null_geometry::Null_geometry(
    const std::string_view debug_label,
    const Geometry_type    geometry_type
)
    : m_debug_label{debug_label}
{
    static_cast<void>(geometry_type);
}

Null_geometry::~Null_geometry() = default;

void Null_geometry::set_user_data(void* ptr)
{
    m_user_data = ptr;
}

auto Null_geometry::get_user_data() -> void*
{
    return m_user_data;
}

auto Null_geometry::debug_label() const -> std::string_view
{
    return m_debug_label;
}


} // namespace erhe::raytrace
