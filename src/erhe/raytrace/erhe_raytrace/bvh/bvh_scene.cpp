#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable : 4702) // unreachable code
#   pragma warning(disable : 4714) // marked as __forceinline not inlined
#endif

#include "erhe_raytrace/bvh/bvh_scene.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_raytrace/bvh/bvh_geometry.hpp"
#include "erhe_raytrace/bvh/bvh_instance.hpp"
#include "erhe_raytrace/iinstance.hpp"
#include "erhe_raytrace/raytrace_log.hpp"
#include "erhe_raytrace/ray.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::raytrace {

auto IScene::create(const std::string_view debug_label) -> IScene*
{
    return new Bvh_scene(debug_label);
}

auto IScene::create_shared(const std::string_view debug_label) -> std::shared_ptr<IScene>
{
    return std::make_shared<Bvh_scene>(debug_label);
}

auto IScene::create_unique(const std::string_view debug_label) -> std::unique_ptr<IScene>
{
    return std::make_unique<Bvh_scene>(debug_label);
}

Bvh_scene::Bvh_scene(const std::string_view debug_label)
    : m_debug_label{debug_label}
{
    log_scene->trace("Created Bvh_scene '{}'", debug_label);
}

Bvh_scene::~Bvh_scene() noexcept
{
    log_scene->trace("Destroyed Bvh_scene '{}'", m_debug_label);
}

void Bvh_scene::attach(IGeometry* geometry)
{
    log_scene->trace("Bvh_scene {} attach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

#ifndef NDEBUG
    const auto i = std::find(m_geometries.begin(), m_geometries.end(), bvh_geometry);
    if (i != m_geometries.end()) {
        log_scene->error("raytrace geometry already in scene");
    } else
#endif
    {
        m_geometries.push_back(bvh_geometry);
    }
}

void Bvh_scene::attach(IInstance* instance)
{
    log_scene->trace("Bvh_scene {} attach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

#ifndef NDEBUG
    const auto i = std::find(m_instances.begin(), m_instances.end(), bvh_instance);
    if (i != m_instances.end()) {
        log_scene->error("raytrace instance already in scene");
    } else
#endif
    {
        m_instances.push_back(bvh_instance);
    }
}

void Bvh_scene::detach(IGeometry* geometry)
{
    log_scene->trace("Bvh_scene {} detach geometry {}", m_debug_label, geometry->debug_label());

    ERHE_VERIFY(geometry != nullptr);

    auto* bvh_geometry = reinterpret_cast<Bvh_geometry*>(geometry);

    const auto i = std::remove(m_geometries.begin(), m_geometries.end(), bvh_geometry);
    if (i == m_geometries.end()) {
        log_scene->error("raytrace geometry not in scene");
    } else {
        m_geometries.erase(i, m_geometries.end());
    }
}

void Bvh_scene::detach(IInstance* instance)
{
    log_scene->trace("Bvh_scene {} detach instance {}", m_debug_label, instance->debug_label());

    ERHE_VERIFY(instance != nullptr);

    auto* bvh_instance = reinterpret_cast<Bvh_instance*>(instance);

    const auto i = std::remove(m_instances.begin(), m_instances.end(), bvh_instance);
    if (i == m_instances.end()) {
        log_scene->error("raytrace instance not in scene");
    } else {
        m_instances.erase(i, m_instances.end());
    }
}

void Bvh_scene::commit()
{
    // TODO
}

auto Bvh_scene::intersect(Ray& ray, Hit& hit) -> bool
{
    // log_frame->trace(
    //     "Bvh_scene {} intersect mask = {:04x} instances = {}, geometries = {}, ray origin = {}, direction = {}",
    //     m_debug_label, ray.mask, m_instances.size(), m_geometries.size(), ray.origin, ray.direction
    // );

    ERHE_PROFILE_FUNCTION();

    bool is_hit = false;
    for (const auto& instance : m_instances) {
        const bool instance_is_hit = instance->intersect(ray, hit);
        if (instance_is_hit) {
            is_hit = true;
        }
    }
    for (const auto& geometry : m_geometries) {
        const bool geometry_is_hit = geometry->intersect_instance(ray, hit, nullptr);
        if (geometry_is_hit) {
            is_hit = true;
        }
    }
    return is_hit;
}

auto Bvh_scene::intersect_instance(Ray& ray, Hit& hit, Bvh_instance* in_instance) -> bool
{
    bool is_hit = false;
    if (in_instance == nullptr) {
        for (const auto& instance : m_instances) {
            const bool instance_is_hit = instance->intersect(ray, hit);
            if (instance_is_hit) {
                is_hit = true;
            }
        }
    } else {
        for (const auto& geometry : m_geometries) {
            const bool geometry_is_hit = geometry->intersect_instance(ray, hit, in_instance);
            if (geometry_is_hit) {
                is_hit = true;
            }
        }
    }
    return is_hit;
}

auto Bvh_scene::debug_label() const -> std::string_view
{
    return m_debug_label;
}

} // namespace erhe::raytrace

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif
