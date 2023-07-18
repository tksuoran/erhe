#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive_log.hpp"

namespace erhe::primitive
{

Material::Material()
{
}

Material::~Material() noexcept
{
}

Material::Material(
    const std::string_view name,
    const glm::vec3        base_color,
    const glm::vec2        roughness,
    const float            metallic
)
    : base_color   {base_color, 1.0f}
    , roughness    {roughness}
    , metallic     {metallic}
    , emissive     {0.0f, 0.0f, 0.0f, 0.0f}
    , m_id         {}
    , m_name       {name}
    , m_label      {fmt::format("{}##Node{}", name, m_id.get_id())}
    , m_shown_in_ui{true}
{
}

auto Material::get_static_type_name() -> const char*
{
    return "Material";
}

auto Material::get_type_name() const -> const char*
{
    return "Material";
}

auto Material::get_name() const -> const std::string&
{
    return m_name;
}

void Material::set_name(const std::string_view in_name)
{
    m_name = in_name;
    m_label = fmt::format("{}##Node{}", m_name, m_id.get_id());
}

auto Material::get_label() const -> const std::string&
{
    return m_label;
}

auto Material::is_shown_in_ui() const -> bool
{
    return m_shown_in_ui;
}

auto Material::get_id() const -> erhe::toolkit::Unique_id<Material>::id_type
{
    return m_id.get_id();
}

} // namespace erhe::primitive
