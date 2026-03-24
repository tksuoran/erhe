#include "erhe_raytrace/tinybvh/tinybvh_scene.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_geometry.hpp"
#include "erhe_raytrace/tinybvh/tinybvh_instance.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::raytrace {

auto IScene::create(const std::string_view debug_label) -> IScene*
{
    return new Tinybvh_scene(debug_label);
}

auto IScene::create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>
{
    return std::make_shared<Tinybvh_scene>(debug_label);
}

auto IScene::create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>
{
    return std::make_unique<Tinybvh_scene>(debug_label);
}

Tinybvh_scene::Tinybvh_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
    log_scene->trace("Created Tinybvh_scene '{}'", debug_label);
}

Tinybvh_scene::~Tinybvh_scene() noexcept
{
    log_scene->trace("Destroyed Tinybvh_scene '{}'", m_debug_label);
}

void Tinybvh_scene::attach(IGeometry* geometry)
{
    log_scene->trace("Tinybvh_scene {} attach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    Tinybvh_geometry* tinybvh_geometry = static_cast<Tinybvh_geometry*>(geometry);

#ifndef NDEBUG
    const auto i = std::find(m_geometries.begin(), m_geometries.end(), tinybvh_geometry);
    if (i != m_geometries.end()) {
        log_scene->error("raytrace geometry already in scene");
    } else
#endif
    {
        m_geometries.push_back(tinybvh_geometry);
    }
}

void Tinybvh_scene::attach(IInstance* instance)
{
    log_scene->trace("Tinybvh_scene {} attach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    Tinybvh_instance* tinybvh_instance = static_cast<Tinybvh_instance*>(instance);

#ifndef NDEBUG
    const auto i = std::find(m_instances.begin(), m_instances.end(), tinybvh_instance);
    if (i != m_instances.end()) {
        log_scene->error("raytrace instance already in scene");
    } else
#endif
    {
        m_instances.push_back(tinybvh_instance);
    }
}

void Tinybvh_scene::detach(IGeometry* geometry)
{
    log_scene->trace("Tinybvh_scene {} detach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    Tinybvh_geometry* tinybvh_geometry = static_cast<Tinybvh_geometry*>(geometry);

    const auto i = std::remove(m_geometries.begin(), m_geometries.end(), tinybvh_geometry);
    if (i == m_geometries.end()) {
        log_scene->error("raytrace geometry not in scene");
    } else {
        m_geometries.erase(i, m_geometries.end());
    }
}

void Tinybvh_scene::detach(IInstance* instance)
{
    log_scene->trace("Tinybvh_scene {} detach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    Tinybvh_instance* tinybvh_instance = static_cast<Tinybvh_instance*>(instance);

    const auto i = std::remove(m_instances.begin(), m_instances.end(), tinybvh_instance);
    if (i == m_instances.end()) {
        log_scene->error("raytrace instance not in scene");
    } else {
        m_instances.erase(i, m_instances.end());
    }
}

void Tinybvh_scene::commit()
{
    // No-op: BVH is built per-geometry in Tinybvh_geometry::commit()
}

auto Tinybvh_scene::intersect(Ray& ray, Hit& hit) -> bool
{
    ERHE_PROFILE_FUNCTION();

    bool is_hit = false;
    for (Tinybvh_instance* instance : m_instances) {
        const bool instance_is_hit = instance->intersect(ray, hit);
        if (instance_is_hit) {
            is_hit = true;
        }
    }
    for (Tinybvh_geometry* geometry : m_geometries) {
        const bool geometry_is_hit = geometry->intersect_instance(ray, hit, nullptr);
        if (geometry_is_hit) {
            is_hit = true;
        }
    }
    return is_hit;
}

auto Tinybvh_scene::intersect_instance(Ray& ray, Hit& hit, Tinybvh_instance* in_instance) -> bool
{
    bool is_hit = false;
    if (in_instance == nullptr) {
        for (Tinybvh_instance* instance : m_instances) {
            const bool instance_is_hit = instance->intersect(ray, hit);
            if (instance_is_hit) {
                is_hit = true;
            }
        }
    } else {
        for (Tinybvh_geometry* geometry : m_geometries) {
            const bool geometry_is_hit = geometry->intersect_instance(ray, hit, in_instance);
            if (geometry_is_hit) {
                is_hit = true;
            }
        }
    }
    return is_hit;
}

auto Tinybvh_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace
