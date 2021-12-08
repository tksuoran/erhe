#include "erhe/raytrace/null/null_geometry.hpp"
#include "erhe/raytrace/null/null_scene.hpp"

namespace erhe::raytrace
{

auto IGeometry::create(const std::string_view debug_label) -> IGeometry*
{
    return new Null_geometry(debug_label);
}

auto IGeometry::create_shared(const std::string_view debug_label) -> std::shared_ptr<IGeometry>
{
    return std::make_shared<Null_geometry>(debug_label);
}

auto IGeometry::create_unique(const std::string_view debug_label) -> std::unique_ptr<IGeometry>
{
    return std::make_unique<Null_geometry>(debug_label);
}

Null_geometry::Null_geometry(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
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
