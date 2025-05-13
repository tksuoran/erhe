#include "erhe_raytrace/null/null_scene.hpp"
#include "erhe_raytrace/null/null_geometry.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"

namespace erhe::raytrace {

auto IScene::create(const std::string_view debug_label) -> IScene*
{
    return new Null_scene(debug_label);
}

auto IScene::create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>
{
    return std::make_shared<Null_scene>(debug_label);
}

auto IScene::create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>
{
    return std::make_unique<Null_scene>(debug_label);
}

Null_scene::Null_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
}

Null_scene::~Null_scene() noexcept
{
}

void Null_scene::attach(IGeometry* geometry)
{
#ifndef NDEBUG
    const auto i = std::find(m_geometries.begin(), m_geometries.end(), geometry);
    if (i != m_geometries.end()) {
        log_scene->error("raytrace geometry already in scene");
    }
    else
#endif
    {
        m_geometries.push_back(geometry);
    }
}

void Null_scene::attach(IInstance* instance)
{
#ifndef NDEBUG
    const auto i = std::find(m_instances.begin(), m_instances.end(), instance);
    if (i != m_instances.end())
    {
        log_scene->error("raytrace instance '{}' already in scene", instance->debug_label());
    }
    else
#endif
    {
        log_scene->info("raytrace instance '{}' attached to scene", instance->debug_label());
        m_instances.push_back(instance);
    }
}

void Null_scene::detach(IGeometry* geometry)
{
    const auto i = std::remove(m_geometries.begin(), m_geometries.end(), geometry);
    if (i == m_geometries.end()) {
        log_scene->error("raytrace geometry not in scene");
    } else {
        m_geometries.erase(i, m_geometries.end());
    }
}

void Null_scene::detach(IInstance* instance)
{
    const auto i = std::remove(m_instances.begin(), m_instances.end(), instance);
    if (i == m_instances.end()) {
        log_scene->error("raytrace instance not in scene");
    } else {
        m_instances.erase(i, m_instances.end());
    }
}

void Null_scene::commit()
{
}

auto Null_scene::intersect(Ray&, Hit&) -> bool
{
    return false;
}

auto Null_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

}
