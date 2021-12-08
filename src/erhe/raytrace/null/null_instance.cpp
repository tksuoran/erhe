#include "erhe/raytrace/null/null_instance.hpp"

namespace erhe::raytrace
{

auto IInstance::create(const std::string_view debug_label) -> IInstance*
{
    return new Null_instance(debug_label);
}

auto IInstance::create_shared(const std::string_view debug_label) -> std::shared_ptr<IInstance>
{
    return std::make_shared<Null_instance>(debug_label);
}

auto IInstance::create_unique(const std::string_view debug_label) -> std::unique_ptr<IInstance>
{
    return std::make_unique<Null_instance>(debug_label);
}

Null_instance::Null_instance(const std::string_view debug_label)
    : m_debug_label(debug_label)
{
}

Null_instance::~Null_instance()
{
}

void Null_instance::commit()
{
}

void Null_instance::set_transform(const glm::mat4 transform)
{
    m_transform = transform;
}

void Null_instance::set_scene(IScene* scene)
{
    m_scene = scene;
}

void Null_instance::set_user_data(void* ptr)
{
    m_user_data = ptr;
}

auto Null_instance::get_user_data() -> void*
{
    return m_user_data;
}

auto Null_instance::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
